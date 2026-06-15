#include "app/cli_interview_app.h"

#include "session/dialog_session.h"
#include "session/interview_report.h"
#include "session/interview_state.h"

#include <algorithm>
#include <string>
#include <vector>

namespace interview {
namespace app {

namespace {

// 把内部状态枚举转换成 CLI 更容易读懂的英文标签。
std::string interviewStateToString(session::InterviewState state) {
    switch (state) {
    case session::InterviewState::kConnecting:
        return "Connecting";
    case session::InterviewState::kInterviewerSpeaking:
        return "InterviewerSpeaking";
    case session::InterviewState::kIdle:
        return "Idle";
    case session::InterviewState::kCandidateSpeaking:
        return "CandidateSpeaking";
    case session::InterviewState::kInterviewerThinking:
        return "InterviewerThinking";
    case session::InterviewState::kSessionEnding:
        return "SessionEnding";
    case session::InterviewState::kCompleted:
        return "Completed";
    case session::InterviewState::kError:
        return "Error";
    }

    return "Unknown";
}

// 统一做状态切换并打印变化，便于测试和手动运行都观察同一条状态流。
void transitionState(session::DialogSession& interview_session, session::InterviewState next_state,
                     std::ostream& output) {
    const session::InterviewState current_state = interview_session.getState();
    interview_session.setState(next_state);
    output << "[State] " << interviewStateToString(current_state) << " -> "
           << interviewStateToString(next_state) << '\n';
}

// 总结阶段统一打印结构化问答记录，帮助用户回看主回答和追问表现。
void printSummary(std::size_t question_count, const session::DialogSession& interview_session,
                  std::ostream& output) {
    const std::vector<session::QuestionAnswerRecord>& records =
        interview_session.getQuestionAnswerRecords();

    output << "\n=== Interview Summary ===\n";
    output << "Answered " << records.size() << " of " << question_count << " questions.\n";

    const std::size_t item_count = std::min(question_count, records.size());
    for (std::size_t index = 0; index < item_count; ++index) {
        const session::QuestionAnswerRecord& record = records[index];
        output << index + 1 << ". Q: " << record.question << '\n';
        output << "   A: " << record.candidate_answer << '\n';
        if (record.has_follow_up) {
            output << "   Follow-up: " << record.follow_up_prompt << '\n';
            output << "   Follow-up A: " << record.follow_up_answer << '\n';
        }
        output << "   Score: " << record.final_score.score << "/100 - "
               << record.final_score.feedback << '\n';
    }
}

// 报告先直接打印成 JSON，后续再决定是否持久化到文件或接入 UI。
void printReportJson(const session::DialogSession& interview_session, std::ostream& output) {
    const nlohmann::json report = session::buildInterviewReportJson(interview_session);
    output << "\n=== Interview Report JSON ===\n";
    output << report.dump(2) << '\n';
}

} // namespace

int runCliInterview(std::istream& input, std::ostream& output,
                    session::PreparedInterview& prepared_interview) {
    if (!prepared_interview.isReady()) {
        output << prepared_interview.getErrorMessage() << '\n';
        return 1;
    }

    session::DialogSession interview_session;
    session::InterviewManager& manager = prepared_interview.getManager();

    // 开场阶段：先打招呼，再把会话切回等待状态。
    output << "=== AI Mock Interview CLI Demo ===\n";
    transitionState(interview_session, session::InterviewState::kInterviewerSpeaking, output);
    output << "Interviewer: Welcome, " << prepared_interview.getCandidateName() << ". ";
    output << "This mock interview is for the " << prepared_interview.getTargetRole() << " role.\n";
    transitionState(interview_session, session::InterviewState::kIdle, output);

    std::size_t question_number = 1;
    while (manager.hasCurrentQuestion()) {
        // 每轮先取当前题目；如果为空，说明流程状态已经不一致。
        const std::string* current_question = manager.getCurrentQuestion();
        if (current_question == nullptr) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "Interview stopped because no current question was found.\n";
            return 1;
        }

        // 面试官提问阶段。
        transitionState(interview_session, session::InterviewState::kInterviewerSpeaking, output);
        output << "\nQuestion " << question_number << "/" << manager.getQuestionCount() << ": "
               << *current_question << '\n';

        // 候选人输入阶段。
        transitionState(interview_session, session::InterviewState::kCandidateSpeaking, output);
        output << "Your answer: ";

        std::string answer;
        if (!std::getline(input, answer)) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "\nInput ended unexpectedly.\n";
            return 1;
        }

        // 思考阶段：先评分，再决定是否进入下一题。
        transitionState(interview_session, session::InterviewState::kInterviewerThinking, output);
        if (!manager.recordCandidateAnswer(interview_session, answer)) {
            transitionState(interview_session, session::InterviewState::kError, output);
            output << "Interview stopped because the answer could not be recorded.\n";
            return 1;
        }

        const services::LlmScoreResult score_result = manager.scoreCandidateAnswer(answer);
        output << "Score: " << score_result.score << "/100 - " << score_result.feedback << '\n';

        std::string follow_up_answer;
        services::LlmScoreResult final_score_result = score_result;
        const session::FollowUpDecision follow_up_decision = manager.decideFollowUp(score_result);

        if (follow_up_decision.needs_follow_up) {
            transitionState(interview_session, session::InterviewState::kInterviewerSpeaking,
                            output);
            output << "Follow-up: " << follow_up_decision.prompt << '\n';

            transitionState(interview_session, session::InterviewState::kCandidateSpeaking, output);
            output << "Your follow-up answer: ";

            if (!std::getline(input, follow_up_answer)) {
                transitionState(interview_session, session::InterviewState::kError, output);
                output << "\nInput ended unexpectedly.\n";
                return 1;
            }

            transitionState(interview_session, session::InterviewState::kInterviewerThinking,
                            output);
            final_score_result = manager.scoreCandidateAnswer(answer + " " + follow_up_answer);
            output << "Updated score: " << final_score_result.score << "/100 - "
                   << final_score_result.feedback << '\n';
        }

        session::QuestionAnswerRecord record;
        record.question = *current_question;
        record.candidate_answer = answer;
        record.has_follow_up = follow_up_decision.needs_follow_up;
        record.follow_up_prompt = follow_up_decision.prompt;
        record.follow_up_answer = follow_up_answer;
        record.final_score = {final_score_result.score, final_score_result.feedback};
        // 评分历史和结构化记录都写入会话，后续报告导出和 UI 读取可复用同一份数据。
        interview_session.addScoreResult(final_score_result.score, final_score_result.feedback);
        interview_session.addQuestionAnswerRecord(record);

        manager.moveToNextQuestion();
        ++question_number;

        if (manager.hasCurrentQuestion()) {
            // 还有下一题时，回到空闲态等待下一轮开始。
            transitionState(interview_session, session::InterviewState::kIdle, output);
        }
    }

    // 所有题目结束后，进入总结和收尾阶段。
    transitionState(interview_session, session::InterviewState::kSessionEnding, output);
    printSummary(manager.getQuestionCount(), interview_session, output);
    printReportJson(interview_session, output);
    transitionState(interview_session, session::InterviewState::kCompleted, output);
    output << "Interview completed.\n";
    return 0;
}

} // namespace app
} // namespace interview
