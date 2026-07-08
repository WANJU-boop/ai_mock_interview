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

// payload 的序列化方式。JSON 表示 payload 是文本 JSON；Raw 表示 payload 是原始二进制，
// 例如后续 TTS 音频数据。协议层只记录类型，不在这里解析业务内容。
enum class VolcRealtimeSerialization : std::uint8_t {
    kRaw = 0x0,
    kJson = 0x1,
};

// payload 的压缩方式。当前项目只真正发送未压缩数据；保留 gzip 枚举是为了能识别服务端
// 或后续音频阶段的压缩 frame，而不是把未知 header 当成普通 payload 误处理。
enum class VolcRealtimeCompression : std::uint8_t {
    kNone = 0x0,
    kGzip = 0x1,
};

// 火山实时对话事件 ID。只列入当前项目会用到的连接、会话、ASR、Chat、TTS 和错误事件。
enum class VolcRealtimeEventId : std::int32_t {
    // Client -> Server：连接级事件，不携带 session_id。
    kStartConnection = 1,
    kFinishConnection = 2,

    // Client -> Server：会话级事件，需要携带 session_id。
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

    // Server -> Client：连接级响应，用于确认 WSS 连接里的火山业务连接状态。
    kConnectionStarted = 50,
    kConnectionFailed = 51,
    kConnectionFinished = 52,

    // Server -> Client：会话、ASR、TTS、Chat 响应，adapter 会挑选其中一部分映射到项目内部事件。
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
    // header 第 2 字节左半字节：决定这是客户端请求、服务端响应、音频包还是错误包。
    VolcRealtimeMessageType message_type = VolcRealtimeMessageType::kFullClientRequest;
    // header 第 2 字节右半字节：决定 optional 区域里接下来是 event id、sequence 还是 error code。
    VolcRealtimeMessageFlag flag = VolcRealtimeMessageFlag::kEvent;
    // header 第 3 字节左半字节：告诉上层 payload 应按 JSON 文本还是原始字节理解。
    VolcRealtimeSerialization serialization = VolcRealtimeSerialization::kJson;
    // header 第 3 字节右半字节：当前只发送未压缩 payload；解码时仍保留字段，便于发现协议变化。
    VolcRealtimeCompression compression = VolcRealtimeCompression::kNone;
    // 错误 frame 使用 code；普通事件不应填这个字段。
    std::optional<std::int32_t> code;
    // 流式音频 sequence 包使用 sequence；文本模式事件当前不会使用。
    std::optional<std::int32_t> sequence;
    // 事件 frame 使用 event_id；没有 event_id 的 frame 通常是 sequence 或错误 frame。
    std::optional<VolcRealtimeEventId> event_id;
    // Session 级事件必须携带 session_id；Connection 级事件不携带。
    std::string session_id;
    // payload 始终是原始字节，避免协议层错误地假设所有 payload 都是 JSON。
    std::vector<std::uint8_t> payload;
};

// 编码火山 realtime frame。当前不支持在 frame optional 中携带 connect id，
// 连接追踪使用 WebSocket 握手 header 的 X-Api-Connect-Id。
// 调用方只需要填好 VolcRealtimeFrame；本函数负责按火山文档写 header、optional 字段和 payload 长度。
std::vector<std::uint8_t> encodeVolcRealtimeFrame(const VolcRealtimeFrame& frame);

// 解码火山 realtime frame。协议结构错误、未知枚举、长度越界都会抛出 std::invalid_argument。
// 这里选择“严格失败”，是为了供应商协议变化时尽早暴露问题，而不是让错误字节进入业务状态机。
VolcRealtimeFrame decodeVolcRealtimeFrame(const std::vector<std::uint8_t>& bytes);

// 判断事件是否属于 Session 级别；Session 事件在火山协议 optional 中必须携带 session id。
bool isVolcRealtimeSessionEvent(VolcRealtimeEventId event_id);

// 判断事件是否属于 Connect 级别；当前只用于校验 StartConnection/ConnectionStarted 等连接事件。
bool isVolcRealtimeConnectEvent(VolcRealtimeEventId event_id);

// 稳定事件 key 便于日志、测试和后续供应商事件到项目内部事件的映射。
std::string volcRealtimeEventIdToKey(VolcRealtimeEventId event_id);

} // namespace services
} // namespace interview
