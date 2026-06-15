#include "session/interview_manager.h"

#include "common/logger.h"
#include "session/dialog_session.h"

#include <utility>

namespace interview {
namespace session {

InterviewManager::InterviewManager(std::vector<std::string> questions,
                                   services::ILlmClient& llm_client)
    : llm_client_(llm_client), questions_(std::move(questions)) {
    if (questions_.empty()) {
        LOG_WARN("InterviewManager initialized with an empty question list");
        return;
    }

    LOG_DEBUG("InterviewManager initialized with {} questions", questions_.size());
}

bool InterviewManager::hasCurrentQuestion() const {
    return current_question_index_ < questions_.size();
}

const std::string* InterviewManager::getCurrentQuestion() const {
    if (!hasCurrentQuestion()) {
        LOG_WARN("No current question is available");
        return nullptr;
    }

    return &questions_[current_question_index_];
}

bool InterviewManager::recordCandidateAnswer(DialogSession& session, const std::string& answer) {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to record an answer without an active question");
        return false;
    }

    session.addCandidateAnswer(answer);
    LOG_DEBUG("Recorded candidate answer for question index {}", current_question_index_);
    return true;
}

services::LlmScoreResult InterviewManager::scoreCandidateAnswer(const std::string& answer) const {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to score an answer without an active question");
        return {0, "No active question available."};
    }

    // 评分上下文统一从当前题目读取，后续切到真实 LLM 时也不需要改主流程调用点。
    const services::LlmScoreResult result =
        llm_client_.scoreAnswer({questions_[current_question_index_], answer});
    LOG_DEBUG("Scored candidate answer for question index {} with score {}",
              current_question_index_, result.score);
    return result;
}

FollowUpDecision
InterviewManager::decideFollowUp(const services::LlmScoreResult& score_result) const {
    if (score_result.score < 70 || score_result.score >= 90) {
        LOG_DEBUG("No follow-up needed for score {}", score_result.score);
        return {false, ""};
    }

    if (score_result.feedback == "Good answer, but add one concrete example.") {
        LOG_DEBUG("Follow-up requested for score {} with example prompt", score_result.score);
        return {true, "Could you give one concrete example from your project or practice?"};
    }

    LOG_DEBUG("Follow-up requested for score {} with detail prompt", score_result.score);
    return {true, "Could you explain one specific design choice or tradeoff in more detail?"};
}

bool InterviewManager::moveToNextQuestion() {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to move past the available questions");
        return false;
    }

    ++current_question_index_;
    if (hasCurrentQuestion()) {
        // 只要还有题，返回 true，调用方就可以继续下一轮问答。
        LOG_DEBUG("Moved to question index {}", current_question_index_);
        return true;
    }

    // 下标已经越过最后一题，说明本轮面试流程已问完所有问题。
    LOG_DEBUG("InterviewManager reached the end of the question list");
    return false;
}

std::size_t InterviewManager::getQuestionCount() const {
    return questions_.size();
}

} // namespace session
} // namespace interview
