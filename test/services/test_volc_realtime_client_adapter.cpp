#include "services/volc_realtime_client_adapter.h"

#include <deque>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FakeVolcRealtimeTransport final : public interview::services::IVolcRealtimeTransport {
  public:
    void connect(const interview::services::VolcRealtimeConnectionRequest& request) override {
        last_request = request;
        connected = true;
    }

    void sendBinary(const std::vector<std::uint8_t>& bytes) override {
        if (!connected || closed) {
            throw std::runtime_error("fake transport is not connected");
        }
        sent_frames.push_back(bytes);
    }

    std::vector<std::uint8_t> receiveBinary() override {
        if (incoming_frames.empty()) {
            throw std::out_of_range("no incoming frame");
        }

        std::vector<std::uint8_t> frame = incoming_frames.front();
        incoming_frames.pop_front();
        return frame;
    }

    void close() override {
        closed = true;
    }

    bool connected = false;
    bool closed = false;
    interview::services::VolcRealtimeConnectionRequest last_request;
    std::vector<std::vector<std::uint8_t>> sent_frames;
    std::deque<std::vector<std::uint8_t>> incoming_frames;
};

interview::services::VolcRealtimeClientConfig makeConfig() {
    interview::services::VolcRealtimeClientConfig config;
    config.app_id = "test-app-id";
    config.access_key = "test-access-key";
    config.connect_id = "connect-001";
    config.session_id = "session-001";
    return config;
}

std::vector<std::uint8_t> toBytes(const std::string& text) {
    return {text.begin(), text.end()};
}

std::vector<std::uint8_t> makeServerEvent(interview::services::VolcRealtimeEventId event_id,
                                          const std::string& payload = "{}") {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kFullServerResponse;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.event_id = event_id;
    frame.payload = toBytes(payload);
    if (interview::services::isVolcRealtimeSessionEvent(event_id)) {
        frame.session_id = "session-001";
    }
    return interview::services::encodeVolcRealtimeFrame(frame);
}

interview::services::VolcRealtimeFrame
decodeSentFrame(const std::shared_ptr<FakeVolcRealtimeTransport>& transport, std::size_t index) {
    return interview::services::decodeVolcRealtimeFrame(transport->sent_frames[index]);
}

} // namespace

// 验证火山 ASRResponse 的 interim/final 标记会映射成项目内部 partial/final transcript。
TEST(VolcRealtimeClientAdapterTest, MapsAsrResponsesToTranscriptEvents) {
    const interview::services::VolcRealtimeFrame interim_frame =
        interview::services::decodeVolcRealtimeFrame(
            makeServerEvent(interview::services::VolcRealtimeEventId::kAsrResponse,
                            R"({"results":[{"text":"我正在回答","is_interim":true}]})"));
    const interview::services::VolcRealtimeFrame final_frame =
        interview::services::decodeVolcRealtimeFrame(
            makeServerEvent(interview::services::VolcRealtimeEventId::kAsrResponse,
                            R"({"results":[{"text":"这是最终回答","is_interim":false}]})"));

    const std::optional<interview::common::RealtimeEvent> interim_event =
        interview::services::mapVolcRealtimeFrameToRealtimeEvent(interim_frame);
    const std::optional<interview::common::RealtimeEvent> final_event =
        interview::services::mapVolcRealtimeFrameToRealtimeEvent(final_frame);

    ASSERT_TRUE(interim_event.has_value());
    ASSERT_TRUE(final_event.has_value());
    EXPECT_EQ(interim_event->type, interview::common::RealtimeEventType::kTranscriptPartial);
    EXPECT_EQ(interim_event->text, "我正在回答");
    EXPECT_EQ(final_event->type, interview::common::RealtimeEventType::kTranscriptFinal);
    EXPECT_EQ(final_event->text, "这是最终回答");
}

// 验证 ChatResponse 和错误事件会分别映射成面试官文本和项目内部错误事件。
TEST(VolcRealtimeClientAdapterTest, MapsChatResponseAndErrorEvents) {
    const interview::services::VolcRealtimeFrame chat_frame =
        interview::services::decodeVolcRealtimeFrame(
            makeServerEvent(interview::services::VolcRealtimeEventId::kChatResponse,
                            R"({"content":"请补充一个项目例子。"})"));
    const interview::services::VolcRealtimeFrame error_frame =
        interview::services::decodeVolcRealtimeFrame(
            makeServerEvent(interview::services::VolcRealtimeEventId::kDialogCommonError,
                            R"({"status_code":"50000000","message":"模型推理失败"})"));

    const std::optional<interview::common::RealtimeEvent> chat_event =
        interview::services::mapVolcRealtimeFrameToRealtimeEvent(chat_frame);
    const std::optional<interview::common::RealtimeEvent> error_event =
        interview::services::mapVolcRealtimeFrameToRealtimeEvent(error_frame);

    ASSERT_TRUE(chat_event.has_value());
    ASSERT_TRUE(error_event.has_value());
    EXPECT_EQ(chat_event->type, interview::common::RealtimeEventType::kInterviewerText);
    EXPECT_EQ(chat_event->text, "请补充一个项目例子。");
    EXPECT_EQ(error_event->type, interview::common::RealtimeEventType::kError);
    EXPECT_EQ(error_event->error_message, "模型推理失败");
}

// 验证 adapter connect 会完成火山连接/会话初始化，并向 DialogOrchestrator 暴露统一 kConnected。
TEST(VolcRealtimeClientAdapterTest, ConnectStartsVolcConnectionAndExposesConnectedEvent) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kConnectionStarted));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kSessionStarted));
    interview::services::VolcRealtimeClientAdapter adapter(makeConfig(), transport);

    ASSERT_TRUE(adapter.connect());

    ASSERT_EQ(transport->sent_frames.size(), 2u);
    EXPECT_EQ(decodeSentFrame(transport, 0).event_id,
              interview::services::VolcRealtimeEventId::kStartConnection);
    EXPECT_EQ(decodeSentFrame(transport, 1).event_id,
              interview::services::VolcRealtimeEventId::kStartSession);
    ASSERT_TRUE(adapter.hasNextEvent());

    const interview::common::RealtimeEvent event = adapter.receiveNextEvent();
    EXPECT_EQ(event.type, interview::common::RealtimeEventType::kConnected);
}

// 验证 sendInterviewerText 通过 ChatTTSText 下发两包文本合成请求，保持 IRealtimeClient 接口不变。
TEST(VolcRealtimeClientAdapterTest, SendInterviewerTextUsesChatTtsTextFrames) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kConnectionStarted));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kSessionStarted));
    interview::services::VolcRealtimeClientAdapter adapter(makeConfig(), transport);

    ASSERT_TRUE(adapter.connect());
    ASSERT_TRUE(adapter.sendInterviewerText("请回答第一题。"));

    ASSERT_EQ(transport->sent_frames.size(), 4u);
    const interview::services::VolcRealtimeFrame first_tts = decodeSentFrame(transport, 2);
    const interview::services::VolcRealtimeFrame end_tts = decodeSentFrame(transport, 3);
    EXPECT_EQ(first_tts.event_id, interview::services::VolcRealtimeEventId::kChatTtsText);
    EXPECT_EQ(end_tts.event_id, interview::services::VolcRealtimeEventId::kChatTtsText);
    EXPECT_NE(
        std::string(first_tts.payload.begin(), first_tts.payload.end()).find("请回答第一题。"),
        std::string::npos);
    EXPECT_NE(std::string(end_tts.payload.begin(), end_tts.payload.end()).find(R"("end":true)"),
              std::string::npos);
}

// 验证 adapter 能持续读取火山 frame，并在 SessionFinished 后关闭内部事件流。
TEST(VolcRealtimeClientAdapterTest, ReceivesMappedEventsUntilClosed) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kConnectionStarted));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kSessionStarted));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kAsrResponse,
                        R"({"results":[{"text":"最终回答","is_interim":false}]})"));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kSessionFinished));
    interview::services::VolcRealtimeClientAdapter adapter(makeConfig(), transport);

    ASSERT_TRUE(adapter.connect());
    adapter.receiveNextEvent(); // consume kConnected emitted by adapter after setup.

    const interview::common::RealtimeEvent transcript = adapter.receiveNextEvent();
    EXPECT_EQ(transcript.type, interview::common::RealtimeEventType::kTranscriptFinal);
    EXPECT_EQ(transcript.text, "最终回答");

    const interview::common::RealtimeEvent closed = adapter.receiveNextEvent();
    EXPECT_EQ(closed.type, interview::common::RealtimeEventType::kClosed);
    EXPECT_FALSE(adapter.hasNextEvent());
}
