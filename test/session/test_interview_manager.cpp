#include "services/llm_client.h"
#include "session/dialog_session.h"
#include "session/interview_manager.h"

#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

struct TestInterviewManagerContext {
    explicit TestInterviewManagerContext(std::vector<std::string> questions)
        : client(), manager(std::move(questions), client) {}

    interview::services::MockLlmClient client;
    interview::session::InterviewManager manager;
};

class StubLlmClient final : public interview::services::ILlmClient {
  public:
    std::vector<std::string>
    generateQuestions(const interview::services::QuestionGenerationRequest& request) override {
        return {request.target_role};
    }

    interview::services::LlmScoreResult
    scoreAnswer(const interview::services::AnswerScoringRequest& request) override {
        last_question = request.question;
        last_candidate_answer = request.candidate_answer;
        return {77, "Stubbed score result."};
    }

    std::string last_question;
    std::string last_candidate_answer;
};

} // namespace

// 管理器初始化后，第一题应该立刻可读。
TEST(InterviewManagerTest, StartsWithFirstQuestion) {
    TestInterviewManagerContext context(
        std::vector<std::string>{"Tell me about yourself", "What is C++?"});

    ASSERT_TRUE(context.manager.hasCurrentQuestion());
    ASSERT_NE(context.manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*context.manager.getCurrentQuestion(), "Tell me about yourself");
    EXPECT_EQ(context.manager.getQuestionCount(), 2u);
}

// 空题库时不应暴露当前题，也不应允许继续推进。
TEST(InterviewManagerTest, ReturnsNoCurrentQuestionForEmptyList) {
    TestInterviewManagerContext context(std::vector<std::string>{});

    EXPECT_FALSE(context.manager.hasCurrentQuestion());
    EXPECT_EQ(context.manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(context.manager.getQuestionCount(), 0u);
    EXPECT_FALSE(context.manager.moveToNextQuestion());
}

// 多道题应严格按给定顺序推进。
TEST(InterviewManagerTest, MovesThroughQuestionsInOrder) {
    TestInterviewManagerContext context(
        std::vector<std::string>{"Question one", "Question two", "Question three"});

    ASSERT_NE(context.manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*context.manager.getCurrentQuestion(), "Question one");

    ASSERT_TRUE(context.manager.moveToNextQuestion());
    ASSERT_NE(context.manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*context.manager.getCurrentQuestion(), "Question two");

    ASSERT_TRUE(context.manager.moveToNextQuestion());
    ASSERT_NE(context.manager.getCurrentQuestion(), nullptr);
    EXPECT_EQ(*context.manager.getCurrentQuestion(), "Question three");
}

// 走到最后一题后，再推进应该结束题目流。
TEST(InterviewManagerTest, EndsAfterLastQuestion) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    ASSERT_TRUE(context.manager.hasCurrentQuestion());
    EXPECT_FALSE(context.manager.moveToNextQuestion());
    EXPECT_FALSE(context.manager.hasCurrentQuestion());
    EXPECT_EQ(context.manager.getCurrentQuestion(), nullptr);
}

// 正常回答会被写入 DialogSession，供总结阶段读取。
TEST(InterviewManagerTest, RecordsCandidateAnswerIntoDialogSession) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_TRUE(context.manager.recordCandidateAnswer(session, "I would improve the CLI flow."));
    ASSERT_EQ(session.getCandidateAnswerCount(), 1u);
    EXPECT_EQ(session.getCandidateAnswers().front(), "I would improve the CLI flow.");
}

// 没有当前题时，不应再接受回答，避免流程错位。
TEST(InterviewManagerTest, DoesNotRecordAnswerWithoutActiveQuestion) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});
    interview::session::DialogSession session;

    ASSERT_FALSE(context.manager.moveToNextQuestion());
    EXPECT_FALSE(context.manager.recordCandidateAnswer(session, "This should fail."));
    EXPECT_TRUE(session.getCandidateAnswers().empty());
}

// 评分要真正走注入的 LLM 接口，而不是在管理器内部再写一套规则。
TEST(InterviewManagerTest, ScoresAnswerThroughInjectedLlmClient) {
    StubLlmClient client;
    interview::session::InterviewManager manager(
        std::vector<std::string>{"What is one C++ concept?"}, client);

    const interview::services::LlmScoreResult score_result =
        manager.scoreCandidateAnswer("I am learning RAII.");

    EXPECT_EQ(score_result.score, 77);
    EXPECT_EQ(score_result.feedback, "Stubbed score result.");
    EXPECT_EQ(client.last_question, "What is one C++ concept?");
    EXPECT_EQ(client.last_candidate_answer, "I am learning RAII.");
}

// 空回答是最重要的边界条件，应直接落到 0 分。
TEST(InterviewManagerTest, ScoresEmptyAnswerAsZero) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::services::LlmScoreResult score_result =
        context.manager.scoreCandidateAnswer("");

    EXPECT_EQ(score_result.score, 0);
    EXPECT_EQ(score_result.feedback, "No answer provided.");
}

// 如果当前已经没有题目，评分函数应返回明确错误，而不是继续调用服务层。
TEST(InterviewManagerTest, ReturnsExplicitFailureWhenScoringWithoutActiveQuestion) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    ASSERT_FALSE(context.manager.moveToNextQuestion());
    const interview::services::LlmScoreResult score_result =
        context.manager.scoreCandidateAnswer("This answer should not be scored.");

    EXPECT_EQ(score_result.score, 0);
    EXPECT_EQ(score_result.feedback, "No active question available.");
}

// 很短的回答应明显低于包含技术细节的完整回答。
TEST(InterviewManagerTest, ScoresShortAnswerLowerThanDetailedAnswer) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::services::LlmScoreResult short_result =
        context.manager.scoreCandidateAnswer("C++ basics.");
    const interview::services::LlmScoreResult detailed_result =
        context.manager.scoreCandidateAnswer("I am learning C++ memory management by building a "
                                             "small logger project, because it helps me understand "
                                             "ownership, debugging, and testing in real code.");

    EXPECT_LT(short_result.score, detailed_result.score);
    EXPECT_EQ(short_result.feedback, "Answer is too short. Add more detail.");
    EXPECT_GE(detailed_result.score, 90);
    EXPECT_EQ(detailed_result.feedback, "Strong answer with concrete detail.");
}

// 中等质量回答应落在“还不错，但最好补例子”的中间档位。
TEST(InterviewManagerTest, ScoresMediumAnswerAsNeedsMoreExample) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::services::LlmScoreResult score_result = context.manager.scoreCandidateAnswer(
        "I am learning classes and pointers, and I can explain the basic idea.");

    EXPECT_GE(score_result.score, 65);
    EXPECT_LT(score_result.score, 85);
    EXPECT_EQ(score_result.feedback, "Good answer, but add one concrete example.");
}

// 中间分数段需要触发追问，帮助候选人补充例子或细节。
TEST(InterviewManagerTest, RequestsFollowUpForScoresBetweenSeventyAndEightyNine) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({75, "Good answer, but add one concrete example."});

    EXPECT_TRUE(follow_up.needs_follow_up);
    EXPECT_EQ(follow_up.prompt,
              "Could you give one concrete example from your project or practice?");
}

// 70 分是追问区间的下边界，需要锁住边界行为，防止条件改动导致回归。
TEST(InterviewManagerTest, RequestsFollowUpAtSeventy) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({70, "Good answer, but add one concrete example."});

    EXPECT_TRUE(follow_up.needs_follow_up);
}

// 89 分仍属于追问区间；只有到 90 分及以上才直接进入下一题。
TEST(InterviewManagerTest, RequestsFollowUpAtEightyNine) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({89, "Good answer, but add one concrete example."});

    EXPECT_TRUE(follow_up.needs_follow_up);
}

// 追问不只用来要例子；当反馈不是 example 文案时，应走“补设计细节”分支。
TEST(InterviewManagerTest, RequestsDetailFollowUpWhenFeedbackIsNotExamplePrompt) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({80, "Basic answer, but it needs more detail."});

    EXPECT_TRUE(follow_up.needs_follow_up);
    EXPECT_EQ(follow_up.prompt,
              "Could you explain one specific design choice or tradeoff in more detail?");
}

// 高分回答直接进入下一题，不应再追加追问。
TEST(InterviewManagerTest, DoesNotRequestFollowUpForNinetyOrAbove) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({90, "Strong answer with concrete detail."});

    EXPECT_FALSE(follow_up.needs_follow_up);
    EXPECT_TRUE(follow_up.prompt.empty());
}

// 低于追问阈值时直接进入下一题，避免让很弱的回答陷入冗长追问。
TEST(InterviewManagerTest, DoesNotRequestFollowUpBelowSeventy) {
    TestInterviewManagerContext context(std::vector<std::string>{"Only question"});

    const interview::session::FollowUpDecision follow_up =
        context.manager.decideFollowUp({69, "Basic answer, but it needs more detail."});

    EXPECT_FALSE(follow_up.needs_follow_up);
    EXPECT_TRUE(follow_up.prompt.empty());
}
