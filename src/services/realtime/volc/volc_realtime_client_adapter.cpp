#include "services/realtime/volc/volc_realtime_client_adapter.h"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>

namespace interview {
namespace services {

namespace {

common::RealtimeEvent makeEvent(common::RealtimeEventType type) {
    // 小工厂函数让映射代码只关注“事件类型是什么”，避免到处重复创建 RealtimeEvent。
    common::RealtimeEvent event;
    event.type = type;
    return event;
}

common::RealtimeEvent makeTextEvent(common::RealtimeEventType type, const std::string& text) {
    // 文本事件包括候选人 transcript 和面试官文本，两者都用 RealtimeEvent::text 承载。
    common::RealtimeEvent event;
    event.type = type;
    event.text = text;
    return event;
}

common::RealtimeEvent makeErrorEvent(const std::string& message) {
    // 项目内部统一用 kError + error_message 表达 realtime 层错误，
    // 这样 DialogOrchestrator 不需要认识供应商自己的错误 JSON 字段。
    common::RealtimeEvent event;
    event.type = common::RealtimeEventType::kError;
    event.error_message = message;
    return event;
}

std::vector<std::uint8_t> toLittleEndianPcmBytes(const AudioPcmChunk& chunk) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(chunk.samples.size() * 2);
    for (const std::int16_t sample : chunk.samples) {
        const std::uint16_t unsigned_sample = static_cast<std::uint16_t>(sample);
        // 显式按小端拆分，不能依赖运行机器的整数内存布局。
        bytes.push_back(static_cast<std::uint8_t>(unsigned_sample & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>((unsigned_sample >> 8) & 0xFF));
    }
    return bytes;
}

nlohmann::json parsePayloadJson(const VolcRealtimeFrame& frame) {
    try {
        // 只有明确知道 payload 是 JSON 的事件才会走这里。
        // TTS 音频 payload 不应该进入 JSON 解析，否则二进制音频会被误判成坏数据。
        return nlohmann::json::parse(std::string(frame.payload.begin(), frame.payload.end()));
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("无法解析火山 realtime payload：" + std::string(error.what()));
    }
}

std::string stringFieldOrEmpty(const nlohmann::json& object, const std::string& key) {
    // 供应商 payload 字段可能缺失或类型变化。这里返回空字符串，
    // 由调用方决定空文本是可接受的中间状态还是要变成错误事件。
    if (!object.contains(key) || !object.at(key).is_string()) {
        return "";
    }

    return object.at(key).get<std::string>();
}

common::RealtimeEvent mapAsrResponse(const VolcRealtimeFrame& frame) {
    // 火山 ASRResponse 的 results 里可能有多段识别结果。
    // 当前面试主流程先取第一段，形成“候选人当前一句回答”的最小闭环。
    const nlohmann::json payload = parsePayloadJson(frame);
    if (!payload.contains("results") || !payload.at("results").is_array() ||
        payload.at("results").empty() || !payload.at("results").front().is_object()) {
        // 缺 results 表示服务端响应结构和我们预期不一致，直接转内部错误事件，
        // 避免 session 把空文本当成候选人的真实回答。
        return makeErrorEvent("火山 ASRResponse 缺少 results 数组。");
    }

    const nlohmann::json& first_result = payload.at("results").front();
    const std::string text = stringFieldOrEmpty(first_result, "text");
    // is_interim=true 表示临时识别结果，只用于 UI 展示；final 才会触发面试评分和进入下一题。
    const bool is_interim = first_result.value("is_interim", false);
    return makeTextEvent(is_interim ? common::RealtimeEventType::kTranscriptPartial
                                    : common::RealtimeEventType::kTranscriptFinal,
                         text);
}

common::RealtimeEvent mapChatResponse(const VolcRealtimeFrame& frame) {
    // ChatResponse 是火山对话模型生成的文本，项目内部把它当成面试官文本事件。
    // 这里不做评分逻辑，评分仍然属于 session/interview 层。
    const nlohmann::json payload = parsePayloadJson(frame);
    return makeTextEvent(common::RealtimeEventType::kInterviewerText,
                         stringFieldOrEmpty(payload, "content"));
}

common::RealtimeEvent mapDialogError(const VolcRealtimeFrame& frame) {
    // 火山错误 payload 常见字段是 message，也可能是 error。
    // adapter 做兼容提取，业务层只看统一的 error_message。
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
    // 这个函数是“供应商事件 -> 项目事件”的唯一翻译入口。
    // 返回 nullopt 表示这个火山事件只是 ack/状态细节，DialogOrchestrator 不需要处理。
    if (frame.message_type == VolcRealtimeMessageType::kErrorInformation) {
        common::RealtimeEvent event = makeErrorEvent("火山 realtime 返回错误 frame。");
        // 原始 payload 保留下来，后续手动集成排查时可以查看供应商返回的完整错误内容。
        event.payload = frame.payload;
        return event;
    }

    if (!frame.event_id.has_value()) {
        // 没有 event_id 的 frame 不是当前业务层可处理的事件，例如后续音频 sequence 包。
        return std::nullopt;
    }

    try {
        switch (*frame.event_id) {
        case VolcRealtimeEventId::kConnectionStarted:
        case VolcRealtimeEventId::kSessionStarted:
            // 火山分 ConnectionStarted 和 SessionStarted；项目内部先统一成 kConnected，
            // 让 session 层只关心“realtime 能用了没有”。
            return makeEvent(common::RealtimeEventType::kConnected);
        case VolcRealtimeEventId::kConnectionFailed:
        case VolcRealtimeEventId::kSessionFailed:
        case VolcRealtimeEventId::kDialogCommonError:
            return mapDialogError(frame);
        case VolcRealtimeEventId::kConnectionFinished:
        case VolcRealtimeEventId::kSessionFinished:
            // 任一关闭事件都代表业务事件流应该收口。
            return makeEvent(common::RealtimeEventType::kClosed);
        case VolcRealtimeEventId::kAsrResponse:
            return mapAsrResponse(frame);
        case VolcRealtimeEventId::kChatResponse:
            return mapChatResponse(frame);
        case VolcRealtimeEventId::kTtsResponse: {
            // TTSResponse 可能是音频二进制。当前项目内部还没有音频播放边界，
            // 所以先把 payload 原样带出去，后续 IAudioDevice 接入时再消费。
            common::RealtimeEvent event = makeEvent(common::RealtimeEventType::kInterviewerText);
            event.payload = frame.payload;
            return event;
        }
        default:
            // ChatEnded、UsageResponse 等事件对当前 DialogOrchestrator 没有直接动作，
            // 保持 nullopt 可以减少业务层状态分支。
            return std::nullopt;
        }
    } catch (const std::exception& error) {
        // JSON 解析失败、字段结构不符合预期等问题统一转成 kError，
        // 避免异常穿透到 session 主循环导致资源关闭路径被跳过。
        return makeErrorEvent(error.what());
    }
}

VolcRealtimeClientAdapter::VolcRealtimeClientAdapter(
    VolcRealtimeRuntimeConfig config, std::shared_ptr<IVolcRealtimeTransport> transport)
    : client_(std::move(config), std::move(transport)) {}

bool VolcRealtimeClientAdapter::connect() {
    try {
        // adapter 对外只有一个 connect()，内部负责完成火山要求的两段式启动：
        // 1. WebSocket + StartConnection
        // 2. StartSession(input_mod=text 或 audio)
        client_.connect();
        client_.startConnection();
        client_.receiveUntilEvent(VolcRealtimeEventId::kConnectionStarted);
        client_.startSession();
        client_.receiveUntilEvent(VolcRealtimeEventId::kSessionStarted);
        connected_ = true;
        closed_ = false;
        // DialogOrchestrator 只认识项目内部事件，所以这里补一个 kConnected 放到待消费队列。
        pending_events_.push_back(makeEvent(common::RealtimeEventType::kConnected));
        return true;
    } catch (const std::exception&) {
        // IRealtimeClient::connect 返回 bool，不向 session 层暴露火山/Beast 异常类型。
        connected_ = false;
        closed_ = true;
        return false;
    }
}

bool VolcRealtimeClientAdapter::hasNextEvent() const {
    // 当前真实实现是阻塞 receiveNextEvent，不像 mock 那样提前知道队列长度。
    // 因此 hasNextEvent 表达的是“连接还处于可读取状态”，不是“本地队列里已经有事件”。
    return connected_ && !closed_;
}

common::RealtimeEvent VolcRealtimeClientAdapter::receiveNextEvent() {
    if (!pending_events_.empty()) {
        // 优先返回 adapter 自己生成的事件，例如 connect 成功后的 kConnected。
        common::RealtimeEvent event = pending_events_.front();
        pending_events_.pop_front();
        return event;
    }

    for (;;) {
        // 真实服务端可能返回 adapter 不关心的 ack。循环读取直到映射出项目内部事件。
        std::optional<common::RealtimeEvent> event =
            mapVolcRealtimeFrameToRealtimeEvent(client_.receiveFrame());
        if (!event.has_value()) {
            continue;
        }

        if (event->type == common::RealtimeEventType::kClosed ||
            event->type == common::RealtimeEventType::kError) {
            // 一旦读到关闭或错误，事件流就应该停止，防止 DialogOrchestrator 继续阻塞读取。
            closed_ = true;
        }
        return *event;
    }
}

bool VolcRealtimeClientAdapter::sendInterviewerText(const std::string& text) {
    try {
        // 项目内部接口叫“发送面试官文本”，火山实现需要转成 ChatTTSText。
        // 这里隐藏供应商事件名，让 session 层不依赖火山协议。
        client_.sendChatTtsText(text);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool VolcRealtimeClientAdapter::sendCandidateAudio(const AudioPcmChunk& chunk) {
    if (!connected_ || closed_ || chunk.samples.empty()) {
        return false;
    }

    try {
        // 此方法必须由拥有 adapter 的 realtime worker 调用；PortAudio callback
        // 只负责把样本放进队列。
        client_.sendAudioPcm(toLittleEndianPcmBytes(chunk));
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void VolcRealtimeClientAdapter::close() {
    if (!connected_ || closed_) {
        // 如果 connect 失败或已经收到关闭事件，close 只需要标记状态，不再发 finish 包。
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
            // finish 包失败时仍尝试关闭 WebSocket。这里吞掉异常，是为了保证析构/清理路径稳定。
            client_.close();
        } catch (const std::exception&) {
        }
    }
    closed_ = true;
}

} // namespace services
} // namespace interview
