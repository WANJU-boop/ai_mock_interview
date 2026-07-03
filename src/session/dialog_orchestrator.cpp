#include "session/dialog_orchestrator.h"

#include "common/realtime_protocol.h"

#include <cstddef>
#include <string>

namespace interview {
namespace session {

namespace {

struct PendingQuestionAnswer {
    QuestionAnswerRecord record;
    bool waiting_for_follow_up = false;
};

void transitionState(DialogSession& session, InterviewState next_state) {
    session.setState(next_state);
}

void failSession(DialogOrchestratorResult& result, services::IRealtimeClient& realtime_client,
                 const std::string& error_message) {
    result.success = false;
    result.error_message = error_message;
    transitionState(result.session, InterviewState::kError);
    realtime_client.close();
}

bool sendInterviewerText(DialogOrchestratorResult& result,
                         services::IRealtimeClient& realtime_client, const std::string& text) {
    if (!realtime_client.sendInterviewerText(text)) {
        failSession(result, realtime_client, "发送面试官文本失败，realtime 会话可能已经关闭。");
        return false;
    }

    result.interviewer_messages.push_back(text);
    return true;
}

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
        failSession(result, realtime_client_, prepared_interview_.getErrorMessage());
        return result;
    }

    if (!realtime_client_.connect()) {
        failSession(result, realtime_client_, "realtime 会话连接失败。");
        return result;
    }

    InterviewManager& manager = prepared_interview_.getManager();
    PendingQuestionAnswer pending_record;
    bool interview_started = false;
    std::size_t question_number = 1;

    while (realtime_client_.hasNextEvent()) {
        const common::RealtimeEvent event = realtime_client_.receiveNextEvent();
        switch (event.type) {
        case common::RealtimeEventType::kConnected:
            if (interview_started) {
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
        failSession(result, realtime_client_, "realtime 事件流结束，但面试尚未完成。");
    }

    return result;
}

} // namespace session
} // namespace interview
