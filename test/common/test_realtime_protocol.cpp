#include "common/realtime_protocol.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

// 验证业务事件可以完整编码再解码，锁住 WebSocket 前一层的稳定二进制边界。
TEST(RealtimeProtocolTest, EncodesAndDecodesEventFrameRoundTrip) {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kTranscriptFinal;
    event.text = "I learned RAII from a logger project.";
    event.error_message = "";
    event.payload = {0x01, 0x02, 0x03};

    const std::vector<std::uint8_t> frame = interview::common::encodeRealtimeEventFrame(event);
    const interview::common::RealtimeEvent decoded =
        interview::common::decodeRealtimeEventFrame(frame);

    EXPECT_EQ(decoded.type, interview::common::RealtimeEventType::kTranscriptFinal);
    EXPECT_EQ(decoded.text, event.text);
    EXPECT_TRUE(decoded.error_message.empty());
    EXPECT_EQ(decoded.payload, event.payload);
}

// 验证 UTF-8 中文转写不会在 frame 边界中被截断，后续 ASR 中文结果可直接复用。
TEST(RealtimeProtocolTest, PreservesUtf8TranscriptPayload) {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kTranscriptPartial;
    event.text = "我在练习 C++ 资源管理。";

    const interview::common::RealtimeEvent decoded = interview::common::decodeRealtimeEventFrame(
        interview::common::encodeRealtimeEventFrame(event));

    EXPECT_EQ(decoded.text, "我在练习 C++ 资源管理。");
}

// 验证短于固定头部的 frame 会被拒绝，避免真实网络半包被误当成合法事件。
TEST(RealtimeProtocolTest, RejectsFrameShorterThanHeader) {
    const std::vector<std::uint8_t> broken_frame = {0x01, 0x00, 0x00};

    EXPECT_THROW(interview::common::decodeRealtimeEventFrame(broken_frame), std::invalid_argument);
}

// 验证声明长度大于实际字节时会失败，这类边界是 WebSocket 对接时最容易定位错误的位置。
TEST(RealtimeProtocolTest, RejectsPayloadSizeLargerThanAvailableBytes) {
    std::vector<std::uint8_t> frame = {
        static_cast<std::uint8_t>(interview::common::RealtimeEventType::kTranscriptFinal),
        0x00,
        0x00,
        0x00,
        0x05,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        0x00,
        'o',
        'k',
    };

    EXPECT_THROW(interview::common::decodeRealtimeEventFrame(frame), std::invalid_argument);
}

// 验证未知事件类型不会静默进入主流程，避免服务端协议变化导致状态机错乱。
TEST(RealtimeProtocolTest, RejectsUnknownEventType) {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kConnected;
    std::vector<std::uint8_t> frame = interview::common::encodeRealtimeEventFrame(event);
    frame.front() = 99;

    EXPECT_THROW(interview::common::decodeRealtimeEventFrame(frame), std::invalid_argument);
}

// 验证错误事件会保留错误信息，编排层可据此进入 Error 状态并停止会话。
TEST(RealtimeProtocolTest, ParsesErrorEventPayload) {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kError;
    event.error_message = "realtime service unavailable";

    const interview::common::RealtimeEvent decoded = interview::common::decodeRealtimeEventFrame(
        interview::common::encodeRealtimeEventFrame(event));

    EXPECT_EQ(decoded.type, interview::common::RealtimeEventType::kError);
    EXPECT_EQ(decoded.error_message, "realtime service unavailable");
}
