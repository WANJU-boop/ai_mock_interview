#include "services/volc_realtime_client_adapter.h"

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace interview {
namespace services {

namespace {

common::RealtimeEvent makeEvent(common::RealtimeEventType type) {
    common::RealtimeEvent event;
    event.type = type;
    return event;
}

common::RealtimeEvent makeTextEvent(common::RealtimeEventType type, const std::string& text) {
    common::RealtimeEvent event;
    event.type = type;
    event.text = text;
    return event;
}

common::RealtimeEvent makeErrorEvent(const std::string& message) {
    common::RealtimeEvent event;
    event.type = common::RealtimeEventType::kError;
    event.error_message = message;
    return event;
}

nlohmann::json parsePayloadJson(const VolcRealtimeFrame& frame) {
    try {
        return nlohmann::json::parse(std::string(frame.payload.begin(), frame.payload.end()));
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("无法解析火山 realtime payload：" + std::string(error.what()));
    }
}

std::string stringFieldOrEmpty(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        return "";
    }

    return object.at(key).get<std::string>();
}

common::RealtimeEvent mapAsrResponse(const VolcRealtimeFrame& frame) {
    const nlohmann::json payload = parsePayloadJson(frame);
    if (!payload.contains("results") || !payload.at("results").is_array() ||
        payload.at("results").empty() || !payload.at("results").front().is_object()) {
        return makeErrorEvent("火山 ASRResponse 缺少 results 数组。");
    }

    const nlohmann::json& first_result = payload.at("results").front();
    const std::string text = stringFieldOrEmpty(first_result, "text");
    const bool is_interim = first_result.value("is_interim", false);
    return makeTextEvent(is_interim ? common::RealtimeEventType::kTranscriptPartial
                                    : common::RealtimeEventType::kTranscriptFinal,
                         text);
}

common::RealtimeEvent mapChatResponse(const VolcRealtimeFrame& frame) {
    const nlohmann::json payload = parsePayloadJson(frame);
    return makeTextEvent(common::RealtimeEventType::kInterviewerText,
                         stringFieldOrEmpty(payload, "content"));
}

common::RealtimeEvent mapDialogError(const VolcRealtimeFrame& frame) {
    const nlohmann::json payload = parsePayloadJson(frame);
    const std::string message = stringFieldOrEmpty(payload, "message");
    if (!message.empty()) {
        return makeErrorEvent(message);
    }

    return makeErrorEvent(stringFieldOrEmpty(payload, "error"));
}

} // namespace

std::optional<common::RealtimeEvent>
mapVolcRealtimeFrameToRealtimeEvent(const VolcRealtimeFrame& frame) {
    if (frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        common::RealtimeEvent event = makeErrorEvent("火山 realtime 返回错误 frame。");
        event.payload = frame.payload;
        return event;
    }

    if (!frame.event_id.has_value()) {
        return std::nullopt;
    }

    try {
        switch (*frame.event_id) {
        case VolcRealtimeEventId::kConnectionStarted:
        case VolcRealtimeEventId::kSessionStarted:
            return makeEvent(common::RealtimeEventType::kConnected);
        case VolcRealtimeEventId::kConnectionFailed:
        case VolcRealtimeEventId::kSessionFailed:
        case VolcRealtimeEventId::kDialogCommonError:
            return mapDialogError(frame);
        case VolcRealtimeEventId::kConnectionFinished:
        case VolcRealtimeEventId::kSessionFinished:
            return makeEvent(common::RealtimeEventType::kClosed);
        case VolcRealtimeEventId::kAsrResponse:
            return mapAsrResponse(frame);
        case VolcRealtimeEventId::kChatResponse:
            return mapChatResponse(frame);
        case VolcRealtimeEventId::kTtsResponse: {
            common::RealtimeEvent event = makeEvent(common::RealtimeEventType::kInterviewerText);
            event.payload = frame.payload;
            return event;
        }
        default:
            return std::nullopt;
        }
    } catch (const std::exception& error) {
        return makeErrorEvent(error.what());
    }
}

VolcRealtimeClientAdapter::VolcRealtimeClientAdapter(
    VolcRealtimeClientConfig config, std::shared_ptr<IVolcRealtimeTransport> transport)
    : client_(std::move(config), std::move(transport)) {}

bool VolcRealtimeClientAdapter::connect() {
    try {
        client_.connect();
        client_.startConnection();
        client_.receiveUntilEvent(VolcRealtimeEventId::kConnectionStarted);
        client_.startTextSession();
        client_.receiveUntilEvent(VolcRealtimeEventId::kSessionStarted);
        connected_ = true;
        closed_ = false;
        pending_events_.push_back(makeEvent(common::RealtimeEventType::kConnected));
        return true;
    } catch (const std::exception&) {
        connected_ = false;
        closed_ = true;
        return false;
    }
}

bool VolcRealtimeClientAdapter::hasNextEvent() const {
    return connected_ && !closed_;
}

common::RealtimeEvent VolcRealtimeClientAdapter::receiveNextEvent() {
    if (!pending_events_.empty()) {
        common::RealtimeEvent event = pending_events_.front();
        pending_events_.pop_front();
        return event;
    }

    for (;;) {
        std::optional<common::RealtimeEvent> event =
            mapVolcRealtimeFrameToRealtimeEvent(client_.receiveFrame());
        if (!event.has_value()) {
            continue;
        }

        if (event->type == common::RealtimeEventType::kClosed ||
            event->type == common::RealtimeEventType::kError) {
            closed_ = true;
        }
        return *event;
    }
}

bool VolcRealtimeClientAdapter::sendInterviewerText(const std::string& text) {
    try {
        client_.sendChatTtsText(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void VolcRealtimeClientAdapter::close() {
    if (!connected_ || closed_) {
        closed_ = true;
        return;
    }

    try {
        // 关闭阶段的目标是释放连接；finish 包失败时仍要继续 close 底层传输，避免资源悬挂。
        client_.finishSession();
        client_.finishConnection();
        client_.close();
    } catch (const std::exception&) {
        try {
            client_.close();
        } catch (const std::exception&) {
        }
    }
    closed_ = true;
}

} // namespace services
} // namespace interview
