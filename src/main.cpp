#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "common/logger.h"
#include "session/dialog_session.h"
#include "session/interview_manager.h"
#include "session/interview_state.h"

namespace {

// 把内部状态枚举转换成 CLI 更容易读懂的英文标签。
std::string interviewStateToString(interview::session::InterviewState state) {
    switch (state) {
        case interview::session::InterviewState::kConnecting:
            return "Connecting";
        case interview::session::InterviewState::kInterviewerSpeaking:
            return "InterviewerSpeaking";
        case interview::session::InterviewState::kIdle:
            return "Idle";
        case interview::session::InterviewState::kCandidateSpeaking:
            return "CandidateSpeaking";
        case interview::session::InterviewState::kInterviewerThinking:
            return "InterviewerThinking";
        case interview::session::InterviewState::kSessionEnding:
            return "SessionEnding";
        case interview::session::InterviewState::kCompleted:
            return "Completed";
        case interview::session::InterviewState::kError:
            return "Error";
    }

    return "Unknown";
}

// 统一做状态切换并打印变化，便于观察整个流程是怎么推进的。
void transitionState(interview::session::DialogSession& session, interview::session::InterviewState next_state) {
    const interview::session::InterviewState current_state = session.getState();
    session.setState(next_state);
    std::cout << "[State] " << interviewStateToString(current_state) << " -> " << interviewStateToString(next_state)
              << '\n';
}

// 总结阶段统一打印结构化问答记录，帮助用户回看主回答和追问表现。
void printSummary(const std::vector<std::string>& questions, const interview::session::DialogSession& session) {
    const std::vector<interview::session::QuestionAnswerRecord>& records = session.getQuestionAnswerRecords();

    std::cout << "\n=== Interview Summary ===\n";
    std::cout << "Answered " << records.size() << " of " << questions.size() << " questions.\n";

    const std::size_t item_count = std::min(questions.size(), records.size());
    for (std::size_t index = 0; index < item_count; ++index) {
        const interview::session::QuestionAnswerRecord& record = records[index];
        std::cout << index + 1 << ". Q: " << record.question << '\n';
        std::cout << "   A: " << record.candidate_answer << '\n';
        if (record.has_follow_up) {
            std::cout << "   Follow-up: " << record.follow_up_prompt << '\n';
            std::cout << "   Follow-up A: " << record.follow_up_answer << '\n';
        }
        std::cout << "   Score: " << record.final_score.score << "/100 - " << record.final_score.feedback << '\n';
    }
}

}  // namespace

int main() {
    // 初始化日志系统，方便之后观察状态变化和异常情况。
    interview::common::Logger::Init("interview.log", false);

    // 当前 MVP 直接使用固定问题列表，不接入外部题目服务。
    interview::session::DialogSession session;
    const std::vector<std::string> questions = {"Please introduce yourself in one or two sentences.",
                                                "What is one C++ concept you are currently learning?",
                                                "Which project detail would you improve next?"};
    interview::session::InterviewManager manager(questions);

    // 开场阶段：先打招呼，再把会话切回等待状态。
    std::cout << "=== AI Mock Interview CLI Demo ===\n";
    transitionState(session, interview::session::InterviewState::kInterviewerSpeaking);
    std::cout << "Interviewer: Welcome to the text-based mock interview.\n";
    transitionState(session, interview::session::InterviewState::kIdle);

    std::size_t question_number = 1;
    while (manager.hasCurrentQuestion()) {
        // 每轮先取当前题目；如果为空，说明流程状态已经不一致。
        const std::string* current_question = manager.getCurrentQuestion();
        if (current_question == nullptr) {
            transitionState(session, interview::session::InterviewState::kError);
            std::cout << "Interview stopped because no current question was found.\n";
            return 1;
        }

        // 面试官提问阶段。
        transitionState(session, interview::session::InterviewState::kInterviewerSpeaking);
        std::cout << "\nQuestion " << question_number << "/" << manager.getQuestionCount() << ": " << *current_question
                  << '\n';

        // 候选人输入阶段。
        transitionState(session, interview::session::InterviewState::kCandidateSpeaking);
        std::cout << "Your answer: ";

        std::string answer;
        if (!std::getline(std::cin, answer)) {
            transitionState(session, interview::session::InterviewState::kError);
            std::cout << "\nInput ended unexpectedly.\n";
            return 1;
        }

        // 思考阶段：先评分，再决定是否进入下一题。
        transitionState(session, interview::session::InterviewState::kInterviewerThinking);
        const interview::session::MockScoreResult score_result = manager.scoreCandidateAnswer(answer);
        std::cout << "Score: " << score_result.score << "/100 - " << score_result.feedback << '\n';
        std::string follow_up_answer;
        interview::session::MockScoreResult final_score_result = score_result;
        const interview::session::FollowUpDecision follow_up_decision = manager.decideFollowUp(score_result);

        if (follow_up_decision.needs_follow_up) {
            transitionState(session, interview::session::InterviewState::kInterviewerSpeaking);
            std::cout << "Follow-up: " << follow_up_decision.prompt << '\n';

            transitionState(session, interview::session::InterviewState::kCandidateSpeaking);
            std::cout << "Your follow-up answer: ";

            if (!std::getline(std::cin, follow_up_answer)) {
                transitionState(session, interview::session::InterviewState::kError);
                std::cout << "\nInput ended unexpectedly.\n";
                return 1;
            }

            transitionState(session, interview::session::InterviewState::kInterviewerThinking);
            final_score_result = manager.scoreCandidateAnswer(answer + " " + follow_up_answer);
            std::cout << "Updated score: " << final_score_result.score << "/100 - " << final_score_result.feedback
                      << '\n';
        }

        interview::session::QuestionAnswerRecord record;
        record.question = *current_question;
        record.candidate_answer = answer;
        record.has_follow_up = follow_up_decision.needs_follow_up;
        record.follow_up_prompt = follow_up_decision.prompt;
        record.follow_up_answer = follow_up_answer;
        record.final_score = {final_score_result.score, final_score_result.feedback};
        session.addQuestionAnswerRecord(record);

        manager.moveToNextQuestion();
        ++question_number;

        if (manager.hasCurrentQuestion()) {
            // 还有下一题时，回到空闲态等待下一轮开始。
            transitionState(session, interview::session::InterviewState::kIdle);
        }
    }

    // 所有题目结束后，进入总结和收尾阶段。
    transitionState(session, interview::session::InterviewState::kSessionEnding);
    printSummary(questions, session);
    transitionState(session, interview::session::InterviewState::kCompleted);
    std::cout << "Interview completed.\n";
    return 0;
}
