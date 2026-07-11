#include "services/realtime/volc/volc_realtime_client_adapter.h"

#include <deque>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class FakeVolcRealtimeTransport final : public interview::services::IVolcRealtimeTransport {
  public:
    void connect(const interview::services::VolcRealtimeConnectionRequest& request) override {
        // adapter 测试仍然不联网。fake 只记录 request，并把 connected 标记为 true。
        last_request = request;
        connected = true;
    }

    void sendBinary(const std::vector<std::uint8_t>& bytes) override {
        // 模拟真实 transport 的状态限制，确保 adapter 会先 connect 再发
        // StartConnection/StartSession。
        if (!connected || closed) {
            throw std::runtime_error("fake transport is not connected");
        }
        sent_frames.push_back(bytes);
    }

    std::vector<std::uint8_t> receiveBinary() override {
        // incoming_frames 是一段确定性的“服务端脚本”。
        // 如果 adapter 多读或少读，测试会通过 out_of_range 或断言失败暴露问题。
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
    // 保存握手请求，后续如果 adapter 需要验证 header，也可以复用这个 fake。
    interview::services::VolcRealtimeConnectionRequest last_request;
    // 保存 adapter/client 发出的火山 frame bytes，测试再 decode 检查事件类型。
    std::vector<std::vector<std::uint8_t>> sent_frames;
    // 预置的服务端事件队列，按 receiveBinary 调用顺序弹出。
    std::deque<std::vector<std::uint8_t>> incoming_frames;
};

interview::services::VolcRealtimeRuntimeConfig makeConfig() {
    // fake 配置只要求字段完整，不需要真实火山账号。
    interview::services::VolcRealtimeRuntimeConfig config;
    config.endpoint = "wss://example.com/realtime";
    config.app_id = "test-app-id";
    config.access_key = "test-access-key";
    config.resource_id = "volc.speech.dialog";
    config.app_key = "public-app-key";
    config.connect_id = "connect-001";
    config.session_id = "session-001";
    config.model = "1.2.1.1";
    config.input_mod = "text";
    config.strict_audit = true;
    config.enable_volc_websearch = false;
    config.speaker = "test-speaker";
    config.tts_audio_format = "pcm_s16le";
    config.tts_sample_rate_hz = 24000;
    config.tts_channels = 1;
    config.capture_sample_rate_hz = 16000;
    config.capture_channels = 1;
    config.frames_per_buffer = 320;
    config.timeout_ms = 12000;
    return config;
}

std::vector<std::uint8_t> toBytes(const std::string& text) {
    // 把 JSON fixture 转成 payload bytes，覆盖协议层真实字节路径。
    return {text.begin(), text.end()};
}

std::vector<std::uint8_t> makeServerEvent(interview::services::VolcRealtimeEventId event_id,
                                          const std::string& payload = "{}") {
    // 构造服务端事件时，Session 级事件自动写 session_id；
    // ConnectionStarted 这类连接级事件不写 session_id，和真实协议保持一致。
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
    // adapter 发出去的是 bytes。测试通过 decodeSentFrame 回到结构化
    // frame，避免用字符串猜测协议内容。
    return interview::services::decodeVolcRealtimeFrame(transport->sent_frames[index]);
}

} // namespace

// 验证火山 ASRResponse 的 interim/final 标记会映射成项目内部 partial/final transcript。
TEST(VolcRealtimeClientAdapterTest, MapsAsrResponsesToTranscriptEvents) {
    const interview::services::VolcRealtimeFrame interim_frame =
        interview::services::decodeVolcRealtimeFrame(
            // interim=true 模拟 ASR 临时识别，UI 可以展示，但不能触发评分。
            makeServerEvent(interview::services::VolcRealtimeEventId::kAsrResponse,
                            R"({"results":[{"text":"我正在回答","is_interim":true}]})"));
    const interview::services::VolcRealtimeFrame final_frame =
        interview::services::decodeVolcRealtimeFrame(
            // interim=false 模拟最终识别结果，DialogOrchestrator 后续会用它作为候选人回答。
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
        // connect() 内部会等待 ConnectionStarted。
        makeServerEvent(interview::services::VolcRealtimeEventId::kConnectionStarted));
    transport->incoming_frames.push_back(
        // startSession() 后会等待 SessionStarted。
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
        // 前两帧用于 adapter.connect() 完成初始化。
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

// 验证 adapter 显式把 int16 采样编码为 little-endian raw audio frame，
// 业务层不需要了解火山 sequence 编号或二进制 header。
TEST(VolcRealtimeClientAdapterTest, SendsCandidateAudioAsLittleEndianPcm) {
    const std::shared_ptr<FakeVolcRealtimeTransport> transport =
        std::make_shared<FakeVolcRealtimeTransport>();
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kConnectionStarted));
    transport->incoming_frames.push_back(
        makeServerEvent(interview::services::VolcRealtimeEventId::kSessionStarted));
    interview::services::VolcRealtimeRuntimeConfig config = makeConfig();
    config.input_mod = "audio";
    interview::services::VolcRealtimeClientAdapter adapter(config, transport);
    interview::services::AudioPcmChunk chunk;
    chunk.samples = {1, -2};

    ASSERT_TRUE(adapter.connect());
    ASSERT_TRUE(adapter.sendCandidateAudio(chunk));

    ASSERT_EQ(transport->sent_frames.size(), 3u);
    const interview::services::VolcRealtimeFrame frame = decodeSentFrame(transport, 2);
    EXPECT_EQ(frame.message_type, interview::services::VolcRealtimeMessageType::kAudioOnlyRequest);
    EXPECT_EQ(frame.payload, (std::vector<std::uint8_t>{0x01, 0x00, 0xFE, 0xFF}));
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
        // 第三帧是业务层真正关心的候选人最终回答。
        makeServerEvent(interview::services::VolcRealtimeEventId::kAsrResponse,
                        R"({"results":[{"text":"最终回答","is_interim":false}]})"));
    transport->incoming_frames.push_back(
        // 最后一帧关闭 session，adapter 应该转成 kClosed 并让 hasNextEvent 变 false。
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
