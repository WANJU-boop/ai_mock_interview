#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "session/dialog_session.h"
#include "session/interview_manager.h"

TEST(InterviewManagerTest, StartsWithFirstQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Tell me about yourself", "What is C++?"});

    ASSERT_TRUE(manager.hasCurrentQuestion());
    ASSERT_NE(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*manager.getCurrentQuestion(), "Tell me about yourself");
    EXPECT_EQ(manager.getQuestionCount(), 2u);
}

TEST(InterviewManagerTest, ReturnsNoCurrentQuestionForEmptyList) {
    interview::session::InterviewManager manager(std::vector<std::string>{});

    EXPECT_FALSE(manager.hasCurrentQuestion());
    EXPECT_EQ(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(manager.getQuestionCount(), 0u);
    EXPECT_FALSE(manager.moveToNextQuestion());
}

TEST(InterviewManagerTest, MovesThroughQuestionsInOrder) {
    interview::session::InterviewManager manager(
        std::vector<std::string>{"Question one", "Question two", "Question three"});

    ASSERT_NE(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*manager.getCurrentQuestion(), "Question one");

    ASSERT_TRUE(manager.moveToNextQuestion());
    ASSERT_NE(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*manager.getCurrentQuestion(), "Question two");

    ASSERT_TRUE(manager.moveToNextQuestion());
    ASSERT_NE(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*manager.getCurrentQuestion(), "Question three");
}

TEST(InterviewManagerTest, EndsAfterLastQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    ASSERT_TRUE(manager.hasCurrentQuestion());
    EXPECT_FALSE(manager.moveToNextQuestion());
    EXPECT_FALSE(manager.hasCurrentQuestion());
    EXPECT_EQ(manager.getCurrentQuestion(), nullptr);
}

TEST(InterviewManagerTest, RecordsCandidateAnswerIntoDialogSession) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_TRUE(manager.recordCandidateAnswer(session, "I would improve the CLI flow."));
    ASSERT_EQ(session.getCandidateAnswerCount(), 1u);
    EXPECT_EQ(session.getCandidateAnswers().front(), "I would improve the CLI flow.");
}

TEST(InterviewManagerTest, DoesNotRecordAnswerWithoutActiveQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_FALSE(manager.moveToNextQuestion());
    EXPECT_FALSE(manager.recordCandidateAnswer(session, "This should fail."));
    EXPECT_TRUE(session.getCandidateAnswers().empty());
}
