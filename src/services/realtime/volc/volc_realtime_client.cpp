#include "services/realtime/volc/volc_realtime_client.h"

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

nlohmann::json buildStartSessionPayload(const VolcRealtimeClientConfig& config) {
    // 文本模式仍然显式传 asr.extra / tts.extra 空对象，避免服务端把 null 配置视为坏请求。
    // 这里同时保留 TTS audio_config，是因为即使
    // input_mod=text，服务端仍可能需要知道面试官文本如何合成语音。
    return {
        {"asr", {{"extra", nlohmann::json::object()}}},
        {"dialog",
         {{"extra",
           {{"input_mod", config.input_mod},
            {"model", config.model},
            {"strict_audit", true},
            {"enable_volc_websearch", false}}}}},
        {"tts",
         {{"speaker", config.speaker},
          {"extra", nlohmann::json::object()},
          {"audio_config", {{"channel", 1}, {"format", "pcm_s16le"}, {"sample_rate", 24000}}}}}};
}

std::string buildTextQueryPayload(const std::string& content) {
    // ChatTextQuery 的 payload 很小，当前只传 content。
    // 如果后续要加上下文、RAG 或 conversation id，应在这里集中扩展并补测试。
    return nlohmann::json{{"content", content}}.dump();
}

} // namespace

VolcRealtimeClient::VolcRealtimeClient(VolcRealtimeClientConfig config,
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

void VolcRealtimeClient::startTextSession() {
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

VolcRealtimeFrame VolcRealtimeClient::receiveFrame() {
    // transport 只返回 WebSocket binary message；协议合法性由 decodeVolcRealtimeFrame 负责。
    VolcRealtimeFrame frame = decodeVolcRealtimeFrame(transport_->receiveBinary());
    if (frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        // 供应商错误 frame 表示本轮调用已经失败，底层 client 用异常暴露；
        // adapter 层会再把异常转换成项目内部 kError 事件。
        throw std::runtime_error("火山 realtime 返回错误 frame。");
    }
    return frame;
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
    if (config_.input_mod != "text") {
        // 当前阶段只实现文本模式。audio 模式会涉及麦克风采集、音频编码、TaskRequest 流式发送，
        // 必须等 IAudioDevice/PortAudio 阶段完成后再打开。
        throw std::runtime_error("当前 VolcRealtimeClient 只实现 input_mod=text 文本模式。");
    }
    if (config_.timeout_ms <= 0) {
        throw std::runtime_error("火山 realtime timeout_ms 必须是正数。");
    }
}

} // namespace services
} // namespace interview
