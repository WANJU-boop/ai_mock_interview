#include "session/dialog_orchestrator.h"

#include "common/realtime_protocol.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <optional>
#include <string>
#include <thread>

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
                 RealtimeAudioBridge* audio_bridge, const std::string& error_message) {
    result.success = false;
    result.error_message = error_message;
    transitionState(result.session, InterviewState::kError);
    if (audio_bridge != nullptr) {
        // 先停止 PortAudio callback，再关闭 WSS；这样 callback 不会继续累积无法发送的 PCM。
        audio_bridge->stop();
    }
    realtime_client.close();
}

// 只有底层确认发送成功后才记录 interviewer_messages，
// 这样结果对象表示“实际发出的文本”，而不是“尝试发送的文本”。
bool sendInterviewerText(DialogOrchestratorResult& result,
                         services::IRealtimeClient& realtime_client,
                         RealtimeAudioBridge* audio_bridge, const std::string& text) {
    if (audio_bridge != nullptr) {
        audio_bridge->suspendCaptureForwarding();
    }
    if (!realtime_client.sendInterviewerText(text)) {
        failSession(result, realtime_client, audio_bridge,
                    "发送面试官文本失败，realtime 会话可能已经关闭。");
        return false;
    }

    result.interviewer_messages.push_back(text);
    return true;
}

// 开始新题时重置 pending_record，防止上一题追问标记或回答泄漏到下一题。
bool askCurrentQuestion(DialogOrchestratorResult& result,
                        services::IRealtimeClient& realtime_client, InterviewManager& manager,
                        RealtimeAudioBridge* audio_bridge, std::size_t question_number,
                        PendingQuestionAnswer& pending_record, const std::string& opening_text) {
    const std::string* current_question = manager.getCurrentQuestion();
    if (current_question == nullptr) {
        failSession(result, realtime_client, audio_bridge, "没有可提问的当前题目。");
        return false;
    }

    pending_record = PendingQuestionAnswer{};
    pending_record.record.question = *current_question;

    transitionState(result.session, InterviewState::kInterviewerSpeaking);
    const std::string question_text = opening_text + "问题 " + std::to_string(question_number) +
                                      "/" + std::to_string(manager.getQuestionCount()) + "：" +
                                      *current_question + " 我说完后，请开始回答。";
    if (!sendInterviewerText(result, realtime_client, audio_bridge, question_text)) {
        return false;
    }

    // 提问后马上进入候选人回答态，等待后续 transcript_final 事件。
    transitionState(result.session, InterviewState::kCandidateSpeaking);
    return true;
}

// 正常结束也先发送结束语，再进入 kCompleted；发送失败仍应落入统一错误收口。
void completeSession(DialogOrchestratorResult& result, services::IRealtimeClient& realtime_client,
                     RealtimeAudioBridge* audio_bridge) {
    transitionState(result.session, InterviewState::kCompleted);
    result.success = true;
    if (audio_bridge != nullptr) {
        audio_bridge->stop();
    }
    realtime_client.close();
}

bool finishSession(DialogOrchestratorResult& result, services::IRealtimeClient& realtime_client,
                   RealtimeAudioBridge* audio_bridge) {
    transitionState(result.session, InterviewState::kSessionEnding);
    if (!sendInterviewerText(result, realtime_client, audio_bridge,
                             "本次模拟面试结束，正在生成报告。")) {
        return false;
    }

    if (audio_bridge == nullptr) {
        // mock/text 模式没有本地播放队列，发送成功即可完成。
        completeSession(result, realtime_client, audio_bridge);
    }
    return true;
}

std::optional<services::LlmScoreResult>
scoreAnswerKeepingAudioAlive(InterviewManager& manager, services::IRealtimeClient& realtime_client,
                             RealtimeAudioBridge* audio_bridge, const std::string& answer,
                             std::string& error_message) {
    if (audio_bridge == nullptr) {
        // 离线 mock 不需要额外线程，保持单元测试的确定性。
        try {
            return manager.scoreCandidateAnswer(answer);
        } catch (const std::exception& error) {
            error_message = "LLM 评分失败：" + std::string(error.what());
            return std::nullopt;
        }
    }

    audio_bridge->muteCaptureForwarding();
    // HTTP LLM 是阻塞调用。将它放到可 join 的 future 中，realtime 所有者线程
    // 继续发送静音保活并消费服务端 ack，避免 30~60 秒评分期间 WSS 超时。
    std::future<services::LlmScoreResult> score_future = std::async(
        std::launch::async, [&manager, answer]() { return manager.scoreCandidateAnswer(answer); });

    while (score_future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        if (!audio_bridge->pumpCapturedAudio()) {
            error_message = "LLM 评分期间发送音频保活失败。";
            return std::nullopt;
        }

        const std::optional<common::RealtimeEvent> event = realtime_client.tryReceiveNextEvent();
        if (event.has_value()) {
            if (!audio_bridge->consumeRealtimeEvent(*event)) {
                error_message = "LLM 评分期间 TTS 音频播放失败。";
                return std::nullopt;
            }
            if (event->type == common::RealtimeEventType::kError ||
                event->type == common::RealtimeEventType::kClosed) {
                error_message = event->error_message.empty() ? "LLM 评分期间 realtime 连接关闭。"
                                                             : event->error_message;
                return std::nullopt;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    try {
        return score_future.get();
    } catch (const std::exception& error) {
        error_message = "LLM 评分失败：" + std::string(error.what());
        return std::nullopt;
    }
}

// 题目下标只由 InterviewManager 推进，展示用的 question_number 在确认还有下一题后再递增。
bool moveToNextQuestionOrFinish(DialogOrchestratorResult& result,
                                services::IRealtimeClient& realtime_client,
                                InterviewManager& manager, RealtimeAudioBridge* audio_bridge,
                                std::size_t& question_number,
                                PendingQuestionAnswer& pending_record) {
    if (!manager.moveToNextQuestion()) {
        return finishSession(result, realtime_client, audio_bridge);
    }

    ++question_number;
    transitionState(result.session, InterviewState::kIdle);
    return askCurrentQuestion(result, realtime_client, manager, audio_bridge, question_number,
                              pending_record, "");
}

// 主回答先写入回答历史并完成首次评分；若触发追问，结构化记录暂不落盘，
// 等下一条 final transcript 到达后再生成最终评分。
bool processPrimaryAnswer(DialogOrchestratorResult& result,
                          services::IRealtimeClient& realtime_client, InterviewManager& manager,
                          RealtimeAudioBridge* audio_bridge, std::size_t& question_number,
                          PendingQuestionAnswer& pending_record, const std::string& answer) {
    transitionState(result.session, InterviewState::kInterviewerThinking);
    if (!manager.recordCandidateAnswer(result.session, answer)) {
        failSession(result, realtime_client, audio_bridge, "候选人回答无法被记录。");
        return false;
    }

    pending_record.record.candidate_answer = answer;
    std::string score_error;
    const std::optional<services::LlmScoreResult> score_result =
        scoreAnswerKeepingAudioAlive(manager, realtime_client, audio_bridge, answer, score_error);
    if (!score_result.has_value()) {
        failSession(result, realtime_client, audio_bridge, score_error);
        return false;
    }
    const FollowUpDecision follow_up = manager.decideFollowUp(*score_result);
    if (follow_up.needs_follow_up) {
        pending_record.waiting_for_follow_up = true;
        pending_record.record.has_follow_up = true;
        pending_record.record.follow_up_prompt = follow_up.prompt;

        transitionState(result.session, InterviewState::kInterviewerSpeaking);
        if (!sendInterviewerText(result, realtime_client, audio_bridge, follow_up.prompt)) {
            return false;
        }

        transitionState(result.session, InterviewState::kCandidateSpeaking);
        return true;
    }

    pending_record.record.final_score = {score_result->score, score_result->feedback};
    result.session.addScoreResult(score_result->score, score_result->feedback);
    result.session.addQuestionAnswerRecord(pending_record.record);
    return moveToNextQuestionOrFinish(result, realtime_client, manager, audio_bridge,
                                      question_number, pending_record);
}

// 追问评分使用“主回答 + 追问回答”的组合上下文，最终只追加一条题目记录，
// 保证报告题数仍与实际主问题数量一致。
bool processFollowUpAnswer(DialogOrchestratorResult& result,
                           services::IRealtimeClient& realtime_client, InterviewManager& manager,
                           RealtimeAudioBridge* audio_bridge, std::size_t& question_number,
                           PendingQuestionAnswer& pending_record,
                           const std::string& follow_up_answer) {
    transitionState(result.session, InterviewState::kInterviewerThinking);
    pending_record.record.follow_up_answer = follow_up_answer;
    const std::string combined_answer =
        pending_record.record.candidate_answer + " " + follow_up_answer;
    std::string score_error;
    const std::optional<services::LlmScoreResult> final_score = scoreAnswerKeepingAudioAlive(
        manager, realtime_client, audio_bridge, combined_answer, score_error);
    if (!final_score.has_value()) {
        failSession(result, realtime_client, audio_bridge, score_error);
        return false;
    }

    pending_record.record.final_score = {final_score->score, final_score->feedback};
    result.session.addScoreResult(final_score->score, final_score->feedback);
    result.session.addQuestionAnswerRecord(pending_record.record);

    return moveToNextQuestionOrFinish(result, realtime_client, manager, audio_bridge,
                                      question_number, pending_record);
}

} // namespace

DialogOrchestrator::DialogOrchestrator(PreparedInterview& prepared_interview,
                                       services::IRealtimeClient& realtime_client,
                                       RealtimeAudioBridge* audio_bridge)
    : prepared_interview_(prepared_interview), realtime_client_(realtime_client),
      audio_bridge_(audio_bridge) {}

DialogOrchestratorResult DialogOrchestrator::run() {
    DialogOrchestratorResult result;
    if (!prepared_interview_.isReady()) {
        // 准备阶段失败时不尝试连接外部服务，直接复用已经收口好的错误信息。
        failSession(result, realtime_client_, audio_bridge_, prepared_interview_.getErrorMessage());
        return result;
    }

    if (!realtime_client_.connect()) {
        // connect 返回 false 表示客户端没有建立可用事件流，后续不能继续读取或发送。
        const std::string detail = realtime_client_.getLastErrorMessage();
        failSession(result, realtime_client_, audio_bridge_,
                    detail.empty() ? "realtime 会话连接失败。"
                                   : "realtime 会话连接失败：" + detail);
        return result;
    }

    if (audio_bridge_ != nullptr && !audio_bridge_->start()) {
        failSession(result, realtime_client_, audio_bridge_, "本地音频设备无法启动。");
        return result;
    }

    InterviewManager& manager = prepared_interview_.getManager();
    PendingQuestionAnswer pending_record;
    bool interview_started = false;
    std::size_t question_number = 1;

    // orchestrator 用非阻塞轮询交替执行“发 20ms 录音”和“收服务端事件”。
    // audio_bridge 只由这个同一线程调用，保证 WSS 的 read/write/close
    // 只有一个所有者；PortAudio callback 只通过无锁队列提供已采集 PCM。
    // 后续接 Qt 时，不应在 UI 线程直接调用 run()，而应把整个 run() 放到一个 joinable worker。
    while (realtime_client_.hasNextEvent()) {
        if (audio_bridge_ != nullptr && !audio_bridge_->pumpCapturedAudio()) {
            failSession(result, realtime_client_, audio_bridge_, "候选人音频发送失败。");
            return result;
        }
        if (audio_bridge_ != nullptr &&
            result.session.getState() == InterviewState::kSessionEnding &&
            audio_bridge_->isInterviewerPlaybackComplete()) {
            // 结束语的服务端音频和本地播放队列都已清空，现在关闭不会截断尾音。
            completeSession(result, realtime_client_, audio_bridge_);
            return result;
        }
        const std::optional<common::RealtimeEvent> next_event =
            realtime_client_.tryReceiveNextEvent();
        if (!next_event.has_value()) {
            // 5ms 休眠避免空轮询占满 CPU，同时远小于 20ms 音频包节奏。
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        const common::RealtimeEvent& event = *next_event;
        if (audio_bridge_ != nullptr && !audio_bridge_->consumeRealtimeEvent(event)) {
            failSession(result, realtime_client_, audio_bridge_, "面试官 TTS 音频播放失败。");
            return result;
        }
        switch (event.type) {
        case common::RealtimeEventType::kConnected: {
            if (interview_started) {
                // 重复 connected 不应重复发送欢迎语或第一题，保持状态机幂等。
                break;
            }

            interview_started = true;
            // 开场还未收到 ASREnded，因此把欢迎语和第一题合并成一次 SayHello。
            // 这也避免两段 TTS 同时入队造成重叠播放。
            const std::string opening_text = "欢迎你，" + prepared_interview_.getCandidateName() +
                                             "。本次模拟面试岗位是 " +
                                             prepared_interview_.getTargetRole() + "。";
            transitionState(result.session, InterviewState::kIdle);
            if (!askCurrentQuestion(result, realtime_client_, manager, audio_bridge_,
                                    question_number, pending_record, opening_text)) {
                return result;
            }
            break;
        }

        case common::RealtimeEventType::kTranscriptPartial:
            if (!interview_started) {
                failSession(result, realtime_client_, audio_bridge_,
                            "收到 partial transcript 前尚未开始面试。");
                return result;
            }
            // partial transcript 只用于未来 UI 实时展示，不推进评分和题目状态。
            result.partial_transcripts.push_back(event.text);
            transitionState(result.session, InterviewState::kCandidateSpeaking);
            break;

        case common::RealtimeEventType::kTranscriptFinal:
            if (!interview_started) {
                failSession(result, realtime_client_, audio_bridge_,
                            "收到 final transcript 前尚未开始面试。");
                return result;
            }

            if (pending_record.waiting_for_follow_up) {
                if (!processFollowUpAnswer(result, realtime_client_, manager, audio_bridge_,
                                           question_number, pending_record, event.text)) {
                    return result;
                }
            } else if (!processPrimaryAnswer(result, realtime_client_, manager, audio_bridge_,
                                             question_number, pending_record, event.text)) {
                return result;
            }

            if (result.success || result.session.isFinished()) {
                return result;
            }
            break;

        case common::RealtimeEventType::kInterviewerText:
            // 服务端回传的面试官文本当前只作为兼容事件保留；主流程的权威输出来自本地发送。
            break;

        case common::RealtimeEventType::kTtsStarted:
        case common::RealtimeEventType::kTtsEnded:
            // TTS 事件已由 audio_bridge 处理：它们只控制麦克风门禁，不改变面试业务状态。
            break;

        case common::RealtimeEventType::kError:
            failSession(result, realtime_client_, audio_bridge_,
                        event.error_message.empty() ? "realtime 服务返回未知错误。"
                                                    : event.error_message);
            return result;

        case common::RealtimeEventType::kClosed:
            failSession(result, realtime_client_, audio_bridge_, "realtime 连接在面试完成前关闭。");
            return result;
        }
    }

    if (!result.session.isFinished()) {
        // hasNextEvent() 变为 false 但尚未完成，说明脚本过短或真实连接提前耗尽。
        failSession(result, realtime_client_, audio_bridge_,
                    "realtime 事件流结束，但面试尚未完成。");
    }

    return result;
}

} // namespace session
} // namespace interview
