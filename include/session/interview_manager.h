#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace interview {
namespace session {

class DialogSession;

class InterviewManager {
public:
    explicit InterviewManager(std::vector<std::string> questions);

    bool hasCurrentQuestion() const;

    const std::string* getCurrentQuestion() const;

    bool recordCandidateAnswer(DialogSession& session, const std::string& answer);

    bool moveToNextQuestion();

    std::size_t getQuestionCount() const;

private:
    std::vector<std::string> questions_;
    std::size_t current_question_index_ = 0;
};

}  // namespace session
}  // namespace interview
