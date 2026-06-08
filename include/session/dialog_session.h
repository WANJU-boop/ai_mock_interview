#pragma once

#include "session/interview_state.h"

namespace interview {
namespace session {

class DialogSession {
public:
  DialogSession();

  InterviewState getState() const;

  void setState(InterviewState state);

  bool isFinished() const;

private:
  InterviewState current_state_ = InterviewState::kConnecting;
};

}  // namespace session
}  // namespace interview
