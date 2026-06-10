#include "session/interview_manager.h"

#include <utility>

#include "common/logger.h"
#include "session/dialog_session.h"

namespace interview {
namespace session {

InterviewManager::InterviewManager(std::vector<std::string> questions) : questions_(std::move(questions)) {
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

bool InterviewManager::moveToNextQuestion() {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to move past the available questions");
        return false;
    }

    ++current_question_index_;
    if (hasCurrentQuestion()) {
        LOG_DEBUG("Moved to question index {}", current_question_index_);
        return true;
    }

    LOG_DEBUG("InterviewManager reached the end of the question list");
    return false;
}

std::size_t InterviewManager::getQuestionCount() const {
    return questions_.size();
}

}  // namespace session
}  // namespace interview
