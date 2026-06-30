// clang-format off
#include "services/llm_client.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
// clang-format on

// 通过接口指针调用 mock，验证当前阶段真正建立了可替换的 LLM 抽象边界。
TEST(MockLlmClientTest, CanCallMockThroughLlmClientInterface) {
    const std::unique_ptr<interview::services::ILlmClient> client =
        std::make_unique<interview::services::MockLlmClient>();

    const std::vector<std::string> questions =
        client->generateQuestions({"Demo Candidate", "C++ Intern", 1});

    ASSERT_EQ(questions.size(), 1u);
    EXPECT_FALSE(questions.front().empty());
}

// Mock 必须是确定性的，这样单元测试不会被随机数、时间或外部服务影响。
TEST(MockLlmClientTest, GeneratesQuestionsDeterministically) {
    interview::services::MockLlmClient client;
    // 同一个 request 连续调用两次，应该得到完全相同的问题列表。
    const interview::services::QuestionGenerationRequest request{"Demo Candidate", "C++ Intern", 3};

    const std::vector<std::string> first_questions = client.generateQuestions(request);
    const std::vector<std::string> second_questions = client.generateQuestions(request);

    EXPECT_EQ(first_questions, second_questions);
}

// Mock 客户端必须按请求数量返回问题，后续领域层才能稳定用它替换硬编码题库。
TEST(MockLlmClientTest, GeneratesRequestedQuestionCount) {
    interview::services::MockLlmClient client;

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 3});

    ASSERT_EQ(questions.size(), 3u);
    EXPECT_FALSE(questions[0].empty());
    EXPECT_FALSE(questions[1].empty());
    EXPECT_FALSE(questions[2].empty());
}

// 题目里带上目标岗位，能验证 request 数据确实进入了 mock 生成逻辑。
TEST(MockLlmClientTest, IncludesTargetRoleInGeneratedQuestions) {
    interview::services::MockLlmClient client;

    // 使用不常见岗位名，避免测试误判为模板里碰巧出现的普通词。
    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "Backend C++ Engineer", 2});

    ASSERT_EQ(questions.size(), 2u);
    EXPECT_NE(questions[0].find("Backend C++ Engineer"), std::string::npos);
    EXPECT_NE(questions[1].find("Backend C++ Engineer"), std::string::npos);
}

// 目标岗位为空时仍要返回稳定题目，避免配置里缺少岗位名称时主流程直接中断。
TEST(MockLlmClientTest, UsesFallbackRoleWhenTargetRoleEmpty) {
    interview::services::MockLlmClient client;

    const std::vector<std::string> questions = client.generateQuestions({"Demo Candidate", "", 1});

    ASSERT_EQ(questions.size(), 1u);
    EXPECT_NE(questions.front().find("C++ 学习者"), std::string::npos);
}

// 请求数量超过模板题库时要追加序号，避免 mock 生成一组完全重复的问题。
TEST(MockLlmClientTest, AppendsOrdinalWhenRequestedCountExceedsTemplateBank) {
    interview::services::MockLlmClient client;

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 6});

    ASSERT_EQ(questions.size(), 6u);
    EXPECT_NE(questions.back().find("#6"), std::string::npos);
}

// 非法题目数量直接返回空列表，避免调用方拿到看似有效的默认问题。
TEST(MockLlmClientTest, ReturnsEmptyQuestionsForNonPositiveCount) {
    interview::services::MockLlmClient client;

    EXPECT_TRUE(client.generateQuestions({"Demo Candidate", "C++ Intern", 0}).empty());
    EXPECT_TRUE(client.generateQuestions({"Demo Candidate", "C++ Intern", -2}).empty());
}

// 空回答是评分接口最重要的边界，必须稳定返回 0 分。
TEST(MockLlmClientTest, ScoresEmptyAnswerAsZero) {
    interview::services::MockLlmClient client;

    const interview::services::LlmScoreResult result = client.scoreAnswer({"What is C++?", ""});

    EXPECT_EQ(result.score, 0);
    EXPECT_EQ(result.feedback, "未提供回答。");
}

// 详细回答应该显著高于短回答，验证 mock 评分能支持后续流程测试。
TEST(MockLlmClientTest, ScoresDetailedAnswerHigherThanShortAnswer) {
    interview::services::MockLlmClient client;

    // 短回答和详细回答共用同一个问题，确保分差来自回答质量而不是题目变化。
    const interview::services::LlmScoreResult short_result =
        client.scoreAnswer({"What is C++?", "C++ basics."});
    const interview::services::LlmScoreResult detailed_result =
        client.scoreAnswer({"What is C++?", "I am learning C++ memory management by building a "
                                            "small logger project, because it helps me understand "
                                            "ownership, debugging, and testing in real code."});

    EXPECT_LT(short_result.score, detailed_result.score);
    EXPECT_EQ(short_result.feedback, "回答太短，建议补充更多细节。");
    EXPECT_GE(detailed_result.score, 90);
}

// 中文回答通常不靠空格分词，这个用例锁定“中文长回答也能拿到合理高分”的运行体验。
TEST(MockLlmClientTest, ScoresChineseDetailedAnswerWithoutSpaces) {
    interview::services::MockLlmClient client;

    const interview::services::LlmScoreResult result =
        client.scoreAnswer({"请说明一个 C++ 项目经验。",
                            "我最近做了一个C++日志项目，练习RAII、所有权、测试、调试和设计取舍，"
                            "也会结合具体代码说明资源管理和接口隔离。"});

    EXPECT_GE(result.score, 90);
    EXPECT_LE(result.score, 100);
    EXPECT_EQ(result.feedback, "回答扎实，包含具体细节。");
}

// 即使回答很长并命中多个关键词，mock 评分也必须保持在 0 到 100 的稳定区间内。
TEST(MockLlmClientTest, KeepsDetailedAnswerScoreWithinRange) {
    interview::services::MockLlmClient client;

    const interview::services::LlmScoreResult result = client.scoreAnswer(
        {"Describe a project.", "My C++ project uses class design, memory ownership, pointer "
                                "safety, template utilities, logger tracing, "
                                "testing coverage, debugging notes, and performance checks to "
                                "explain concrete tradeoffs in a realistic "
                                "interview answer."});

    EXPECT_GE(result.score, 90);
    EXPECT_LE(result.score, 100);
    EXPECT_EQ(result.feedback, "回答扎实，包含具体细节。");
}

// 关键词判断不应受大小写影响，否则候选人的自然输入会导致评分不稳定。
TEST(MockLlmClientTest, ScoresTechnicalKeywordCaseInsensitively) {
    interview::services::MockLlmClient client;

    // 故意使用大写关键词，锁定大小写归一化逻辑，避免后续重构破坏自然输入体验。
    const interview::services::LlmScoreResult result = client.scoreAnswer(
        {"What are you learning?",
         "I use CLASS examples and POINTER practice to explain ownership in my current project."});

    EXPECT_GE(result.score, 65);
    EXPECT_EQ(result.feedback, "回答不错，但建议补充一个具体例子。");
}
