#include "services/realtime/volc/volc_realtime_protocol.h"

#include <cstddef>
#include <limits>
#include <stdexcept>

namespace interview {
namespace services {

namespace {

// 火山协议固定 4 字节基础 header：
// byte0 = version + header size，byte1 = message type + flag，
// byte2 = serialization + compression，byte3 当前保留。
// 这些常量只属于火山协议层，业务层不应该依赖它们。
constexpr std::uint8_t kProtocolVersion = 0x1;
constexpr std::uint8_t kHeaderSizeInWords = 0x1;
constexpr std::size_t kFixedHeaderSize = 4;
constexpr std::uint8_t kNibbleMask = 0x0F;

void appendInt32(std::vector<std::uint8_t>& bytes, std::int32_t value) {
    // 火山协议里的 int32 使用网络字节序（big-endian，大端序）。
    // C++ 机器本地可能是小端，所以不能直接把 int32 内存拷贝进 vector。
    const std::uint32_t raw = static_cast<std::uint32_t>(value);
    bytes.push_back(static_cast<std::uint8_t>((raw >> 24) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 16) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xFF));
}

std::uint32_t readUint32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    // 解码时按大端序手动拼回 uint32，和 appendInt32 保持完全对称。
    // 调用前必须先 ensureAvailable，否则 offset + 3 可能越界。
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
    // 所有读取前都经过这个边界检查，避免供应商返回坏包或测试构造坏包时读出 vector 范围。
    // offset > size 也要单独判断，否则 size - offset 会发生无符号下溢。
    if (offset > bytes.size() || required_size > bytes.size() - offset) {
        throw std::invalid_argument(std::string(field_name) + " 超出火山 realtime frame 边界。");
    }
}

void appendSizedBytes(std::vector<std::uint8_t>& bytes, const std::vector<std::uint8_t>& payload,
                      const char* field_name) {
    // 火山 optional 字符串和 payload 都采用“4 字节长度 + 数据”格式。
    // 这里先检查长度能否放进 uint32，避免非常大的 vector 被截断成错误长度。
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码火山 frame。");
    }

    appendInt32(bytes, static_cast<std::int32_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void appendSizedString(std::vector<std::uint8_t>& bytes, const std::string& value,
                       const char* field_name) {
    // session_id 本质上也是一段 UTF-8/ASCII 字节；协议层只关心长度和字节，不关心语义。
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument(std::string(field_name) + " 太大，无法编码火山 frame。");
    }

    appendInt32(bytes, static_cast<std::int32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

VolcRealtimeMessageType decodeMessageType(std::uint8_t raw_value) {
    // 显式 switch 的好处是：文档外的新类型不会静默落到某个默认值。
    // 真实集成时一旦火山协议升级，测试或手动调试会尽早报错。
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
    // 只允许当前项目已登记的 event id 进入后续流程。
    // 新增火山事件时，应先补枚举、测试和 adapter 映射，再让它通过解码。
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
    // 音频流式包会用 sequence 来标识流片段顺序；文本事件当前主要走 kEvent。
    // 保留这个判断是为了后面接 TaskRequest/音频流时不用重写 frame 主结构。
    return flag == VolcRealtimeMessageFlag::kPositiveSequence ||
           flag == VolcRealtimeMessageFlag::kLastNegativeSequence;
}

} // namespace

bool isVolcRealtimeConnectEvent(VolcRealtimeEventId event_id) {
    // Connection 级事件属于一条 WebSocket 连接本身，不属于某个 session，
    // 所以编码时不会在 optional 区域写 session_id。
    return event_id == VolcRealtimeEventId::kStartConnection ||
           event_id == VolcRealtimeEventId::kFinishConnection ||
           event_id == VolcRealtimeEventId::kConnectionStarted ||
           event_id == VolcRealtimeEventId::kConnectionFailed ||
           event_id == VolcRealtimeEventId::kConnectionFinished;
}

bool isVolcRealtimeSessionEvent(VolcRealtimeEventId event_id) {
    // 当前除 Connection 级事件外，其余事件都按 Session 级处理。
    // 这样新增 Chat/TTS/ASR 事件时默认要求 session_id，更不容易漏掉会话归属。
    return !isVolcRealtimeConnectEvent(event_id);
}

std::vector<std::uint8_t> encodeVolcRealtimeFrame(const VolcRealtimeFrame& frame) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kFixedHeaderSize + frame.session_id.size() + frame.payload.size() + 16);

    // 前 4 字节是固定 header。每个字节拆成高 4 bit 和低 4 bit，
    // 所以这里用左移和掩码组合，而不是写多个独立字节。
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
        // 错误 frame 的 optional 区域是 error code，不是 event id。
        // 这里强制要求 code，是为了避免上层误造出“看起来像错误但没有错误码”的包。
        if (!frame.code.has_value()) {
            throw std::invalid_argument("火山错误 frame 必须携带 code。");
        }
        appendInt32(bytes, *frame.code);
    } else if (flagHasSequence(frame.flag)) {
        // sequence frame 的 optional 区域是流片段序号，后续音频流式传输会用到。
        if (!frame.sequence.has_value()) {
            throw std::invalid_argument("火山 sequence frame 必须携带 sequence。");
        }
        appendInt32(bytes, *frame.sequence);
    } else if (frame.flag == VolcRealtimeMessageFlag::kEvent) {
        // 事件 frame 的 optional 区域先写 event id；如果是 Session 级事件，再写 session_id。
        if (!frame.event_id.has_value()) {
            throw std::invalid_argument("火山事件 frame 必须携带 event id。");
        }
        appendInt32(bytes, static_cast<std::int32_t>(*frame.event_id));
        if (isVolcRealtimeSessionEvent(*frame.event_id)) {
            // StartConnection 不带 session_id，但 StartSession/ChatTextQuery 等必须带。
            // 这个校验能在本地测试阶段拦住“服务端返回缺 session”这类低级问题。
            if (frame.session_id.empty()) {
                throw std::invalid_argument("火山 Session 事件必须携带 session id。");
            }
            appendSizedString(bytes, frame.session_id, "session_id");
        } else if (!frame.connect_id.empty()) {
            // Connect ID 在连接级事件中是可选字段。StartConnection 通常不写，
            // 但服务端 ConnectionStarted 可能回传，因此协议对象必须能完整表示。
            appendSizedString(bytes, frame.connect_id, "connect_id");
        }
    }

    appendSizedBytes(bytes, frame.payload, "payload");
    return bytes;
}

VolcRealtimeFrame decodeVolcRealtimeFrame(const std::vector<std::uint8_t>& bytes) {
    // 解码顺序必须和编码顺序一致：固定 header -> optional 区域 -> payload。
    // 每一步都检查长度，避免坏包导致越界读取或把后续字段错位解析。
    ensureAvailable(bytes, 0, kFixedHeaderSize, "header");

    const std::uint8_t protocol_version = (bytes[0] >> 4) & kNibbleMask;
    const std::uint8_t header_size_words = bytes[0] & kNibbleMask;
    if (protocol_version != kProtocolVersion || header_size_words != kHeaderSizeInWords) {
        // 目前只支持 4 字节基础 header。遇到不同版本先失败，后续再按文档补新版本解析。
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
        // 错误 frame：optional 区域固定读取 4 字节错误码。
        ensureAvailable(bytes, offset, 4, "error code");
        frame.code = readInt32(bytes, offset);
        offset += 4;
    } else if (flagHasSequence(frame.flag)) {
        // sequence frame：optional 区域固定读取 4 字节序号。
        ensureAvailable(bytes, offset, 4, "sequence");
        frame.sequence = readInt32(bytes, offset);
        offset += 4;
    } else if (frame.flag == VolcRealtimeMessageFlag::kEvent) {
        // 事件 frame：先读取 event id，再根据事件级别决定是否读取 session_id。
        ensureAvailable(bytes, offset, 4, "event id");
        frame.event_id = decodeEventId(readInt32(bytes, offset));
        offset += 4;

        if (isVolcRealtimeSessionEvent(*frame.event_id)) {
            // session_id 本身也是“长度 + 字节”；不能直接读到 payload 前，
            // 因为 payload 也可能是任意二进制，必须依赖长度字段切分。
            ensureAvailable(bytes, offset, 4, "session id size");
            const std::uint32_t session_id_size = readUint32(bytes, offset);
            offset += 4;
            ensureAvailable(bytes, offset, session_id_size, "session id");
            frame.session_id =
                std::string(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(offset + session_id_size));
            offset += session_id_size;
        } else {
            // Connect 级事件的 connect_id 是可选的，协议没有单独 flag。
            // 如果当前长度字段直接覆盖到 frame 末尾，它就是 payload size；
            // 否则只有“connect_id + 第二个长度字段 + payload”能严格消费全包时才按 ID 解析。
            ensureAvailable(bytes, offset, 4, "connect id or payload size");
            const std::uint32_t candidate_id_size = readUint32(bytes, offset);
            const std::size_t candidate_id_offset = offset + 4;
            if (candidate_id_size <= bytes.size() - candidate_id_offset) {
                const std::size_t candidate_payload_size_offset =
                    candidate_id_offset + candidate_id_size;
                if (candidate_payload_size_offset + 4 <= bytes.size()) {
                    const std::uint32_t candidate_payload_size =
                        readUint32(bytes, candidate_payload_size_offset);
                    if (candidate_payload_size <=
                            bytes.size() - candidate_payload_size_offset - 4 &&
                        candidate_payload_size_offset + 4 + candidate_payload_size ==
                            bytes.size()) {
                        frame.connect_id = std::string(
                            bytes.begin() + static_cast<std::ptrdiff_t>(candidate_id_offset),
                            bytes.begin() +
                                static_cast<std::ptrdiff_t>(candidate_payload_size_offset));
                        offset = candidate_payload_size_offset;
                    }
                }
            }
        }
    }

    ensureAvailable(bytes, offset, 4, "payload size");
    const std::uint32_t payload_size = readUint32(bytes, offset);
    offset += 4;
    // payload 不在协议层转成 string 或 JSON，是为了同时支持 Chat JSON 和 TTS 音频 bytes。
    ensureAvailable(bytes, offset, payload_size, "payload");
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                         bytes.begin() + static_cast<std::ptrdiff_t>(offset + payload_size));
    offset += payload_size;

    if (offset != bytes.size()) {
        // 如果还有多余字节，说明长度字段或解析逻辑不一致。严格失败比忽略尾部更安全。
        throw std::invalid_argument("火山 realtime frame 包含未声明的多余字节。");
    }

    return frame;
}

std::string volcRealtimeEventIdToKey(VolcRealtimeEventId event_id) {
    // 日志和测试使用稳定字符串比直接打印数字更容易读。
    // 未列出的合法事件仍返回 volc_event_<id>，避免日志完全丢失事件信息。
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
