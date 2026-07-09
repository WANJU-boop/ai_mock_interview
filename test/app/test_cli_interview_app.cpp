#include "app/cli_interview_app.h"
#include "services/llm/mock/mock_llm_client.h"
#include "services/pdf/mock/mock_pdf_parser.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

interview::common::AppConfig makeTestConfig(int question_count) {
    interview::common::AppConfig config;
    config.interview.candidate_name = "测试候选人";
    config.interview.target_role = "C++ 实习生";
    config.interview.question_count = question_count;
    config.llm.provider = "mock";
    config.llm.model = "mock-interviewer";
    return config;
}

interview::session::PreparedInterview
prepareTestInterview(const interview::common::AppConfig& config,
                     interview::services::ILlmClient& llm_client) {
    interview::services::MockPdfParser pdf_parser;
    // CLI 流程测试不依赖真实 PDF；这里显式注入 mock，保证启动链路和生产入口一致。
    return interview::session::prepareInterview(config.interview, llm_client, pdf_parser);
}

} // namespace

// 验证高质量回答可以走完整条 CLI 主流程，并在不追问的情况下输出总结和报告。
TEST(CliInterviewAppTest, CompletesInterviewWithoutFollowUpWhenAnswerIsStrong) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input(
        "我最近做了一个 C++ 日志项目，练习 RAII、所有权、测试、调试和设计取舍。"
        "我会说明为什么用接口隔离日志输出，如何用单元测试验证边界，"
        "并复盘资源管理中的具体代码改动。\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("=== AI 模拟面试 CLI 演示 ==="), std::string::npos);
    EXPECT_NE(rendered_output.find("问题 1/1："), std::string::npos);
    EXPECT_NE(rendered_output.find("面试完成。"), std::string::npos);
    EXPECT_EQ(rendered_output.find("追问："), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 1"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"has_follow_up\": false"), std::string::npos);
}

// 验证中间分数会触发追问，并把追问内容和更新后的最终评分一起写进报告。
TEST(CliInterviewAppTest, RequestsFollowUpAndStoresUpdatedScoreInReport) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input("我在练习 C++ 类和指针，能说明基本思路。\n"
                             "在日志项目练习里，我用 RAII 和测试管理所有权，并记录调试过程。\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("追问：能不能补充一个来自项目或练习的具体例子？"),
              std::string::npos);
    EXPECT_NE(rendered_output.find("更新后得分："), std::string::npos);
    EXPECT_NE(rendered_output.find("\"has_follow_up\": true"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"follow_up_answer\": \"在日志项目练习里，我用 RAII "
                                   "和测试管理所有权，并记录调试过程。\""),
              std::string::npos);
    EXPECT_NE(rendered_output.find("回答扎实，包含具体细节。"), std::string::npos);
}

// 验证启动取题失败时，CLI 只消费 interview 层准备好的错误信息，不再直接依赖服务取题细节。
TEST(CliInterviewAppTest, ReturnsFailureWhenPreparedInterviewIsNotReady) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(0);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input("");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);

    EXPECT_EQ(exit_code, 1);
    EXPECT_EQ(output.str(), "面试启动失败：没有生成任何问题。\n");
}

// 验证主回答输入提前结束时，流程会切到错误状态并立刻返回失败。
TEST(CliInterviewAppTest, ReturnsFailureWhenPrimaryAnswerInputEndsUnexpectedly) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input("");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(rendered_output.find("[状态] 候选人回答中 -> 错误"), std::string::npos);
    EXPECT_NE(rendered_output.find("输入提前结束。"), std::string::npos);
    EXPECT_EQ(rendered_output.find("=== 面试报告 JSON ==="), std::string::npos);
    EXPECT_EQ(rendered_output.find("面试完成。"), std::string::npos);
}

// 验证追问输入提前结束时，也要走同样的错误收口，避免问答记录停在半完成状态。
TEST(CliInterviewAppTest, ReturnsFailureWhenFollowUpInputEndsUnexpectedly) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input("我在练习 C++ 类和指针，能说明基本思路。\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(rendered_output.find("追问：能不能补充一个来自项目或练习的具体例子？"),
              std::string::npos);
    EXPECT_NE(rendered_output.find("[状态] 候选人回答中 -> 错误"), std::string::npos);
    EXPECT_NE(rendered_output.find("输入提前结束。"), std::string::npos);
    EXPECT_EQ(rendered_output.find("更新后得分："), std::string::npos);
    EXPECT_EQ(rendered_output.find("=== 面试报告 JSON ==="), std::string::npos);
}

// 验证多题流程会按顺序推进，并且只有在题目之间才回到 Idle，最终报告题数也与实际一致。
TEST(CliInterviewAppTest, ProcessesMultipleQuestionsAndPrintsAccurateSummary) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(2);
    interview::session::PreparedInterview prepared_interview =
        prepareTestInterview(config, llm_client);
    std::istringstream input(
        "我做过一个 C++ 日志项目，练习所有权、调试、测试和设计取舍，"
        "并能结合具体重构例子说明为什么这样设计。\n"
        "我会通过真实代码改动、测试用例和性能取舍来解释类设计、资源管理和调试策略。\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("问题 1/2："), std::string::npos);
    EXPECT_NE(rendered_output.find("问题 2/2："), std::string::npos);
    EXPECT_NE(rendered_output.find("已回答 2 / 2 道题。"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 2"), std::string::npos);
    EXPECT_NE(rendered_output.find("[状态] 面试官思考中 -> 等待中"), std::string::npos);
    EXPECT_NE(rendered_output.find("[状态] 面试官思考中 -> 会话收尾中"), std::string::npos);
}
