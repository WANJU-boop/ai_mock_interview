#include "services/llm/mock/mock_llm_client.h"
#include "services/pdf/mock/mock_pdf_parser.h"
#include "services/realtime/mock/mock_realtime_client.h"
#include "session/dialog_orchestrator.h"
#include "session/interview_setup.h"

#include <gtest/gtest.h>
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
prepareInterview(const interview::common::InterviewConfig& config,
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

interview::common::RealtimeEvent makeErrorEvent(const std::string& message) {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kError;
    event.error_message = message;
    return event;
}

std::string strongAnswer() {
    return "我最近做了一个 C++ 日志项目，练习 RAII、所有权、测试、调试和设计取舍。"
           "我会说明为什么用接口隔离日志输出，如何用单元测试验证边界，"
           "并复盘资源管理中的具体代码改动。";
}

} // namespace

// 验证单题 strong answer 可以从 connected 事件一路跑到 completed，并写入完整问答记录。
TEST(DialogOrchestratorTest, CompletesSingleQuestionWithoutFollowUp) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(1), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, strongAnswer())});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.error_message.empty());
    EXPECT_EQ(result.session.getState(), interview::session::InterviewState::kCompleted);
    ASSERT_EQ(result.session.getQuestionAnswerRecordCount(), 1u);
    EXPECT_FALSE(result.session.getQuestionAnswerRecords().front().has_follow_up);
    EXPECT_EQ(result.session.getQuestionAnswerRecords().front().candidate_answer, strongAnswer());
    ASSERT_GE(result.interviewer_messages.size(), 3u);
    EXPECT_NE(result.interviewer_messages[0].find("欢迎你，测试候选人"), std::string::npos);
    EXPECT_NE(result.interviewer_messages[1].find("问题 1/1："), std::string::npos);
    EXPECT_TRUE(realtime_client.isClosed());
}

// 验证中间分数会触发追问，并把追问回答和更新后的最终评分写入同一条记录。
TEST(DialogOrchestratorTest, RequestsFollowUpAndStoresUpdatedFinalScore) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(1), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal,
                   "我在练习 C++ 类和指针，能说明基本思路。"),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal,
                   "在日志项目练习里，我用 RAII 和测试管理所有权，并记录调试过程。")});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.session.getQuestionAnswerRecordCount(), 1u);
    const interview::session::QuestionAnswerRecord& record =
        result.session.getQuestionAnswerRecords().front();
    EXPECT_TRUE(record.has_follow_up);
    EXPECT_EQ(record.follow_up_prompt, "能不能补充一个来自项目或练习的具体例子？");
    EXPECT_EQ(record.follow_up_answer,
              "在日志项目练习里，我用 RAII 和测试管理所有权，并记录调试过程。");
    EXPECT_GE(record.final_score.score, 85);
    EXPECT_NE(result.interviewer_messages[2].find("能不能补充一个来自项目或练习的具体例子？"),
              std::string::npos);
}

// 验证多题脚本会按顺序推进，并且每道题只生成一条结构化问答记录。
TEST(DialogOrchestratorTest, ProcessesMultipleQuestionsInOrder) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(2), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, strongAnswer()),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, strongAnswer())});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.session.getQuestionAnswerRecordCount(), 2u);
    ASSERT_GE(result.interviewer_messages.size(), 4u);
    EXPECT_NE(result.interviewer_messages[1].find("问题 1/2："), std::string::npos);
    EXPECT_NE(result.interviewer_messages[2].find("问题 2/2："), std::string::npos);
}

// 验证 partial transcript 只用于实时展示，不会提前记录回答或推进评分。
TEST(DialogOrchestratorTest, DoesNotAdvanceQuestionOnPartialTranscript) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(1), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeEvent(interview::common::RealtimeEventType::kTranscriptPartial, "我还在组织语言"),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, strongAnswer())});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.partial_transcripts.size(), 1u);
    EXPECT_EQ(result.partial_transcripts.front(), "我还在组织语言");
    ASSERT_EQ(result.session.getQuestionAnswerRecordCount(), 1u);
    EXPECT_EQ(result.session.getQuestionAnswerRecords().front().candidate_answer, strongAnswer());
    EXPECT_EQ(result.session.getCandidateAnswerCount(), 1u);
}

// 验证 realtime 错误事件会让流程进入错误态并关闭客户端，不再生成半成品报告记录。
TEST(DialogOrchestratorTest, StopsWithErrorWhenRealtimeErrorArrives) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(1), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeErrorEvent("服务端鉴权失败")});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "服务端鉴权失败");
    EXPECT_EQ(result.session.getState(), interview::session::InterviewState::kError);
    EXPECT_EQ(result.session.getQuestionAnswerRecordCount(), 0u);
    EXPECT_TRUE(realtime_client.isClosed());
}

// 验证事件流提前耗尽时会明确失败，避免等待真实 WebSocket 时出现无声卡住。
TEST(DialogOrchestratorTest, FailsWhenEventStreamEndsBeforeCompletion) {
    interview::services::MockLlmClient llm_client;
    interview::session::PreparedInterview prepared_interview =
        prepareInterview(makeConfig(1), llm_client);
    interview::services::MockRealtimeClient realtime_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected)});

    interview::session::DialogOrchestrator orchestrator(prepared_interview, realtime_client);
    const interview::session::DialogOrchestratorResult result = orchestrator.run();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error_message, "realtime 事件流结束，但面试尚未完成。");
    EXPECT_EQ(result.session.getState(), interview::session::InterviewState::kError);
    EXPECT_TRUE(realtime_client.isClosed());
}
