#include "services/volc_realtime_protocol.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace interview {
namespace services {

namespace {

constexpr std::uint8_t kProtocolVersion = 0x1;
constexpr std::uint8_t kHeaderSizeInWords = 0x1;
constexpr std::size_t kFixedHeaderSize = 4;
constexpr std::uint8_t kNibbleMask = 0x0F;

void appendInt32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>((raw >> 24) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xFF));
}

std::uint32_t readUint32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::int32_t readInt32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::int32_t>(readUint32(bytes, offset));
}

void ensureAvailable(const std::vector<std::uint8_t>& bytes, std::size_t offset,
                     std::size_t required_size, const char* field_name) {
    if (offset > bytes.size() || required_size > bytes.size() - offset) {
        throw std::invalid_argument(std::string(field_name) + " 超出火山 realtime frame 边界。");
    }
}

void appendSizedBytes(std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& payload,
                      const char* field_name) {
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码火山 frame。");
    }

    appendInt32(bytes, static_cast<std::int32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void appendSizedString(std::vector<std::uint8_t>& bytes, const std::string& value,
                       const char* field_name) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码火山 frame。");
    }

    appendInt32(bytes, static_cast<std::int32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

VolcRealtimeMessageType decodeMessageType(std::uint8_t raw_value) {
    switch (raw_value) {
    case static_cast<std::uint8_t>(VolcRealtimeMessageType::kFullClientRequest):
        return VolcRealtimeMessageType::kFullClientRequest;
    case static_cast<std::uint8_t>(VolcRealtimeMessageType::kAudioOnlyRequest):
        return VolcRealtimeMessageType::kAudioOnlyRequest;
    case static_cast<std::uint8_t>(VolcRealtimeMessageType::kFullServerResponse):
        return VolcRealtimeMessageType::kFullServerResponse;
    case static_cast<std::uint8_t>(VolcRealtimeMessageType::kAudioOnlyResponse):
        return VolcRealtimeMessageType::kAudioOnlyResponse;
    case static_cast<std::uint8_t>(VolcRealtimeMessageType::kErrorInformation):
        return VolcRealtimeMessageType::kErrorInformation;
    default:
        throw std::invalid_argument("未知火山 realtime message type。");
    }
}

VolcRealtimeMessageFlag decodeMessageFlag(std::uint8_t raw_value) {
    switch (raw_value) {
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kNoSequence):
        return VolcRealtimeMessageFlag::kNoSequence;
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kPositiveSequence):
        return VolcRealtimeMessageFlag::kPositiveSequence;
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kLastNoSequence):
        return VolcRealtimeMessageFlag::kLastNoSequence;
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kLastNegativeSequence):
        return VolcRealtimeMessageFlag::kLastNegativeSequence;
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kEvent):
        return VolcRealtimeMessageFlag::kEvent;
    case static_cast<std::uint8_t>(VolcRealtimeMessageFlag::kError):
        return VolcRealtimeMessageFlag::kError;
    default:
        throw std::invalid_argument("未知火山 realtime message flag。");
    }
}

VolcRealtimeSerialization decodeSerialization(std::uint8_t raw_value) {
    switch (raw_value) {
    case static_cast<std::uint8_t>(VolcRealtimeSerialization::kRaw):
        return VolcRealtimeSerialization::kRaw;
    case static_cast<std::uint8_t>(VolcRealtimeSerialization::kJson):
        return VolcRealtimeSerialization::kJson;
    default:
        throw std::invalid_argument("未知火山 realtime serialization method。");
    }
}

VolcRealtimeCompression decodeCompression(std::uint8_t raw_value) {
    switch (raw_value) {
    case static_cast<std::uint8_t>(VolcRealtimeCompression::kNone):
        return VolcRealtimeCompression::kNone;
    case static_cast<std::uint8_t>(VolcRealtimeCompression::kGzip):
        return VolcRealtimeCompression::kGzip;
    default:
        throw std::invalid_argument("未知火山 realtime compression method。");
    }
}

VolcRealtimeEventId decodeEventId(std::int32_t raw_value) {
    switch (raw_value) {
    case static_cast<std::int32_t>(VolcRealtimeEventId::kStartConnection):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kFinishConnection):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kStartSession):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kFinishSession):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kTaskRequest):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kUpdateConfig):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kSayHello):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kEndAsr):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatTtsText):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatTextQuery):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatRagText):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationCreate):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationUpdate):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationRetrieve):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationTruncate):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationDelete):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kClientInterrupt):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConnectionStarted):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConnectionFailed):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConnectionFinished):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kSessionStarted):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kSessionFinished):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kSessionFailed):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kUsageResponse):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConfigUpdated):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kTtsSentenceStart):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kTtsSentenceEnd):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kTtsResponse):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kTtsEnded):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kAsrInfo):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kAsrResponse):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kAsrEnded):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatResponse):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatTextQueryConfirmed):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kChatEnded):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationCreated):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationUpdated):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationRetrieved):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationTruncated):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kConversationDeleted):
    case static_cast<std::int32_t>(VolcRealtimeEventId::kDialogCommonError):
        return static_cast<VolcRealtimeEventId>(raw_value);
    default:
        throw std::invalid_argument("未知火山 realtime event id。");
    }
}

bool flagHasSequence(VolcRealtimeMessageFlag flag) {
    return flag == VolcRealtimeMessageFlag::kPositiveSequence ||
           flag == VolcRealtimeMessageFlag::kLastNegativeSequence;
}

} // namespace

bool isVolcRealtimeConnectEvent(VolcRealtimeEventId event_id) {
    return event_id == VolcRealtimeEventId::kStartConnection ||
           event_id == VolcRealtimeEventId::kFinishConnection ||
           event_id == VolcRealtimeEventId::kConnectionStarted ||
           event_id == VolcRealtimeEventId::kConnectionFailed ||
           event_id == VolcRealtimeEventId::kConnectionFinished;
}

bool isVolcRealtimeSessionEvent(VolcRealtimeEventId event_id) {
    return !isVolcRealtimeConnectEvent(event_id);
}

std::vector<std::uint8_t> encodeVolcRealtimeFrame(const VolcRealtimeFrame& frame) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kFixedHeaderSize + frame.session_id.size() + frame.payload.size() + 16);

    bytes.push_back(static_cast<std::uint8_t>((kProtocolVersion << 4) | kHeaderSizeInWords));
    bytes.push_back(
        static_cast<std::uint8_t>((static_cast<std::uint8_t>(frame.message_type) << 4) |
                                  (static_cast<std::uint8_t>(frame.flag) & kNibbleMask)));
    bytes.push_back(
        static_cast<std::uint8_t>((static_cast<std::uint8_t>(frame.serialization) << 4) |
                                  (static_cast<std::uint8_t>(frame.compression) & kNibbleMask)));
    bytes.push_back(0x00);

    if (frame.flag == VolcRealtimeMessageFlag::kError ||
        frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        if (!frame.code.has_value()) {
            throw std::invalid_argument("火山错误 frame 必须携带 code。");
        }
        appendInt32(bytes, *frame.code);
    } else if (flagHasSequence(frame.flag)) {
        if (!frame.sequence.has_value()) {
            throw std::invalid_argument("火山 sequence frame 必须携带 sequence。");
        }
        appendInt32(bytes, *frame.sequence);
    } else if (frame.flag == VolcRealtimeMessageFlag::kEvent) {
        if (!frame.event_id.has_value()) {
            throw std::invalid_argument("火山事件 frame 必须携带 event id。");
        }
        appendInt32(bytes, static_cast<std::int32_t>(*frame.event_id));
        if (isVolcRealtimeSessionEvent(*frame.event_id)) {
            if (frame.session_id.empty()) {
                throw std::invalid_argument("火山 Session 事件必须携带 session id。");
            }
            appendSizedString(bytes, frame.session_id, "session_id");
        }
    }

    appendSizedBytes(bytes, frame.payload, "payload");
    return bytes;
}

VolcRealtimeFrame decodeVolcRealtimeFrame(const std::vector<std::uint8_t>& bytes) {
    ensureAvailable(bytes, 0, kFixedHeaderSize, "header");

    const std::uint8_t protocol_version = (bytes[0] >> 4) & kNibbleMask;
    const std::uint8_t header_size_words = bytes[0] & kNibbleMask;
    if (protocol_version != kProtocolVersion || header_size_words != kHeaderSizeInWords) {
        throw std::invalid_argument("不支持的火山 realtime header 版本或长度。");
    }

    VolcRealtimeFrame frame;
    frame.message_type = decodeMessageType((bytes[1] >> 4) & kNibbleMask);
    frame.flag = decodeMessageFlag(bytes[1] & kNibbleMask);
    frame.serialization = decodeSerialization((bytes[2] >> 4) & kNibbleMask);
    frame.compression = decodeCompression(bytes[2] & kNibbleMask);

    std::size_t offset = kFixedHeaderSize;
    if (frame.flag == VolcRealtimeMessageFlag::kError ||
        frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        ensureAvailable(bytes, offset, 4, "error code");
        frame.code = readInt32(bytes, offset);
        offset += 4;
    } else if (flagHasSequence(frame.flag)) {
        ensureAvailable(bytes, offset, 4, "sequence");
        frame.sequence = readInt32(bytes, offset);
        offset += 4;
    } else if (frame.flag == VolcRealtimeMessageFlag::kEvent) {
        ensureAvailable(bytes, offset, 4, "event id");
        frame.event_id = decodeEventId(readInt32(bytes, offset));
        offset += 4;

        if (isVolcRealtimeSessionEvent(*frame.event_id)) {
            ensureAvailable(bytes, offset, 4, "session id size");
            const std::uint32_t session_id_size = readUint32(bytes, offset);
            offset += 4;
            ensureAvailable(bytes, offset, session_id_size, "session id");
            frame.session_id =
                std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(offset + session_id_size));
            offset += session_id_size;
        }
    }

    ensureAvailable(bytes, offset, 4, "payload size");
    const std::uint32_t payload_size = readUint32(bytes, offset);
    offset += 4;
    ensureAvailable(bytes, offset, payload_size, "payload");
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
    offset += payload_size;

    if (offset != bytes.size()) {
        throw std::invalid_argument("火山 realtime frame 包含未声明的多余字节。");
    }

    return frame;
}

std::string volcRealtimeEventIdToKey(VolcRealtimeEventId event_id) {
    switch (event_id) {
    case VolcRealtimeEventId::kStartConnection:
        return "start_connection";
    case VolcRealtimeEventId::kFinishConnection:
        return "finish_connection";
    case VolcRealtimeEventId::kStartSession:
        return "start_session";
    case VolcRealtimeEventId::kFinishSession:
        return "finish_session";
    case VolcRealtimeEventId::kTaskRequest:
        return "task_request";
    case VolcRealtimeEventId::kChatTextQuery:
        return "chat_text_query";
    case VolcRealtimeEventId::kConnectionStarted:
        return "connection_started";
    case VolcRealtimeEventId::kConnectionFailed:
        return "connection_failed";
    case VolcRealtimeEventId::kConnectionFinished:
        return "connection_finished";
    case VolcRealtimeEventId::kSessionStarted:
        return "session_started";
    case VolcRealtimeEventId::kSessionFinished:
        return "session_finished";
    case VolcRealtimeEventId::kSessionFailed:
        return "session_failed";
    case VolcRealtimeEventId::kTtsResponse:
        return "tts_response";
    case VolcRealtimeEventId::kAsrResponse:
        return "asr_response";
    case VolcRealtimeEventId::kAsrEnded:
        return "asr_ended";
    case VolcRealtimeEventId::kChatResponse:
        return "chat_response";
    case VolcRealtimeEventId::kChatTextQueryConfirmed:
        return "chat_text_query_confirmed";
    case VolcRealtimeEventId::kChatEnded:
        return "chat_ended";
    case VolcRealtimeEventId::kDialogCommonError:
        return "dialog_common_error";
    default:
        return "volc_event_" + std::to_string(static_cast<std::int32_t>(event_id));
    }
}

} // namespace services
} // namespace interview
