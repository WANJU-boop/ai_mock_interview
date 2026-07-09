#include "common/realtime_protocol.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace interview {
namespace common {

namespace {

constexpr std::size_t kHeaderSize = 13;
constexpr std::size_t kTypeOffset = 0;
constexpr std::size_t kTextSizeOffset = 1;
constexpr std::size_t kErrorSizeOffset = 5;
constexpr std::size_t kPayloadSizeOffset = 9;

void appendUint32(std::vector<std::uint8_t>& frame, std::uint32_t value) {
    frame.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

std::uint32_t readUint32(const std::vector<std::uint8_t>& frame, std::size_t offset) {
    return (static_cast<std::uint32_t>(frame[offset]) << 24) |
           (static_cast<std::uint32_t>(frame[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(frame[offset + 2]) << 8) |
           static_cast<std::uint32_t>(frame[offset + 3]);
}

std::uint32_t checkedSize(std::size_t size, const char* field_name) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码 realtime frame。");
    }

    return static_cast<std::uint32_t>(size);
}

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
    frame.reserve(kHeaderSize + event.text.size() + event.error_message.size() +
                  event.payload.size());

    frame.push_back(static_cast<std::uint8_t>(event.type));
    appendUint32(frame, checkedSize(event.text.size(), "text"));
    appendUint32(frame, checkedSize(event.error_message.size(), "error_message"));
    appendUint32(frame, checkedSize(event.payload.size(), "payload"));

    frame.insert(frame.end(), event.text.begin(), event.text.end());
    frame.insert(frame.end(), event.error_message.begin(), event.error_message.end());
    frame.insert(frame.end(), event.payload.begin(), event.payload.end());
    return frame;
}

RealtimeEvent decodeRealtimeEventFrame(const std::vector<std::uint8_t>& frame) {
    if (frame.size() < kHeaderSize) {
        throw std::invalid_argument("realtime frame 头部长度不足。");
    }

    RealtimeEvent event;
    event.type = decodeEventType(frame[kTypeOffset]);
    const std::size_t text_size = readUint32(frame, kTextSizeOffset);
    const std::size_t error_size = readUint32(frame, kErrorSizeOffset);
    const std::size_t payload_size = readUint32(frame, kPayloadSizeOffset);

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

    if (offset != frame.size()) {
        throw std::invalid_argument("realtime frame 包含未声明的多余字节。");
    }

    return event;
}

std::string realtimeEventTypeToKey(RealtimeEventType type) {
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

    return "unknown";
}

} // namespace common
} // namespace interview
