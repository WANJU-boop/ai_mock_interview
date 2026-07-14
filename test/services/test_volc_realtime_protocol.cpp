#include "services/realtime/volc/volc_realtime_protocol.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::vector<std::uint8_t> toBytes(const std::string& text) {
    // 测试里手写 JSON 字符串，再按 UTF-8 字节放进协议 payload。
    // 这样可以验证协议层不会破坏中文文本。
    return {text.begin(), text.end()};
}

} // namespace

// 验证 StartSession 能按火山文档的 header + event + session_id + payload 顺序编码，
// 这是后续真实 WebSocket 发送会话初始化事件的最低层边界。
TEST(VolcRealtimeProtocolTest, EncodesStartSessionFrameWithSessionIdAndJsonPayload) {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kFullClientRequest;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.event_id = interview::services::VolcRealtimeEventId::kStartSession;
    frame.session_id = "session-001";
    // StartSession 是 Session 级事件，测试故意提供 session_id，确认编码器会写入 optional session
    // 字段。
    frame.payload = toBytes(R"({"dialog":{"extra":{"input_mod":"text"}}})");

    const std::vector<std::uint8_t> encoded = interview::services::encodeVolcRealtimeFrame(frame);

    ASSERT_GE(encoded.size(), 4u);
    EXPECT_EQ(encoded[0], 0x11);
    EXPECT_EQ(encoded[1], 0x14);
    EXPECT_EQ(encoded[2], 0x10);
    EXPECT_EQ(encoded[3], 0x00);

    const interview::services::VolcRealtimeFrame decoded =
        interview::services::decodeVolcRealtimeFrame(encoded);
    EXPECT_EQ(decoded.message_type,
              interview::services::VolcRealtimeMessageType::kFullClientRequest);
    EXPECT_EQ(decoded.event_id, interview::services::VolcRealtimeEventId::kStartSession);
    EXPECT_EQ(decoded.session_id, "session-001");
    EXPECT_EQ(decoded.payload, frame.payload);
}

// 验证服务端 ChatResponse 可以保留 UTF-8 中文文本，后续 5.3 会把它映射成项目内部面试官文本事件。
TEST(VolcRealtimeProtocolTest, DecodesServerChatResponseWithUtf8Payload) {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kFullServerResponse;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.event_id = interview::services::VolcRealtimeEventId::kChatResponse;
    frame.session_id = "session-abc";
    frame.payload = toBytes(R"({"content":"请继续说明你的项目经验。"})");

    const interview::services::VolcRealtimeFrame decoded =
        interview::services::decodeVolcRealtimeFrame(
            interview::services::encodeVolcRealtimeFrame(frame));

    EXPECT_EQ(decoded.message_type,
              interview::services::VolcRealtimeMessageType::kFullServerResponse);
    EXPECT_EQ(decoded.event_id, interview::services::VolcRealtimeEventId::kChatResponse);
    EXPECT_EQ(decoded.session_id, "session-abc");
    EXPECT_EQ(std::string(decoded.payload.begin(), decoded.payload.end()),
              R"({"content":"请继续说明你的项目经验。"})");
}

// 验证服务端 ConnectionStarted 携带可选 connect_id 时不会把 ID 长度误当成 payload 长度。
// 这是真实握手的第一帧，解析错位会让后续 StartSession 完全无法发送。
TEST(VolcRealtimeProtocolTest, DecodesConnectionEventWithOptionalConnectId) {
    interview::services::VolcRealtimeFrame source;
    source.message_type = interview::services::VolcRealtimeMessageType::kFullServerResponse;
    source.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    source.serialization = interview::services::VolcRealtimeSerialization::kJson;
    source.event_id = interview::services::VolcRealtimeEventId::kConnectionStarted;
    source.connect_id = "connect-server-001";
    source.payload = {'{', '}'};

    const interview::services::VolcRealtimeFrame decoded =
        interview::services::decodeVolcRealtimeFrame(
            interview::services::encodeVolcRealtimeFrame(source));

    EXPECT_EQ(decoded.connect_id, "connect-server-001");
    EXPECT_EQ(decoded.payload, (std::vector<std::uint8_t>{'{', '}'}));
}

// 验证音频响应 frame 不强行解析 payload，避免 TTSResponse 的二进制音频被误当 JSON。
TEST(VolcRealtimeProtocolTest, PreservesAudioOnlyResponsePayload) {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kAudioOnlyResponse;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kRaw;
    frame.event_id = interview::services::VolcRealtimeEventId::kTtsResponse;
    frame.session_id = "session-audio";
    frame.payload = {0x01, 0x02, 0x03, 0x04};

    const interview::services::VolcRealtimeFrame decoded =
        interview::services::decodeVolcRealtimeFrame(
            interview::services::encodeVolcRealtimeFrame(frame));

    EXPECT_EQ(decoded.message_type,
              interview::services::VolcRealtimeMessageType::kAudioOnlyResponse);
    EXPECT_EQ(decoded.payload, frame.payload);
}

// 验证错误 frame 能解析 code 和错误 JSON，真实集成时可以把它转换成明确错误消息。
TEST(VolcRealtimeProtocolTest, DecodesErrorFrameWithCodeAndPayload) {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kErrorInformation;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kError;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.code = 42000020;
    frame.payload = toBytes(R"({"error":"StartSession event payload asr extra is null"})");

    const interview::services::VolcRealtimeFrame decoded =
        interview::services::decodeVolcRealtimeFrame(
            interview::services::encodeVolcRealtimeFrame(frame));

    EXPECT_EQ(decoded.message_type,
              interview::services::VolcRealtimeMessageType::kErrorInformation);
    ASSERT_TRUE(decoded.code.has_value());
    EXPECT_EQ(*decoded.code, 42000020);
    EXPECT_EQ(decoded.payload, frame.payload);
}

// 验证 Session 级事件缺少 session id 时不能编码，避免真实服务端收到不完整 optional 字段。
TEST(VolcRealtimeProtocolTest, RejectsSessionEventWithoutSessionId) {
    interview::services::VolcRealtimeFrame frame;
    frame.message_type = interview::services::VolcRealtimeMessageType::kFullClientRequest;
    frame.flag = interview::services::VolcRealtimeMessageFlag::kEvent;
    frame.serialization = interview::services::VolcRealtimeSerialization::kJson;
    frame.event_id = interview::services::VolcRealtimeEventId::kChatTextQuery;
    frame.payload = toBytes(R"({"content":"你好"})");

    EXPECT_THROW(interview::services::encodeVolcRealtimeFrame(frame), std::invalid_argument);
}

// 验证未知事件 ID 会被拒绝，供应商协议变化时不会静默进入业务状态机。
TEST(VolcRealtimeProtocolTest, RejectsUnknownEventId) {
    std::vector<std::uint8_t> frame = {
        // 这个 fixture 手写二进制 frame，而不是调用 encode，目的是直接模拟“服务端发来未知 event
        // id”。
        0x11, 0x94, 0x10, 0x00, // header: server JSON event
        0x00, 0x00, 0x27, 0x0F, // unknown event id 9999
        0x00, 0x00, 0x00, 0x02, // payload size
        '{',  '}',
    };

    EXPECT_THROW(interview::services::decodeVolcRealtimeFrame(frame), std::invalid_argument);
}

// 验证声明 payload 长度超过实际字节会失败，这类错误是 WebSocket 半包/封包错位的关键边界。
TEST(VolcRealtimeProtocolTest, RejectsPayloadSizeLargerThanAvailableBytes) {
    std::vector<std::uint8_t> frame = {
        // ConnectionStarted 是连接级事件，不带 session_id；payload size 故意写 5，但实际只有 2
        // 字节。
        0x11, 0x94, 0x10, 0x00, 0x00, 0x00, 0x00, 0x32, // ConnectionStarted
        0x00, 0x00, 0x00, 0x05,                         // payload size declares 5 bytes
        '{',  '}',
    };

    EXPECT_THROW(interview::services::decodeVolcRealtimeFrame(frame), std::invalid_argument);
}

// 验证事件 key 稳定，后续日志和 Volc -> 项目内部事件映射可以复用同一套名称。
TEST(VolcRealtimeProtocolTest, ConvertsEventIdToStableKey) {
    EXPECT_EQ(interview::services::volcRealtimeEventIdToKey(
                  interview::services::VolcRealtimeEventId::kChatResponse),
              "chat_response");
    EXPECT_EQ(interview::services::volcRealtimeEventIdToKey(
                  interview::services::VolcRealtimeEventId::kAsrResponse),
              "asr_response");
}
