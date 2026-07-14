#include "services/realtime/volc/volc_realtime_client.h"

#include "common/logger.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace interview {
namespace services {

namespace {

std::vector<std::uint8_t> toBytes(const std::string& text) {
    // JSON payload 最终要进入二进制 frame，所以这里把 std::string 按原始 UTF-8 字节复制到 vector。
    // 不做编码转换，避免中文内容被破坏。
    return {text.begin(), text.end()};
}

nlohmann::json buildStartSessionPayload(const VolcRealtimeRuntimeConfig& config) {
    // 无论文本还是 audio 模式，都显式传 ASR/TTS 的 PCM 约定，避免服务端用默认采样率解释音频。
    // input_mod=text 时 ASR audio_config 暂不消费，但保留同一配置形状可避免两种模式长期漂移。
    return {{"asr",
             {{"extra", nlohmann::json::object()},
              {"audio_config",
               {{"channel", config.capture_channels},
                {"format", "pcm_s16le"},
                {"sample_rate", config.capture_sample_rate_hz}}}}},
            {"dialog",
             {{"extra",
               {{"input_mod", config.input_mod},
                {"model", config.model},
                {"strict_audit", config.strict_audit},
                {"enable_volc_websearch", config.enable_volc_websearch}}}}},
            {"tts",
             {{"speaker", config.speaker},
              {"extra", nlohmann::json::object()},
              {"audio_config",
               {{"channel", config.tts_channels},
                {"format", config.tts_audio_format},
                {"sample_rate", config.tts_sample_rate_hz}}}}}};
}

std::string buildTextQueryPayload(const std::string& content) {
    // ChatTextQuery 的 payload 很小，当前只传 content。
    // 如果后续要加上下文、RAG 或 conversation id，应在这里集中扩展并补测试。
    return nlohmann::json{{"content", content}}.dump();
}

} // namespace

VolcRealtimeClient::VolcRealtimeClient(VolcRealtimeRuntimeConfig config,
                                       std::shared_ptr<IVolcRealtimeTransport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {
    // 构造阶段就校验配置，能让单元测试和手动 demo 在联网前失败，
    // 不把“缺少 access key / session_id”这种本地问题拖到服务端鉴权错误里。
    validateConfig();
    if (transport_ == nullptr) {
        throw std::runtime_error("VolcRealtimeClient 要求 transport 不能为空。");
    }
}

void VolcRealtimeClient::connect() {
    // connect 只负责 WebSocket 握手，不发送火山业务事件。
    // 这样 StartConnection / StartSession 的顺序可以被测试单独验证。
    VolcRealtimeConnectionRequest request;
    request.url = config_.endpoint;
    request.timeout_ms = config_.timeout_ms;
    // 这些 header 是火山鉴权必需字段。不要在日志中打印 request.headers，
    // 因为 X-Api-Access-Key 是真实密钥。
    request.headers.push_back({"X-Api-App-ID", config_.app_id});
    request.headers.push_back({"X-Api-Access-Key", config_.access_key});
    request.headers.push_back({"X-Api-Resource-Id", config_.resource_id});
    request.headers.push_back({"X-Api-App-Key", config_.app_key});
    if (!config_.connect_id.empty()) {
        request.headers.push_back({"X-Api-Connect-Id", config_.connect_id});
    }

    transport_->connect(request);
}

void VolcRealtimeClient::startConnection() {
    // StartConnection 是连接级事件，所以协议层不会写 session_id。
    sendJsonEvent(VolcRealtimeEventId::kStartConnection, "{}");
}

void VolcRealtimeClient::startSession() {
    // StartSession 是会话级事件，sendJsonEvent 会自动带上 config_.session_id。
    sendJsonEvent(VolcRealtimeEventId::kStartSession, buildStartSessionPayload(config_).dump());
}

void VolcRealtimeClient::sendSayHello(const std::string& content) {
    if (content.empty()) {
        // 空文本对服务端没有意义，提前失败能让调用方明确知道是本地输入问题。
        throw std::runtime_error("SayHello 内容不能为空。");
    }

    sendJsonEvent(VolcRealtimeEventId::kSayHello, nlohmann::json{{"content", content}}.dump());
}

void VolcRealtimeClient::sendChatTtsText(const std::string& content) {
    if (content.empty()) {
        throw std::runtime_error("ChatTTSText 内容不能为空。");
    }

    // ChatTTSText 使用 start/end 两个包表达一次完整的客户端指定文本合成请求。
    // 第一包携带文本内容并标记 start=true；第二包用 end=true 告诉服务端这段文本已经结束。
    // 这样以后接流式 TTS 时，也能沿用“开始包 + 若干内容包 + 结束包”的形状。
    sendJsonEvent(VolcRealtimeEventId::kChatTtsText,
                  nlohmann::json{{"start", true}, {"content", content}, {"end", false}}.dump());
    sendJsonEvent(VolcRealtimeEventId::kChatTtsText,
                  nlohmann::json{{"start", false}, {"content", ""}, {"end", true}}.dump());
}

void VolcRealtimeClient::sendTextQuery(const std::string& content) {
    if (content.empty()) {
        // ChatTextQuery
        // 表示候选人输入。如果允许空字符串，会让后续评分和追问逻辑难以区分“没说话”和“网络问题”。
        throw std::runtime_error("ChatTextQuery 内容不能为空。");
    }

    sendJsonEvent(VolcRealtimeEventId::kChatTextQuery, buildTextQueryPayload(content));
}

void VolcRealtimeClient::sendAudioPcm(const std::vector<std::uint8_t>& pcm_s16le) {
    if (config_.input_mod != "keep_alive" && config_.input_mod != "push_to_talk" &&
        config_.input_mod != "audio_file") {
        throw std::runtime_error("当前 realtime input_mod 不能发送候选人 PCM。");
    }
    if (pcm_s16le.empty() || pcm_s16le.size() % 2 != 0) {
        // s16le 每个采样恰好两个字节。奇数字节说明上游 PCM 块已经损坏，不能交给服务端猜测。
        throw std::runtime_error("候选人 PCM 必须是非空且字节数为偶数的 s16le 数据。");
    }

    // Realtime Dialogue 的麦克风包必须携带 TaskRequest(200) 和 session_id。
    // 旧实现误用了 sequence flag，服务端无法把 PCM 归属到当前会话，因此不会产生 ASR 事件。
    VolcRealtimeFrame frame;
    frame.message_type = VolcRealtimeMessageType::kAudioOnlyRequest;
    frame.flag = VolcRealtimeMessageFlag::kEvent;
    frame.serialization = VolcRealtimeSerialization::kRaw;
    frame.compression = VolcRealtimeCompression::kNone;
    frame.event_id = VolcRealtimeEventId::kTaskRequest;
    frame.session_id = config_.session_id;
    frame.payload = pcm_s16le;
    transport_->sendBinary(encodeVolcRealtimeFrame(frame));
}

VolcRealtimeFrame VolcRealtimeClient::receiveFrame() {
    // transport 只返回 WebSocket binary message；协议合法性由 decodeVolcRealtimeFrame 负责。
    VolcRealtimeFrame frame = decodeVolcRealtimeFrame(transport_->receiveBinary());
    if (frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        // 供应商错误 frame 表示本轮调用已经失败，底层 client 用异常暴露；
        // adapter 层会再把异常转换成项目内部 kError 事件。
        const std::string payload(frame.payload.begin(), frame.payload.end());
        throw std::runtime_error(
            "火山 realtime 返回错误 frame，code=" + std::to_string(frame.code.value_or(0)) +
            "，payload=" + payload.substr(0, 1024));
    }
    if (frame.event_id.has_value()) {
        // 只记录事件类型和长度，不记录 ASR 文本、候选人回答或 TTS 字节。
        // TTSResponse 数量较多，降为 debug，避免正常面试日志被 PCM 块刷屏。
        if (*frame.event_id == VolcRealtimeEventId::kTtsResponse) {
            LOG_DEBUG("收到火山事件 {} payload_bytes={}", volcRealtimeEventIdToKey(*frame.event_id),
                      frame.payload.size());
        } else {
            LOG_INFO("收到火山事件 {} payload_bytes={}", volcRealtimeEventIdToKey(*frame.event_id),
                     frame.payload.size());
        }
    }
    return frame;
}

bool VolcRealtimeClient::hasPendingFrame() const {
    return transport_->hasPendingMessage();
}

std::vector<VolcRealtimeFrame> VolcRealtimeClient::receiveUntilEvent(VolcRealtimeEventId event_id) {
    std::vector<VolcRealtimeFrame> frames;
    for (;;) {
        // 真实服务端可能先返回若干中间事件，例如 ChatResponse、UsageResponse，
        // 所以这里不能只读一帧，而是一直读到目标 event_id。
        VolcRealtimeFrame frame = receiveFrame();
        const bool matched = frame.event_id.has_value() && *frame.event_id == event_id;
        frames.push_back(std::move(frame));
        if (matched) {
            return frames;
        }
    }
}

std::vector<VolcRealtimeFrame> VolcRealtimeClient::receiveUntilChatEnded() {
    return receiveUntilEvent(VolcRealtimeEventId::kChatEnded);
}

void VolcRealtimeClient::finishSession() {
    sendJsonEvent(VolcRealtimeEventId::kFinishSession, "{}");
}

void VolcRealtimeClient::finishConnection() {
    sendJsonEvent(VolcRealtimeEventId::kFinishConnection, "{}");
}

void VolcRealtimeClient::close() {
    transport_->close();
}

void VolcRealtimeClient::sendJsonEvent(VolcRealtimeEventId event_id, const std::string& payload) {
    // 这个 helper 是所有“客户端发 JSON 事件”的唯一出口。
    // 统一出口可以保证 message_type、flag、serialization、compression 的协议字段不会到处重复。
    VolcRealtimeFrame frame;
    frame.message_type = VolcRealtimeMessageType::kFullClientRequest;
    frame.flag = VolcRealtimeMessageFlag::kEvent;
    frame.serialization = VolcRealtimeSerialization::kJson;
    frame.compression = VolcRealtimeCompression::kNone;
    frame.event_id = event_id;
    frame.payload = toBytes(payload);
    if (isVolcRealtimeSessionEvent(event_id)) {
        // 会话级事件由协议层强制要求 session_id。这里集中填入，调用 public 方法时不用重复传。
        frame.session_id = config_.session_id;
    }

    // 先编码为火山二进制 frame，再交给 transport 发送。client 不直接碰 Boost.Beast socket。
    transport_->sendBinary(encodeVolcRealtimeFrame(frame));
    // 只记录事件 ID 和字节数，ChatTTSText 的面试问题正文不进入日志。
    LOG_INFO("发送火山事件 {} payload_bytes={}", volcRealtimeEventIdToKey(event_id),
             payload.size());
}

void VolcRealtimeClient::validateConfig() const {
    // 这些校验只检查本地必需字段，不验证密钥是否真的有效。
    // 真正的鉴权失败只能由火山服务端在 StartConnection/StartSession 阶段返回。
    if (config_.endpoint.empty()) {
        throw std::runtime_error("火山 realtime endpoint 不能为空。");
    }
    if (config_.app_id.empty()) {
        throw std::runtime_error("火山 realtime app_id 不能为空。");
    }
    if (config_.access_key.empty()) {
        throw std::runtime_error("火山 realtime access_key 不能为空。");
    }
    if (config_.resource_id.empty()) {
        throw std::runtime_error("火山 realtime resource_id 不能为空。");
    }
    if (config_.app_key.empty()) {
        throw std::runtime_error("火山 realtime app_key 不能为空。");
    }
    if (config_.session_id.empty()) {
        throw std::runtime_error("火山 realtime session_id 不能为空。");
    }
    if (config_.model.empty()) {
        throw std::runtime_error("火山 realtime model 不能为空。");
    }
    if (config_.input_mod != "text" && config_.input_mod != "keep_alive" &&
        config_.input_mod != "push_to_talk" && config_.input_mod != "audio_file") {
        throw std::runtime_error(
            "火山 realtime input_mod 只能是 text、keep_alive、push_to_talk 或 audio_file。");
    }
    if (config_.speaker.empty()) {
        throw std::runtime_error("火山 realtime speaker 不能为空。");
    }
    if (config_.tts_audio_format.empty()) {
        throw std::runtime_error("火山 realtime TTS audio format 不能为空。");
    }
    if (config_.tts_sample_rate_hz <= 0 || config_.tts_channels <= 0) {
        throw std::runtime_error("火山 realtime TTS sample rate 和 channels 必须是正数。");
    }
    if (config_.capture_sample_rate_hz <= 0 || config_.capture_channels <= 0 ||
        config_.frames_per_buffer <= 0) {
        throw std::runtime_error("火山 realtime capture PCM 配置必须是正数。");
    }
    if (config_.timeout_ms <= 0) {
        throw std::runtime_error("火山 realtime timeout_ms 必须是正数。");
    }
}

} // namespace services
} // namespace interview
