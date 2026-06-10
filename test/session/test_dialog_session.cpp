#include <gtest/gtest.h>

#include "session/dialog_session.h"

TEST(DialogSessionTest, StartsWithConnectingState) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getState(), interview::session::InterviewState::kConnecting);
    EXPECT_FALSE(session.isFinished());
}

TEST(DialogSessionTest, CanChangeState) {
    interview::session::DialogSession session;

    session.setState(interview::session::InterviewState::kIdle);

    EXPECT_EQ(session.getState(), interview::session::InterviewState::kIdle);
    EXPECT_FALSE(session.isFinished());
}

TEST(DialogSessionTest, FinishedWhenCompletedOrError) {
    interview::session::DialogSession session;

    session.setState(interview::session::InterviewState::kCompleted);
    EXPECT_TRUE(session.isFinished());

    session.setState(interview::session::InterviewState::kError);
    EXPECT_TRUE(session.isFinished());
}

TEST(DialogSessionTest, StartsWithEmptyCandidateAnswerHistory) {
    interview::session::DialogSession session;

    EXPECT_EQ(session.getCandidateAnswerCount(), 0u);
    EXPECT_TRUE(session.getCandidateAnswers().empty());
}

TEST(DialogSessionTest, CanStoreSingleCandidateAnswer) {
    interview::session::DialogSession session;

    session.addCandidateAnswer("I built a small C++ logger.");

    ASSERT_EQ(session.getCandidateAnswerCount(), 1u);
    ASSERT_EQ(session.getCandidateAnswers().size(), 1u);
    EXPECT_EQ(session.getCandidateAnswers().front(), "I built a small C++ logger.");
}

TEST(DialogSessionTest, PreservesCandidateAnswerOrder) {
    interview::session::DialogSession session;

    session.addCandidateAnswer("First answer");
    session.addCandidateAnswer("Second answer");
    session.addCandidateAnswer("Third answer");

    ASSERT_EQ(session.getCandidateAnswerCount(), 3u);
    EXPECT_EQ(session.getCandidateAnswers()[0], "First answer");
    EXPECT_EQ(session.getCandidateAnswers()[1], "Second answer");
    EXPECT_EQ(session.getCandidateAnswers()[2], "Third answer");
}
