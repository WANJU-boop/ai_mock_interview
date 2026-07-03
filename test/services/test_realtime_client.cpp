#include "services/realtime_client.h"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

interview::common::RealtimeEvent makeEvent(interview::common::RealtimeEventType type,
                                           const std::string& text = "") {
    interview::common::RealtimeEvent event;
    event.type = type;
    event.text = text;
    return event;
}

} // namespace

// 验证 mock 可以通过 IRealtimeClient 接口使用，后续真实 WebSocket 实现能替换同一入口。
TEST(MockRealtimeClientTest, CanCallMockThroughRealtimeClientInterface) {
    interview::services::MockRealtimeClient mock_client(
        {makeEvent(interview::common::RealtimeEventType::kConnected)});
    interview::services::IRealtimeClient& client = mock_client;

    EXPECT_TRUE(client.connect());
    EXPECT_TRUE(client.hasNextEvent());
    EXPECT_EQ(client.receiveNextEvent().type, interview::common::RealtimeEventType::kConnected);
}

// 验证脚本事件按顺序发出，保证对话编排器测试是确定性的。
TEST(MockRealtimeClientTest, EmitsScriptedEventsInOrder) {
    interview::services::MockRealtimeClient client(
        {makeEvent(interview::common::RealtimeEventType::kConnected),
         makeEvent(interview::common::RealtimeEventType::kTranscriptPartial, "部分回答"),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, "最终回答")});

    ASSERT_TRUE(client.connect());

    EXPECT_EQ(client.receiveNextEvent().type, interview::common::RealtimeEventType::kConnected);
    EXPECT_EQ(client.receiveNextEvent().text, "部分回答");
    EXPECT_EQ(client.receiveNextEvent().text, "最终回答");
    EXPECT_FALSE(client.hasNextEvent());
}

// 验证面试官输出会被记录，后续测试可确认提问和追问是否真的发给 realtime 层。
TEST(MockRealtimeClientTest, CapturesOutboundInterviewerMessages) {
    interview::services::MockRealtimeClient client({});

    ASSERT_TRUE(client.connect());
    ASSERT_TRUE(client.sendInterviewerText("欢迎进入模拟面试。"));
    ASSERT_TRUE(client.sendInterviewerText("请回答第一题。"));

    ASSERT_EQ(client.getOutboundInterviewerMessages().size(), 2u);
    EXPECT_EQ(client.getOutboundInterviewerMessages()[0], "欢迎进入模拟面试。");
    EXPECT_EQ(client.getOutboundInterviewerMessages()[1], "请回答第一题。");
}

// 验证关闭事件会停止读取，避免真实连接关闭后主流程继续消费过期事件。
TEST(MockRealtimeClientTest, StopsAfterCloseEvent) {
    interview::services::MockRealtimeClient client(
        {makeEvent(interview::common::RealtimeEventType::kClosed),
         makeEvent(interview::common::RealtimeEventType::kTranscriptFinal, "不应读取")});

    ASSERT_TRUE(client.connect());
    EXPECT_EQ(client.receiveNextEvent().type, interview::common::RealtimeEventType::kClosed);
    EXPECT_TRUE(client.isClosed());
    EXPECT_FALSE(client.hasNextEvent());
}

// 验证错误事件可以稳定复现，编排层测试不需要真实服务端制造错误。
TEST(MockRealtimeClientTest, EmitsConfiguredErrorEventDeterministically) {
    interview::common::RealtimeEvent error_event;
    error_event.type = interview::common::RealtimeEventType::kError;
    error_event.error_message = "服务端鉴权失败";
    interview::services::MockRealtimeClient client({error_event});

    ASSERT_TRUE(client.connect());
    const interview::common::RealtimeEvent received_event = client.receiveNextEvent();

    EXPECT_EQ(received_event.type, interview::common::RealtimeEventType::kError);
    EXPECT_EQ(received_event.error_message, "服务端鉴权失败");
}

// 验证关闭后不能继续发送面试官文本，锁住后续 WebSocket close 顺序的基本行为。
TEST(MockRealtimeClientTest, RejectsSendAfterClosed) {
    interview::services::MockRealtimeClient client({});

    ASSERT_TRUE(client.connect());
    client.close();

    EXPECT_FALSE(client.sendInterviewerText("这条消息不应发送。"));
    EXPECT_TRUE(client.getOutboundInterviewerMessages().empty());
}

// 验证空队列读取会明确报错，调用方必须先检查 hasNextEvent。
TEST(MockRealtimeClientTest, ThrowsWhenReceivingWithoutAvailableEvent) {
    interview::services::MockRealtimeClient client({});

    ASSERT_TRUE(client.connect());

    EXPECT_THROW(client.receiveNextEvent(), std::out_of_range);
}
