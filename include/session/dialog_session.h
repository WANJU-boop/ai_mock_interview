#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "session/interview_state.h"

namespace interview {
namespace session {

class DialogSession {
public:
    DialogSession();

    InterviewState getState() const;

    void setState(InterviewState state);

    bool isFinished() const;

    void addCandidateAnswer(const std::string& answer);

    const std::vector<std::string>& getCandidateAnswers() const;

    std::size_t getCandidateAnswerCount() const;

private:
    InterviewState current_state_ = InterviewState::kConnecting;
    std::vector<std::string> candidate_answers_;
};

}  // namespace session
}  // namespace interview
