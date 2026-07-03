#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace interview {
namespace common {

// Realtime 事件类型是 WebSocket 之前的稳定边界：
// mock、真实 WebSocket、后续 Qt UI 都只理解这些业务事件，不直接依赖服务端原始包格式。
enum class RealtimeEventType {
    kConnected = 1,
    kTranscriptPartial = 2,
    kTranscriptFinal = 3,
    kInterviewerText = 4,
    kError = 5,
    kClosed = 6,
};

// 一条 realtime 业务事件。当前阶段只保存文本、错误信息和可选二进制载荷，
// 后续接入真实语音服务时可以把音频 chunk 或服务端原始 payload 放进 payload。
struct RealtimeEvent {
    RealtimeEventType type = RealtimeEventType::kConnected;
    std::string text;
    std::string error_message;
    std::vector<std::uint8_t> payload;
};

// 把业务事件编码成固定头部的二进制 frame，方便用 fixture 测试协议边界。
std::vector<std::uint8_t> encodeRealtimeEventFrame(const RealtimeEvent& event);

// 从固定头部的二进制 frame 解析业务事件；格式错误会抛出 std::invalid_argument。
RealtimeEvent decodeRealtimeEventFrame(const std::vector<std::uint8_t>& frame);

// 把事件类型转换成稳定 key，方便日志、测试和后续 UI 状态适配。
std::string realtimeEventTypeToKey(RealtimeEventType type);

} // namespace common
} // namespace interview
