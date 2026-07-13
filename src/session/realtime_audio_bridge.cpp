#include "session/realtime_audio_bridge.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace interview {
namespace session {

namespace {
//这个函数检查 payload 是不是合法的 PCM 音频数据。
bool isValidPcmPayload(const std::vector<std::uint8_t>& bytes) {
    // s16le 每个样本必须恰好两个字节；空 payload 表示这条事件没有携带 TTS 音频而非播放失败。
    return !bytes.empty() && bytes.size() % 2 == 0;
}


//把网络返回的原始字节，按照小段模式解码变成你的音频设备能播放的 AudioPcmChunk
// 第一个字节放低位
// 第二个字节左移 8 位，放高位
// 然后用 | 合起来
services::AudioPcmChunk decodeLittleEndianPcm(const std::vector<std::uint8_t>& bytes) {
    services::AudioPcmChunk chunk;
    chunk.samples.reserve(bytes.size() / 2);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
        const std::uint16_t unsigned_sample = static_cast<std::uint16_t>(bytes[offset]) |
                                              (static_cast<std::uint16_t>(bytes[offset + 1]) << 8);
        chunk.samples.push_back(static_cast<std::int16_t>(unsigned_sample));
    }
    return chunk;
}

} // namespace



RealtimeAudioBridge::RealtimeAudioBridge(services::IAudioDevice& audio_device, //音频设备，比如你前面那个 PortAudioAudioDevice。
                                         services::IRealtimeClient& realtime_client, //实时客户端，比如 WebSocket client，用来和大模型通信。
                                         services::AudioPcmFormat capture_format,  //录音格式。
                                         services::AudioPcmFormat playback_format)  //播放格式。
    : audio_device_(audio_device), realtime_client_(realtime_client),
      capture_format_(capture_format), playback_format_(playback_format) {}


//启动整个音频桥。
bool RealtimeAudioBridge::start() {
    if (started_) {
        return false;
    }

    // 先启动麦克风再启动扬声器。播放初始化失败时 stop 会回收已经打开的麦克风，
    // 保证调用方看见 false 时没有残留的设备资源或后台 callback。
    if (!audio_device_.startCapture(capture_format_) ||
        !audio_device_.startPlayback(playback_format_)) {
        audio_device_.stop();
        return false;
    }

    started_ = true;
    return true;
}


//把麦克风录到的音频，一块一块发送给 realtime_client_
bool RealtimeAudioBridge::pumpCapturedAudio() {
    if (!started_) {
        return false;
    }

//等价于：while (true) {
    for (;;) {  
        ////会从音频设备读取一块录音数据。
        const std::optional<services::AudioPcmChunk> chunk = audio_device_.tryReadCapturedChunk();
        if (!chunk.has_value()) {
            return true;
        }
        // 这里运行在 websocket 所有者线程；PortAudio callback 从未调用 sendCandidateAudio。
        if (!realtime_client_.sendCandidateAudio(*chunk)) {
            return false;
        }
    }
}

// 处理 realtime_client_ 收到的事件
// 如果事件里有 TTS 音频 payload，就播放出来
bool RealtimeAudioBridge::consumeRealtimeEvent(const common::RealtimeEvent& event) {
    if (!started_ || event.type != common::RealtimeEventType::kInterviewerText ||
        event.payload.empty()) {
        return true;
    }
    if (!isValidPcmPayload(event.payload)) {
        return false;
    }

    // adapter 只给 TTSResponse 填充 payload；文字 ChatResponse 仍然走 text 字段，不会被误当音频。
    return audio_device_.playPcmChunk(decodeLittleEndianPcm(event.payload));
}

void RealtimeAudioBridge::stop() {
    if (!started_) {
        return;
    }

    // 先关闭音频 callback，再由编排器关闭 WSS，避免回调继续堆积永远不会发送的录音块。
    audio_device_.stop();
    started_ = false;
}

} // namespace session
} // namespace interview
