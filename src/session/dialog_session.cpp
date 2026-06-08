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
  return current_state_ == InterviewState::kCompleted ||
         current_state_ == InterviewState::kError;
}

}  // namespace session
}  // namespace interview
