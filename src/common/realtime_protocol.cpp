#include "common/realtime_protocol.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

// 这是项目内部 realtime 事件的最小二进制协议，不是火山供应商协议：
// 1. 固定 13 字节头部记录事件类型和三段可变字段长度。
// 2. 正文依次写入 text、error_message、payload，不依赖终止字符。
// 3. 解码时严格拒绝短包、未知类型、越界长度和未声明尾部字节。
// 这样 mock fixture、未来 WebSocket 和 UI 适配层可以共享同一条稳定边界。
namespace interview {
namespace common {

namespace {

// header 布局：
// byte 0      = RealtimeEventType
// byte 1..4   = text 长度
// byte 5..8   = error_message 长度
// byte 9..12  = payload 长度
constexpr std::size_t kHeaderSize = 13;
constexpr std::size_t kTypeOffset = 0;
constexpr std::size_t kTextSizeOffset = 1;
constexpr std::size_t kErrorSizeOffset = 5;
constexpr std::size_t kPayloadSizeOffset = 9;

// 所有 uint32 长度统一写成网络字节序（big-endian，大端序），
// 避免编码结果依赖当前 CPU 是小端还是大端。
void appendUint32(std::vector<std::uint8_t>& frame, std::uint32_t value) {
    frame.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

// 调用方必须先确认至少有 4 字节可读；这里和 appendUint32 保持完全对称。
std::uint32_t readUint32(const std::vector<std::uint8_t>& frame, std::size_t offset) {
    return (static_cast<std::uint32_t>(frame[offset]) << 24) |
           (static_cast<std::uint32_t>(frame[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(frame[offset + 2]) << 8) |
           static_cast<std::uint32_t>(frame[offset + 3]);
}

// 协议长度字段只有 32 bit。编码前显式拒绝过大容器，避免 size_t 被静默截断。
std::uint32_t checkedSize(std::size_t size, const char* field_name) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码 realtime frame。");
    }

    return static_cast<std::uint32_t>(size);
}

// 显式列出合法类型，供应商或测试传入新数字时先失败，
// 不让未知事件静默进入 DialogOrchestrator 的状态机。
RealtimeEventType decodeEventType(std::uint8_t raw_type) {
    switch (raw_type) {
    case static_cast<std::uint8_t>(RealtimeEventType::kConnected):
        return RealtimeEventType::kConnected;
    case static_cast<std::uint8_t>(RealtimeEventType::kTranscriptPartial):
        return RealtimeEventType::kTranscriptPartial;
    case static_cast<std::uint8_t>(RealtimeEventType::kTranscriptFinal):
        return RealtimeEventType::kTranscriptFinal;
    case static_cast<std::uint8_t>(RealtimeEventType::kInterviewerText):
        return RealtimeEventType::kInterviewerText;
    case static_cast<std::uint8_t>(RealtimeEventType::kError):
        return RealtimeEventType::kError;
    case static_cast<std::uint8_t>(RealtimeEventType::kClosed):
        return RealtimeEventType::kClosed;
    default:
        throw std::invalid_argument("未知 realtime 事件类型。");
    }
}

// 可变字段按顺序读取，因此 offset 来自前一个已经验证成功的字段末尾。
// 使用“所需长度 <= 剩余长度”避免 offset + field_size 的加法溢出。
void ensureAvailable(const std::vector<std::uint8_t>& frame, std::size_t offset,
                     std::size_t field_size, const char* field_name) {
    if (field_size > frame.size() - offset) {
        throw std::invalid_argument(std::string(field_name) +
                                    " 长度超过 realtime frame 剩余字节。");
    }
}

} // namespace

std::vector<std::uint8_t> encodeRealtimeEventFrame(const RealtimeEvent& event) {
    std::vector<std::uint8_t> frame;
    // reserve 只减少扩容次数，不改变最终字节布局；实际长度仍由后续 push/insert 决定。
    frame.reserve(kHeaderSize + event.text.size() + event.error_message.size() +
                  event.payload.size());

    // 先写完整头部，让解码方在读取正文前就知道三段可变数据的精确边界。
    frame.push_back(static_cast<std::uint8_t>(event.type));
    appendUint32(frame, checkedSize(event.text.size(), "text"));
    appendUint32(frame, checkedSize(event.error_message.size(), "error_message"));
    appendUint32(frame, checkedSize(event.payload.size(), "payload"));

    // std::string 按原始 UTF-8 字节写入，不按“字符数”计算，因此中文不会被协议层截断。
    frame.insert(frame.end(), event.text.begin(), event.text.end());
    frame.insert(frame.end(), event.error_message.begin(), event.error_message.end());
    frame.insert(frame.end(), event.payload.begin(), event.payload.end());
    return frame;
}

RealtimeEvent decodeRealtimeEventFrame(const std::vector<std::uint8_t>& frame) {
    // 只有固定头部完整时，下面按 offset 直接读取长度字段才是安全的。
    if (frame.size() < kHeaderSize) {
        throw std::invalid_argument("realtime frame 头部长度不足。");
    }

    RealtimeEvent event;
    event.type = decodeEventType(frame[kTypeOffset]);
    const std::size_t text_size = readUint32(frame, kTextSizeOffset);
    const std::size_t error_size = readUint32(frame, kErrorSizeOffset);
    const std::size_t payload_size = readUint32(frame, kPayloadSizeOffset);

    // 正文解析顺序必须与编码顺序一致：text -> error_message -> payload。
    // 每段都先验证剩余字节，再构造 string/vector，坏包不会触发越界迭代器。
    std::size_t offset = kHeaderSize;
    ensureAvailable(frame, offset, text_size, "text");
    event.text = std::string(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                             frame.begin() + static_cast<std::ptrdiff_t>(offset + text_size));
    offset += text_size;

    ensureAvailable(frame, offset, error_size, "error_message");
    event.error_message =
        std::string(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                    frame.begin() + static_cast<std::ptrdiff_t>(offset + error_size));
    offset += error_size;

    ensureAvailable(frame, offset, payload_size, "payload");
    event.payload.assign(frame.begin() + static_cast<std::ptrdiff_t>(offset),
                         frame.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
    offset += payload_size;

    // 多余尾部通常意味着发送方长度字段写错或协议版本不一致，不能静默忽略。
    if (offset != frame.size()) {
        throw std::invalid_argument("realtime frame 包含未声明的多余字节。");
    }

    return event;
}

std::string realtimeEventTypeToKey(RealtimeEventType type) {
    // 返回稳定的英文 key 供日志、测试和未来 UI 映射使用，不依赖编译器的枚举输出格式。
    switch (type) {
    case RealtimeEventType::kConnected:
        return "connected";
    case RealtimeEventType::kTranscriptPartial:
        return "transcript_partial";
    case RealtimeEventType::kTranscriptFinal:
        return "transcript_final";
    case RealtimeEventType::kInterviewerText:
        return "interviewer_text";
    case RealtimeEventType::kError:
        return "error";
    case RealtimeEventType::kClosed:
        return "closed";
    }

    // 这里作为防御性兜底；正常枚举值都应在上面的 switch 中返回。
    return "unknown";
}

} // namespace common
} // namespace interview
