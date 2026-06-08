#pragma once

namespace interview {
namespace session {

enum class InterviewState {
  kConnecting,
  kInterviewerSpeaking,
  kIdle,
  kCandidateSpeaking,
  kInterviewerThinking,
  kSessionEnding,
  kCompleted,
  kError,
};

}  // namespace session
}  // namespace interview
