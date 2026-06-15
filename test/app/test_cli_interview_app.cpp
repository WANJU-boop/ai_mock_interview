#include "app/cli_interview_app.h"
#include "services/llm_client.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace {

interview::common::AppConfig makeTestConfig(int question_count) {
    interview::common::AppConfig config;
    config.interview.candidate_name = "Test Candidate";
    config.interview.target_role = "C++ Intern";
    config.interview.question_count = question_count;
    config.llm.provider = "mock";
    config.llm.model = "mock-interviewer";
    return config;
}

} // namespace

// 验证高质量回答可以走完整条 CLI 主流程，并在不追问的情况下输出总结和报告。
TEST(CliInterviewAppTest, CompletesInterviewWithoutFollowUpWhenAnswerIsStrong) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input(
        "I recently built a C++ logger project to practice ownership, testing, debugging, "
        "and design tradeoffs in a realistic workflow.\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("=== AI Mock Interview CLI Demo ==="), std::string::npos);
    EXPECT_NE(rendered_output.find("Question 1/1:"), std::string::npos);
    EXPECT_NE(rendered_output.find("Interview completed."), std::string::npos);
    EXPECT_EQ(rendered_output.find("Follow-up:"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 1"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"has_follow_up\": false"), std::string::npos);
}

// 验证中间分数会触发追问，并把追问内容和更新后的最终评分一起写进报告。
TEST(CliInterviewAppTest, RequestsFollowUpAndStoresUpdatedScoreInReport) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input(
        "I am learning classes and pointers, and I can explain the basic idea.\n"
        "In my logger practice, I used RAII and testing to manage ownership and debug issues.\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("Follow-up: Could you give one concrete example from your "
                                   "project or practice?"),
              std::string::npos);
    EXPECT_NE(rendered_output.find("Updated score:"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"has_follow_up\": true"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"follow_up_answer\": \"In my logger practice, I used RAII "
                                   "and testing to manage ownership and debug issues.\""),
              std::string::npos);
    EXPECT_NE(rendered_output.find("Strong answer with concrete detail."), std::string::npos);
}

// 验证启动取题失败时，CLI 只消费 interview 层准备好的错误信息，不再直接依赖服务取题细节。
TEST(CliInterviewAppTest, ReturnsFailureWhenPreparedInterviewIsNotReady) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(0);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input("");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);

    EXPECT_EQ(exit_code, 1);
    EXPECT_EQ(output.str(), "Failed to start interview because no questions were generated.\n");
}

// 验证主回答输入提前结束时，流程会切到错误状态并立刻返回失败。
TEST(CliInterviewAppTest, ReturnsFailureWhenPrimaryAnswerInputEndsUnexpectedly) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input("");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(rendered_output.find("[State] CandidateSpeaking -> Error"), std::string::npos);
    EXPECT_NE(rendered_output.find("Input ended unexpectedly."), std::string::npos);
    EXPECT_EQ(rendered_output.find("=== Interview Report JSON ==="), std::string::npos);
    EXPECT_EQ(rendered_output.find("Interview completed."), std::string::npos);
}

// 验证追问输入提前结束时，也要走同样的错误收口，避免问答记录停在半完成状态。
TEST(CliInterviewAppTest, ReturnsFailureWhenFollowUpInputEndsUnexpectedly) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(1);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input(
        "I am learning classes and pointers, and I can explain the basic idea.\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(rendered_output.find("Follow-up: Could you give one concrete example from your "
                                   "project or practice?"),
              std::string::npos);
    EXPECT_NE(rendered_output.find("[State] CandidateSpeaking -> Error"), std::string::npos);
    EXPECT_NE(rendered_output.find("Input ended unexpectedly."), std::string::npos);
    EXPECT_EQ(rendered_output.find("Updated score:"), std::string::npos);
    EXPECT_EQ(rendered_output.find("=== Interview Report JSON ==="), std::string::npos);
}

// 验证多题流程会按顺序推进，并且只有在题目之间才回到 Idle，最终报告题数也与实际一致。
TEST(CliInterviewAppTest, ProcessesMultipleQuestionsAndPrintsAccurateSummary) {
    interview::services::MockLlmClient llm_client;
    const interview::common::AppConfig config = makeTestConfig(2);
    interview::session::PreparedInterview prepared_interview =
        interview::session::prepareInterview(config.interview, llm_client);
    std::istringstream input(
        "I built a C++ logger project to practice ownership, debugging, testing, and design "
        "tradeoffs in a realistic workflow with concrete refactoring examples.\n"
        "I explain class design, resource management, and debugging strategy by walking through "
        "real code changes, test cases, and performance tradeoffs from my practice project.\n");
    std::ostringstream output;

    const int exit_code = interview::app::runCliInterview(input, output, prepared_interview);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("Question 1/2:"), std::string::npos);
    EXPECT_NE(rendered_output.find("Question 2/2:"), std::string::npos);
    EXPECT_NE(rendered_output.find("Answered 2 of 2 questions."), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 2"), std::string::npos);
    EXPECT_NE(rendered_output.find("[State] InterviewerThinking -> Idle"), std::string::npos);
    EXPECT_NE(rendered_output.find("[State] InterviewerThinking -> SessionEnding"),
              std::string::npos);
}
