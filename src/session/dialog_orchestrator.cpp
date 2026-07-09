#include "session/dialog_orchestrator.h"

#include "common/realtime_protocol.h"

#include <cstddef>
#include <string>

// DialogOrchestrator 是 realtime 事件和面试领域逻辑之间的同步状态机：
// IRealtimeClient 产生事件 -> InterviewManager 记录/评分 -> DialogSession 保存结果 ->
// 面试官文本再通过 IRealtimeClient 发出。任何失败都统一进入 kError 并关闭客户端。
namespace interview {
namespace session {

namespace {

// 当前题的临时记录跨越“主回答 -> 可选追问 -> 最终评分”多个事件，
// 只有最终评分完成后才写入 DialogSession，避免报告里出现半条记录。
struct PendingQuestionAnswer {
    QuestionAnswerRecord record;
    bool waiting_for_follow_up = false;
};

// 状态切换集中在一个入口，后续接 Qt signal 或状态日志时只需要扩展这里。
void transitionState(DialogSession& session, InterviewState next_state) {
    session.setState(next_state);
}

// 所有错误路径执行同一组动作：保存可展示错误、进入终态并关闭外部资源。
// close() 由接口约定为可重复调用，因此上层清理不必判断失败发生在哪个阶段。
void failSession(DialogOrchestratorResult& result, services::IRealtimeClient& realtime_client,
                 const std::string& error_message) {
    result.success = false;
    result.error_message = error_message;
    transitionState(result.session, InterviewState::kError);
    realtime_client.close();
}

// 只有底层确认发送成功后才记录 interviewer_messages，
// 这样结果对象表示“实际发出的文本”，而不是“尝试发送的文本”。
bool sendInterviewerText(DialogOrchestratorResult& result,
                         services::IRealtimeClient& realtime_client, const std::string& text) {
    if (!realtime_client.sendInterviewerText(text)) {
        failSession(result, realtime_client, "发送面试官文本失败，realtime 会话可能已经关闭。");
        return false;
    }

    result.interviewer_messages.push_back(text);
    return true;
}

// 开始新题时重置 pending_record，防止上一题追问标记或回答泄漏到下一题。
bool askCurrentQuestion(DialogOrchestratorResult& result,
                        services::IRealtimeClient& realtime_client, InterviewManager& manager,
                        std::size_t question_number, PendingQuestionAnswer& pending_record) {
    const std::string* current_question = manager.getCurrentQuestion();
    if (current_question == nullptr) {
        failSession(result, realtime_client, "没有可提问的当前题目。");
        return false;
    }

    pending_record = PendingQuestionAnswer{};
    pending_record.record.question = *current_question;

    transitionState(result.session, InterviewState::kInterviewerSpeaking);
    const std::string question_text = "问题 " + std::to_string(question_number) + "/" +
                                      std::to_string(manager.getQuestionCount()) + "：" +
                                      *current_question;
    if (!sendInterviewerText(result, realtime_client, question_text)) {
        return false;
    }

    // 提问后马上进入候选人回答态，等待后续 transcript_final 事件。
    transitionState(result.session, InterviewState::kCandidateSpeaking);
    return true;
}

// 正常结束也先发送结束语，再进入 kCompleted；发送失败仍应落入统一错误收口。
bool finishSession(DialogOrchestratorResult& result, services::IRealtimeClient& realtime_client) {
    transitionState(result.session, InterviewState::kSessionEnding);
    if (!sendInterviewerText(result, realtime_client, "本次模拟面试结束，正在生成报告。")) {
        return false;
    }

    transitionState(result.session, InterviewState::kCompleted);
    result.success = true;
    realtime_client.close();
    return true;
}

// 题目下标只由 InterviewManager 推进，展示用的 question_number 在确认还有下一题后再递增。
bool moveToNextQuestionOrFinish(DialogOrchestratorResult& result,
                                services::IRealtimeClient& realtime_client,
                                InterviewManager& manager, std::size_t& question_number,
                                PendingQuestionAnswer& pending_record) {
    if (!manager.moveToNextQuestion()) {
        return finishSession(result, realtime_client);
    }

    ++question_number;
    transitionState(result.session, InterviewState::kIdle);
    return askCurrentQuestion(result, realtime_client, manager, question_number, pending_record);
}

// 主回答先写入回答历史并完成首次评分；若触发追问，结构化记录暂不落盘，
// 等下一条 final transcript 到达后再生成最终评分。
bool processPrimaryAnswer(DialogOrchestratorResult& result,
                          services::IRealtimeClient& realtime_client, InterviewManager& manager,
                          std::size_t& question_number, PendingQuestionAnswer& pending_record,
                          const std::string& answer) {
    transitionState(result.session, InterviewState::kInterviewerThinking);
    if (!manager.recordCandidateAnswer(result.session, answer)) {
        failSession(result, realtime_client, "候选人回答无法被记录。");
        return false;
    }

    pending_record.record.candidate_answer = answer;
    const services::LlmScoreResult score_result = manager.scoreCandidateAnswer(answer);
    const FollowUpDecision follow_up = manager.decideFollowUp(score_result);
    if (follow_up.needs_follow_up) {
        pending_record.waiting_for_follow_up = true;
        pending_record.record.has_follow_up = true;
        pending_record.record.follow_up_prompt = follow_up.prompt;

        transitionState(result.session, InterviewState::kInterviewerSpeaking);
        if (!sendInterviewerText(result, realtime_client, follow_up.prompt)) {
            return false;
        }

        transitionState(result.session, InterviewState::kCandidateSpeaking);
        return true;
    }

    pending_record.record.final_score = {score_result.score, score_result.feedback};
    result.session.addScoreResult(score_result.score, score_result.feedback);
    result.session.addQuestionAnswerRecord(pending_record.record);
    return moveToNextQuestionOrFinish(result, realtime_client, manager, question_number,
                                      pending_record);
}

// 追问评分使用“主回答 + 追问回答”的组合上下文，最终只追加一条题目记录，
// 保证报告题数仍与实际主问题数量一致。
bool processFollowUpAnswer(DialogOrchestratorResult& result,
                           services::IRealtimeClient& realtime_client, InterviewManager& manager,
                           std::size_t& question_number, PendingQuestionAnswer& pending_record,
                           const std::string& follow_up_answer) {
    transitionState(result.session, InterviewState::kInterviewerThinking);
    pending_record.record.follow_up_answer = follow_up_answer;
    const std::string combined_answer =
        pending_record.record.candidate_answer + " " + follow_up_answer;
    const services::LlmScoreResult final_score = manager.scoreCandidateAnswer(combined_answer);

    pending_record.record.final_score = {final_score.score, final_score.feedback};
    result.session.addScoreResult(final_score.score, final_score.feedback);
    result.session.addQuestionAnswerRecord(pending_record.record);

    return moveToNextQuestionOrFinish(result, realtime_client, manager, question_number,
                                      pending_record);
}

} // namespace

DialogOrchestrator::DialogOrchestrator(PreparedInterview& prepared_interview,
                                       services::IRealtimeClient& realtime_client)
    : prepared_interview_(prepared_interview), realtime_client_(realtime_client) {}

DialogOrchestratorResult DialogOrchestrator::run() {
    DialogOrchestratorResult result;
    if (!prepared_interview_.isReady()) {
        // 准备阶段失败时不尝试连接外部服务，直接复用已经收口好的错误信息。
        failSession(result, realtime_client_, prepared_interview_.getErrorMessage());
        return result;
    }

    if (!realtime_client_.connect()) {
        // connect 返回 false 表示客户端没有建立可用事件流，后续不能继续读取或发送。
        failSession(result, realtime_client_, "realtime 会话连接失败。");
        return result;
    }

    InterviewManager& manager = prepared_interview_.getManager();
    PendingQuestionAnswer pending_record;
    bool interview_started = false;
    std::size_t question_number = 1;

    // 当前 orchestrator 仍是同步事件循环：mock 测试能确定性复现，
    // 真实 WebSocket adapter 可以把 receiveNextEvent 实现成阻塞读取。
    // 后续接 Qt 时，不应在 UI 线程直接调用 run()，而应放到 worker 线程并把事件派发回主线程。
    while (realtime_client_.hasNextEvent()) {
        const common::RealtimeEvent event = realtime_client_.receiveNextEvent();
        switch (event.type) {
        case common::RealtimeEventType::kConnected:
            if (interview_started) {
                // 重复 connected 不应重复发送欢迎语或第一题，保持状态机幂等。
                break;
            }

            interview_started = true;
            transitionState(result.session, InterviewState::kInterviewerSpeaking);
            if (!sendInterviewerText(result, realtime_client_,
                                     "欢迎你，" + prepared_interview_.getCandidateName() +
                                         "。本次模拟面试岗位是 " +
                                         prepared_interview_.getTargetRole() + "。")) {
                return result;
            }

            transitionState(result.session, InterviewState::kIdle);
            if (!askCurrentQuestion(result, realtime_client_, manager, question_number,
                                    pending_record)) {
                return result;
            }
            break;

        case common::RealtimeEventType::kTranscriptPartial:
            if (!interview_started) {
                failSession(result, realtime_client_, "收到 partial transcript 前尚未开始面试。");
                return result;
            }
            // partial transcript 只用于未来 UI 实时展示，不推进评分和题目状态。
            result.partial_transcripts.push_back(event.text);
            transitionState(result.session, InterviewState::kCandidateSpeaking);
            break;

        case common::RealtimeEventType::kTranscriptFinal:
            if (!interview_started) {
                failSession(result, realtime_client_, "收到 final transcript 前尚未开始面试。");
                return result;
            }

            if (pending_record.waiting_for_follow_up) {
                if (!processFollowUpAnswer(result, realtime_client_, manager, question_number,
                                           pending_record, event.text)) {
                    return result;
                }
            } else if (!processPrimaryAnswer(result, realtime_client_, manager, question_number,
                                             pending_record, event.text)) {
                return result;
            }

            if (result.success || result.session.isFinished()) {
                return result;
            }
            break;

        case common::RealtimeEventType::kInterviewerText:
            // 服务端回传的面试官文本当前只作为兼容事件保留；主流程的权威输出来自本地发送。
            break;

        case common::RealtimeEventType::kError:
            failSession(result, realtime_client_,
                        event.error_message.empty() ? "realtime 服务返回未知错误。"
                                                    : event.error_message);
            return result;

        case common::RealtimeEventType::kClosed:
            failSession(result, realtime_client_, "realtime 连接在面试完成前关闭。");
            return result;
        }
    }

    if (!result.session.isFinished()) {
        // hasNextEvent() 变为 false 但尚未完成，说明脚本过短或真实连接提前耗尽。
        failSession(result, realtime_client_, "realtime 事件流结束，但面试尚未完成。");
    }

    return result;
}

} // namespace session
} // namespace interview
