#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "common/logger.h"
#include "session/dialog_session.h"
#include "session/interview_manager.h"
#include "session/interview_state.h"

namespace {

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

void transitionState(interview::session::DialogSession& session, interview::session::InterviewState next_state) {
    const interview::session::InterviewState current_state = session.getState();
    session.setState(next_state);
    std::cout << "[State] " << interviewStateToString(current_state) << " -> " << interviewStateToString(next_state)
              << '\n';
}

void printSummary(const std::vector<std::string>& questions, const interview::session::DialogSession& session) {
    const std::vector<std::string>& answers = session.getCandidateAnswers();

    std::cout << "\n=== Interview Summary ===\n";
    std::cout << "Answered " << answers.size() << " of " << questions.size() << " questions.\n";

    const std::size_t item_count = std::min(questions.size(), answers.size());
    for (std::size_t index = 0; index < item_count; ++index) {
        std::cout << index + 1 << ". Q: " << questions[index] << '\n';
        std::cout << "   A: " << answers[index] << '\n';
    }
}

}  // namespace

int main() {
    interview::common::Logger::Init("interview.log", false);

    interview::session::DialogSession session;
    const std::vector<std::string> questions = {"Please introduce yourself in one or two sentences.",
                                                "What is one C++ concept you are currently learning?",
                                                "Which project detail would you improve next?"};
    interview::session::InterviewManager manager(questions);

    std::cout << "=== AI Mock Interview CLI Demo ===\n";
    transitionState(session, interview::session::InterviewState::kInterviewerSpeaking);
    std::cout << "Interviewer: Welcome to the text-based mock interview.\n";
    transitionState(session, interview::session::InterviewState::kIdle);

    std::size_t question_number = 1;
    while (manager.hasCurrentQuestion()) {
        const std::string* current_question = manager.getCurrentQuestion();
        if (current_question == nullptr) {
            transitionState(session, interview::session::InterviewState::kError);
            std::cout << "Interview stopped because no current question was found.\n";
            return 1;
        }

        transitionState(session, interview::session::InterviewState::kInterviewerSpeaking);
        std::cout << "\nQuestion " << question_number << "/" << manager.getQuestionCount() << ": " << *current_question
                  << '\n';

        transitionState(session, interview::session::InterviewState::kCandidateSpeaking);
        std::cout << "Your answer: ";

        std::string answer;
        if (!std::getline(std::cin, answer)) {
            transitionState(session, interview::session::InterviewState::kError);
            std::cout << "\nInput ended unexpectedly.\n";
            return 1;
        }

        if (!manager.recordCandidateAnswer(session, answer)) {
            transitionState(session, interview::session::InterviewState::kError);
            std::cout << "Failed to save the answer.\n";
            return 1;
        }

        transitionState(session, interview::session::InterviewState::kInterviewerThinking);
        manager.moveToNextQuestion();
        ++question_number;

        if (manager.hasCurrentQuestion()) {
            transitionState(session, interview::session::InterviewState::kIdle);
        }
    }

    transitionState(session, interview::session::InterviewState::kSessionEnding);
    printSummary(questions, session);
    transitionState(session, interview::session::InterviewState::kCompleted);
    std::cout << "Interview completed.\n";
    return 0;
}
