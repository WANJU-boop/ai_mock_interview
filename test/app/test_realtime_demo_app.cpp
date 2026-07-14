#include "app/realtime_demo_app.h"
#include "services/llm/mock/mock_llm_client.h"
#include "services/pdf/mock/mock_pdf_parser.h"
#include "services/realtime/mock/mock_realtime_client.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

namespace {

interview::common::InterviewConfig makeConfig(int question_count) {
    interview::common::InterviewConfig config;
    config.candidate_name = "测试候选人";
    config.target_role = "C++ 实习生";
    config.question_count = question_count;
    return config;
}

interview::session::PreparedInterview
prepareDemoInterview(const interview::common::InterviewConfig& config,
                     interview::services::ILlmClient& llm_client) {
    interview::services::MockPdfParser pdf_parser;
    return interview::session::prepareInterview(config, llm_client, pdf_parser);
}

interview::common::RealtimeEvent makeEvent(interview::common::RealtimeEventType type,
                                           const std::string& text = "") {
    interview::common::RealtimeEvent event;
    event.type = type;
    event.text = text;
    return event;
}

} // namespace

// 验证 demo app 能用默认脚本跑完单题 realtime mock 流程，并输出报告 JSON。
TEST(RealtimeDemoAppTest, CompletesSingleQuestionAndPrintsReport) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareDemoInterview(makeConfig(1), llm_client);
    std::ostringstream output;

    const int exit_code = interview::app::runRealtimeDemoInterview(
        output, prepared_interview, interview::app::buildDefaultRealtimeDemoScript(1));
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("=== Realtime mock Demo ==="), std::string::npos);
    EXPECT_NE(rendered_output.find("问题 1/1："), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 1"), std::string::npos);
    EXPECT_NE(rendered_output.find("Realtime mock 面试完成。"), std::string::npos);
}

// 验证默认脚本能按题数生成多条 final transcript，确保 demo 可用于多题 smoke test。
TEST(RealtimeDemoAppTest, ProcessesMultipleQuestionsWithDefaultScript) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareDemoInterview(makeConfig(2), llm_client);
    std::ostringstream output;

    const int exit_code = interview::app::runRealtimeDemoInterview(
        output, prepared_interview, interview::app::buildDefaultRealtimeDemoScript(2));
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("问题 1/2："), std::string::npos);
    EXPECT_NE(rendered_output.find("面试官：问题 2/2："), std::string::npos);
    EXPECT_NE(rendered_output.find("\"question_count\": 2"), std::string::npos);
}

// 验证 demo app 不是只能走高分路径；中等回答会触发追问并写入报告。
TEST(RealtimeDemoAppTest, RequestsFollowUpAndStoresUpdatedScore) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareDemoInterview(makeConfig(1), llm_client);
    const std::vector<interview::common::RealtimeEvent> script = {
        makeEvent(interview::common::RealtimeEventType::kConnected),
        makeEvent(interview::common::RealtimeEventType::kTranscriptFinal,
                  "我在练习 C++ 类和指针，能说明基本思路。"),
        makeEvent(interview::common::RealtimeEventType::kTranscriptFinal,
                  "在日志项目练习里，我用 RAII 和测试管理所有权，并记录调试过程。")};
    std::ostringstream output;

    const int exit_code =
        interview::app::runRealtimeDemoInterview(output, prepared_interview, script);
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("面试官：能不能补充一个来自项目或练习的具体例子？"),
              std::string::npos);
    EXPECT_NE(rendered_output.find("\"has_follow_up\": true"), std::string::npos);
    EXPECT_NE(rendered_output.find("\"follow_up_answer\": \"在日志项目练习里，我用 RAII "
                                   "和测试管理所有权，并记录调试过程。\""),
              std::string::npos);
}

// 验证准备面试失败时，demo app 直接复用 interview 层错误，不启动 mock realtime。
TEST(RealtimeDemoAppTest, ReturnsFailureWhenPreparedInterviewIsNotReady) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareDemoInterview(makeConfig(0), llm_client);
    std::ostringstream output;

    const int exit_code = interview::app::runRealtimeDemoInterview(output, prepared_interview, {});

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(output.str().find("面试启动失败：没有生成任何问题。"), std::string::npos);
}

// 验证脚本过短会明确失败，避免下一步接 WebSocket 时事件流中断后静默生成错误报告。
TEST(RealtimeDemoAppTest, ReturnsFailureWhenScriptEndsBeforeCompletion) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareDemoInterview(makeConfig(1), llm_client);
    std::ostringstream output;

    const int exit_code = interview::app::runRealtimeDemoInterview(
        output, prepared_interview, {makeEvent(interview::common::RealtimeEventType::kConnected)});
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 1);
    EXPECT_NE(rendered_output.find("Realtime demo 失败：realtime 事件流结束，但面试尚未完成。"),
              std::string::npos);
    EXPECT_EQ(rendered_output.find("=== Realtime Mock 报告 JSON ==="), std::string::npos);
}

// 验证真实 provider 的连接 smoke test 只检查 connect/send/close，不进入需要麦克风 transcript
// 的面试循环。
TEST(RealtimeDemoAppTest, RunsConnectionSmokeWithoutInterviewLoop) {
    interview::services::MockRealtimeClient realtime_client({});
    std::ostringstream output;

    const int exit_code =
        interview::app::runRealtimeConnectionSmoke(output, realtime_client, "mock");
    const std::string rendered_output = output.str();

    EXPECT_EQ(exit_code, 0);
    EXPECT_NE(rendered_output.find("=== Realtime mock Connection Smoke ==="), std::string::npos);
    EXPECT_NE(rendered_output.find("连接 smoke test 完成"), std::string::npos);
    EXPECT_TRUE(realtime_client.isClosed());
}
