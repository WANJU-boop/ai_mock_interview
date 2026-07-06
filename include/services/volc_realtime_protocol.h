#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace interview {
namespace services {

// 火山 Realtime WebSocket 的消息类型，来自供应商二进制协议 header 第 2 个字节左半字节。
// 这些值只描述火山私有 framing，不直接暴露给 session 层。
enum class VolcRealtimeMessageType : std::uint8_t {
    kFullClientRequest = 0x1,
    kAudioOnlyRequest = 0x2,
    kFullServerResponse = 0x9,
    kAudioOnlyResponse = 0xB,
    kErrorInformation = 0xF,
};

// 火山协议的 message type specific flags。当前先覆盖事件包和错误包，
// sequence 包保留枚举是为了后续音频流式包扩展时不用重命名协议层类型。
enum class VolcRealtimeMessageFlag : std::uint8_t {
    kNoSequence = 0x0,
    kPositiveSequence = 0x1,
    kLastNoSequence = 0x2,
    kLastNegativeSequence = 0x3,
    kEvent = 0x4,
    kError = 0xF,
};

enum class VolcRealtimeSerialization : std::uint8_t {
    kRaw = 0x0,
    kJson = 0x1,
};

enum class VolcRealtimeCompression : std::uint8_t {
    kNone = 0x0,
    kGzip = 0x1,
};

// 火山实时对话事件 ID。只列入当前项目会用到的连接、会话、ASR、Chat、TTS 和错误事件。
enum class VolcRealtimeEventId : std::int32_t {
    kStartConnection = 1,
    kFinishConnection = 2,
    kStartSession = 100,
    kFinishSession = 102,
    kTaskRequest = 200,
    kUpdateConfig = 201,
    kSayHello = 300,
    kEndAsr = 400,
    kChatTtsText = 500,
    kChatTextQuery = 501,
    kChatRagText = 502,
    kConversationCreate = 510,
    kConversationUpdate = 511,
    kConversationRetrieve = 512,
    kConversationTruncate = 513,
    kConversationDelete = 514,
    kClientInterrupt = 515,

    kConnectionStarted = 50,
    kConnectionFailed = 51,
    kConnectionFinished = 52,
    kSessionStarted = 150,
    kSessionFinished = 152,
    kSessionFailed = 153,
    kUsageResponse = 154,
    kConfigUpdated = 251,
    kTtsSentenceStart = 350,
    kTtsSentenceEnd = 351,
    kTtsResponse = 352,
    kTtsEnded = 359,
    kAsrInfo = 450,
    kAsrResponse = 451,
    kAsrEnded = 459,
    kChatResponse = 550,
    kChatTextQueryConfirmed = 553,
    kChatEnded = 559,
    kConversationCreated = 567,
    kConversationUpdated = 568,
    kConversationRetrieved = 569,
    kConversationTruncated = 570,
    kConversationDeleted = 571,
    kDialogCommonError = 599,
};

// 解码后的火山 frame。payload 保持原始字节，由上层按 event_id 决定解析 JSON 或音频。
struct VolcRealtimeFrame {
    VolcRealtimeMessageType message_type = VolcRealtimeMessageType::kFullClientRequest;
    VolcRealtimeMessageFlag flag = VolcRealtimeMessageFlag::kEvent;
    VolcRealtimeSerialization serialization = VolcRealtimeSerialization::kJson;
    VolcRealtimeCompression compression = VolcRealtimeCompression::kNone;
    std::optional<std::int32_t> code;
    std::optional<std::int32_t> sequence;
    std::optional<VolcRealtimeEventId> event_id;
    std::string session_id;
    std::vector<std::uint8_t> payload;
};

// 编码火山 realtime frame。当前不支持在 frame optional 中携带 connect id，
// 连接追踪使用 WebSocket 握手 header 的 X-Api-Connect-Id。
std::vector<std::uint8_t> encodeVolcRealtimeFrame(const VolcRealtimeFrame& frame);

// 解码火山 realtime frame。协议结构错误、未知枚举、长度越界都会抛出 std::invalid_argument。
VolcRealtimeFrame decodeVolcRealtimeFrame(const std::vector<std::uint8_t>& bytes);

// 判断事件是否属于 Session 级别；Session 事件在火山协议 optional 中必须携带 session id。
bool isVolcRealtimeSessionEvent(VolcRealtimeEventId event_id);

// 判断事件是否属于 Connect 级别；当前只用于校验 StartConnection/ConnectionStarted 等连接事件。
bool isVolcRealtimeConnectEvent(VolcRealtimeEventId event_id);

// 稳定事件 key 便于日志、测试和后续供应商事件到项目内部事件的映射。
std::string volcRealtimeEventIdToKey(VolcRealtimeEventId event_id);

} // namespace services
} // namespace interview
