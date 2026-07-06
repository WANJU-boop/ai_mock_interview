#include "services/volc_realtime_client.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace interview {
namespace services {

namespace {

std::vector<std::uint8_t> toBytes(const std::string& text) {
    return {text.begin(), text.end()};
}

nlohmann::json buildStartSessionPayload(const VolcRealtimeClientConfig& config) {
    // 文本模式仍然显式传 asr.extra / tts.extra 空对象，避免服务端把 null 配置视为坏请求。
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
    return nlohmann::json{{"content", content}}.dump();
}

} // namespace

VolcRealtimeClient::VolcRealtimeClient(VolcRealtimeClientConfig config,
                                       std::shared_ptr<IVolcRealtimeTransport> transport)
    : config_(std::move(config)), transport_(std::move(transport)) {
    validateConfig();
    if (transport_ == nullptr) {
        throw std::runtime_error("VolcRealtimeClient 要求 transport 不能为空。");
    }
}

void VolcRealtimeClient::connect() {
    VolcRealtimeConnectionRequest request;
    request.url = config_.endpoint;
    request.timeout_ms = config_.timeout_ms;
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
    sendJsonEvent(VolcRealtimeEventId::kStartConnection, "{}");
}

void VolcRealtimeClient::startTextSession() {
    sendJsonEvent(VolcRealtimeEventId::kStartSession, buildStartSessionPayload(config_).dump());
}

void VolcRealtimeClient::sendTextQuery(const std::string& content) {
    if (content.empty()) {
        throw std::runtime_error("ChatTextQuery 内容不能为空。");
    }

    sendJsonEvent(VolcRealtimeEventId::kChatTextQuery, buildTextQueryPayload(content));
}

VolcRealtimeFrame VolcRealtimeClient::receiveFrame() {
    VolcRealtimeFrame frame = decodeVolcRealtimeFrame(transport_->receiveBinary());
    if (frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        throw std::runtime_error("火山 realtime 返回错误 frame。");
    }
    return frame;
}

std::vector<VolcRealtimeFrame> VolcRealtimeClient::receiveUntilEvent(VolcRealtimeEventId event_id) {
    std::vector<VolcRealtimeFrame> frames;
    for (;;) {
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
    VolcRealtimeFrame frame;
    frame.message_type = VolcRealtimeMessageType::kFullClientRequest;
    frame.flag = VolcRealtimeMessageFlag::kEvent;
    frame.serialization = VolcRealtimeSerialization::kJson;
    frame.compression = VolcRealtimeCompression::kNone;
    frame.event_id = event_id;
    frame.payload = toBytes(payload);
    if (isVolcRealtimeSessionEvent(event_id)) {
        frame.session_id = config_.session_id;
    }

    transport_->sendBinary(encodeVolcRealtimeFrame(frame));
}

void VolcRealtimeClient::validateConfig() const {
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
        throw std::runtime_error("当前 VolcRealtimeClient 只实现 input_mod=text 文本模式。");
    }
    if (config_.timeout_ms <= 0) {
        throw std::runtime_error("火山 realtime timeout_ms 必须是正数。");
    }
}

} // namespace services
} // namespace interview
