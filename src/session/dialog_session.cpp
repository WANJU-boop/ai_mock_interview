#include "session/dialog_session.h"

namespace interview {
namespace session {

DialogSession::DialogSession() = default;

InterviewState DialogSession::getState() const {
    return current_state_;
}

void DialogSession::setState(InterviewState state) {
    current_state_ = state;
}

bool DialogSession::isFinished() const {
    return current_state_ == InterviewState::kCompleted || current_state_ == InterviewState::kError;
}

void DialogSession::addCandidateAnswer(const std::string& answer) {
    candidate_answers_.push_back(answer);
}

const std::vector<std::string>& DialogSession::getCandidateAnswers() const {
    return candidate_answers_;
}

std::size_t DialogSession::getCandidateAnswerCount() const {
    return candidate_answers_.size();
}

}  // namespace session
}  // namespace interview
