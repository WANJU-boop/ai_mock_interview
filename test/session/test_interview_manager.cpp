#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "session/dialog_session.h"
#include "session/interview_manager.h"

// 管理器初始化后，第一题应该立刻可读。
TEST(InterviewManagerTest, StartsWithFirstQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Tell me about yourself", "What is C++?"});

    ASSERT_TRUE(manager.hasCurrentQuestion());
    ASSERT_NE(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*manager.getCurrentQuestion(), "Tell me about yourself");
    EXPECT_EQ(manager.getQuestionCount(), 2u);
}

// 空题库时不应暴露当前题，也不应允许继续推进。
TEST(InterviewManagerTest, ReturnsNoCurrentQuestionForEmptyList) {
    interview::session::InterviewManager manager(std::vector<std::string>{});

    EXPECT_FALSE(manager.hasCurrentQuestion());
    EXPECT_EQ(manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(manager.getQuestionCount(), 0u);
    EXPECT_FALSE(manager.moveToNextQuestion());
}

// 多道题应严格按给定顺序推进。
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

// 走到最后一题后，再推进应该结束题目流。
TEST(InterviewManagerTest, EndsAfterLastQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    ASSERT_TRUE(manager.hasCurrentQuestion());
    EXPECT_FALSE(manager.moveToNextQuestion());
    EXPECT_FALSE(manager.hasCurrentQuestion());
    EXPECT_EQ(manager.getCurrentQuestion(), nullptr);
}

// 正常回答会被写入 DialogSession，供总结阶段读取。
TEST(InterviewManagerTest, RecordsCandidateAnswerIntoDialogSession) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_TRUE(manager.recordCandidateAnswer(session, "I would improve the CLI flow."));
    ASSERT_EQ(session.getCandidateAnswerCount(), 1u);
    EXPECT_EQ(session.getCandidateAnswers().front(), "I would improve the CLI flow.");
}

// 没有当前题时，不应再接受回答，避免流程错位。
TEST(InterviewManagerTest, DoesNotRecordAnswerWithoutActiveQuestion) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_FALSE(manager.moveToNextQuestion());
    EXPECT_FALSE(manager.recordCandidateAnswer(session, "This should fail."));
    EXPECT_TRUE(session.getCandidateAnswers().empty());
}

// 空回答是最重要的边界条件，应直接落到 0 分。
TEST(InterviewManagerTest, ScoresEmptyAnswerAsZero) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::MockScoreResult score_result = manager.scoreCandidateAnswer("");

    EXPECT_EQ(score_result.score, 0);
    EXPECT_EQ(score_result.feedback, "No answer provided.");
}

// 很短的回答应明显低于包含技术细节的完整回答。
TEST(InterviewManagerTest, ScoresShortAnswerLowerThanDetailedAnswer) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::MockScoreResult short_result = manager.scoreCandidateAnswer("C++ basics.");
    const interview::session::MockScoreResult detailed_result = manager.scoreCandidateAnswer(
        "I am learning C++ memory management by building a small logger project, because it helps me understand "
        "ownership, debugging, and testing in real code.");

    EXPECT_LT(short_result.score, detailed_result.score);
    EXPECT_EQ(short_result.feedback, "Answer is too short. Add more detail.");
    EXPECT_GE(detailed_result.score, 90);
    EXPECT_EQ(detailed_result.feedback, "Strong answer with concrete detail.");
}

// 中等质量回答应落在“还不错，但最好补例子”的中间档位。
TEST(InterviewManagerTest, ScoresMediumAnswerAsNeedsMoreExample) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::MockScoreResult score_result =
        manager.scoreCandidateAnswer("I am learning classes and pointers, and I can explain the basic idea.");

    EXPECT_GE(score_result.score, 65);
    EXPECT_LT(score_result.score, 85);
    EXPECT_EQ(score_result.feedback, "Good answer, but add one concrete example.");
}

// 中间分数段需要触发追问，帮助候选人补充例子或细节。
TEST(InterviewManagerTest, RequestsFollowUpForScoresBetweenSeventyAndEightyNine) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        manager.decideFollowUp({75, "Good answer, but add one concrete example."});

    EXPECT_TRUE(follow_up.needs_follow_up);
    EXPECT_EQ(follow_up.prompt, "Could you give one concrete example from your project or practice?");
}

// 高分回答直接进入下一题，不应再追加追问。
TEST(InterviewManagerTest, DoesNotRequestFollowUpForNinetyOrAbove) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        manager.decideFollowUp({90, "Strong answer with concrete detail."});

    EXPECT_FALSE(follow_up.needs_follow_up);
    EXPECT_TRUE(follow_up.prompt.empty());
}

// 低于追问阈值时直接进入下一题，避免让很弱的回答陷入冗长追问。
TEST(InterviewManagerTest, DoesNotRequestFollowUpBelowSeventy) {
    interview::session::InterviewManager manager(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        manager.decideFollowUp({69, "Basic answer, but it needs more detail."});

    EXPECT_FALSE(follow_up.needs_follow_up);
    EXPECT_TRUE(follow_up.prompt.empty());
}
