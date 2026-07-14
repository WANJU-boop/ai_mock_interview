#include "session/realtime_audio_bridge.h"

#include "common/logger.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace interview {
namespace session {

namespace {
// 每次只发送有限数量的录音块，避免“采集速度 >= 网络发送速度”时永远抽不空队列，
// 导致同一 worker 无法再处理服务端事件或检查扬声器是否播放完毕。
constexpr std::size_t kMaxCaptureChunksPerPump = 1;

// 这个函数检查 payload 是不是合法的 PCM 音频数据。
bool isValidPcmPayload(const std::vector<std::uint8_t>& bytes) {
    // s16le 每个样本必须恰好两个字节；空 payload 表示这条事件没有携带 TTS 音频而非播放失败。
    return !bytes.empty() && bytes.size() % 2 == 0;
}

// 把网络返回的原始字节，按照小段模式解码变成你的音频设备能播放的 AudioPcmChunk
//  第一个字节放低位
//  第二个字节左移 8 位，放高位
//  然后用 | 合起来
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

RealtimeAudioBridge::RealtimeAudioBridge(
    services::IAudioDevice& audio_device, // 音频设备，比如你前面那个 PortAudioAudioDevice。
    services::IRealtimeClient&
        realtime_client, // 实时客户端，比如 WebSocket client，用来和大模型通信。
    services::AudioPcmFormat capture_format,  // 录音格式。
    services::AudioPcmFormat playback_format) // 播放格式。
    : audio_device_(audio_device), realtime_client_(realtime_client),
      capture_format_(capture_format), playback_format_(playback_format) {}

// 启动整个音频桥。
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
    // 刚启动时先不上传真实录音，等待欢迎语和第一题 TTS 播放结束。
    capture_forwarding_enabled_ = false;
    forwarded_capture_chunks_ = 0;
    return true;
}

// 把麦克风录到的音频，一块一块发送给 realtime_client_
bool RealtimeAudioBridge::pumpCapturedAudio() {
    if (!started_) {
        return false;
    }

    // TTS 服务端结束后，PortAudio 播放队列可能还有尚未播完的 PCM。
    // 只在本地队列也清空后恢复真实麦克风，避免截到面试官尾音。
    const bool playback_queue_empty = !audio_device_.hasPendingPlayback();
    const bool playback_deadline_reached =
        capture_resume_deadline_ != std::chrono::steady_clock::time_point{} &&
        std::chrono::steady_clock::now() >= capture_resume_deadline_;
    if (resume_capture_after_playback_ && (playback_queue_empty || playback_deadline_reached)) {
        if (!playback_queue_empty) {
            // 某些 CoreAudio/PortAudio 组合的队列状态可能长期不归零。服务端 PCM 样本数能给出
            // 精确播放时长，因此到达保守截止时间后恢复麦克风，避免会话永久静音。
            LOG_WARN("播放队列状态未归零，已按 PCM 计算时长强制恢复麦克风。");
        }
        capture_forwarding_enabled_ = true;
        resume_capture_after_playback_ = false;
        LOG_INFO("面试官语音播放完毕，开始上传真实麦克风 PCM。");
    }

    for (std::size_t sent_chunks = 0; sent_chunks < kMaxCaptureChunksPerPump; ++sent_chunks) {
        ////会从音频设备读取一块录音数据。
        const std::optional<services::AudioPcmChunk> chunk = audio_device_.tryReadCapturedChunk();
        if (!chunk.has_value()) {
            return true;
        }
        services::AudioPcmChunk outgoing_chunk = *chunk;
        if (!capture_forwarding_enabled_) {
            // keep_alive 模式不能长时间断流。扬声器播放时保留 20ms 发包节奏，
            // 但把采集值替换为静音，同时解决连接空闲和回声自激问题。
            std::fill(outgoing_chunk.samples.begin(), outgoing_chunk.samples.end(), 0);
        } else {
            ++forwarded_capture_chunks_;
            if (forwarded_capture_chunks_ == 1 || forwarded_capture_chunks_ % 50 == 0) {
                int peak = 0;
                for (const std::int16_t sample : outgoing_chunk.samples) {
                    const int value = static_cast<int>(sample);
                    peak = std::max(peak, value >= 0 ? value : -value);
                }
                // 每约一秒只记录一次峰值；不写采样值、转写文本或完整回答。
                LOG_INFO("麦克风 PCM 已上传：chunks={} peak={}", forwarded_capture_chunks_, peak);
            }
        }

        // 这里运行在 websocket 所有者线程；PortAudio callback 从未调用 sendCandidateAudio。
        if (!realtime_client_.sendCandidateAudio(outgoing_chunk)) {
            return false;
        }
    }

    // 队列即使还有数据也主动让出执行权；下一轮会继续发送，不丢弃已采集 PCM。
    return true;
}

// 处理 realtime_client_ 收到的事件
// 如果事件里有 TTS 音频 payload，就播放出来
bool RealtimeAudioBridge::consumeRealtimeEvent(const common::RealtimeEvent& event) {
    if (!started_) {
        return true;
    }

    if (event.type == common::RealtimeEventType::kTtsStarted) {
        capture_forwarding_enabled_ = false;
        resume_capture_after_playback_ = false;
        current_tts_samples_ = 0;
        tts_playback_started_at_ = std::chrono::steady_clock::now();
        capture_resume_deadline_ = {};
        return true;
    }

    if (event.type == common::RealtimeEventType::kTtsEnded) {
        if (pending_interviewer_utterances_ > 0) {
            --pending_interviewer_utterances_;
        }
        // 欢迎语和第一题可能连续入队；必须等所有本地文本的 TTS 都结束。
        resume_capture_after_playback_ = pending_interviewer_utterances_ == 0;
        const double duration_seconds = static_cast<double>(current_tts_samples_) /
                                        (static_cast<double>(playback_format_.sample_rate_hz) *
                                         static_cast<double>(playback_format_.channels));
        // 额外留 500ms 覆盖首包调度和声卡缓冲；正常情况下队列先归零，不会等到此截止时间。
        const auto conservative_duration = std::chrono::duration<double>(duration_seconds + 0.5);
        capture_resume_deadline_ =
            tts_playback_started_at_ +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(conservative_duration);
        LOG_INFO("火山 TTS 已接收完毕：samples={} estimated_seconds={:.1f}，等待扬声器播放完。",
                 current_tts_samples_, duration_seconds);
        return true;
    }

    if (event.type != common::RealtimeEventType::kInterviewerText || event.payload.empty()) {
        return true;
    }
    if (!isValidPcmPayload(event.payload)) {
        LOG_ERROR("火山 TTS PCM 字节数非法：payload_bytes={}", event.payload.size());
        return false;
    }

    // adapter 只给 TTSResponse 填充 payload；文字 ChatResponse 仍然走 text 字段，不会被误当音频。
    const services::AudioPcmChunk pcm_chunk = decodeLittleEndianPcm(event.payload);
    if (!audio_device_.playPcmChunk(pcm_chunk)) {
        // 不打印音频内容，只记录大小。真实服务可能一次返回超过本地队列容量的大块 PCM，
        // 这个诊断能区分“格式错误”和“播放队列装不下”。
        LOG_ERROR("本地播放队列拒绝火山 TTS：samples={}", pcm_chunk.samples.size());
        return false;
    }
    current_tts_samples_ += pcm_chunk.samples.size();
    return true;
}

void RealtimeAudioBridge::suspendCaptureForwarding() {
    capture_forwarding_enabled_ = false;
    resume_capture_after_playback_ = false;
    forwarded_capture_chunks_ = 0;
    current_tts_samples_ = 0;
    ++pending_interviewer_utterances_;
}

void RealtimeAudioBridge::muteCaptureForwarding() {
    capture_forwarding_enabled_ = false;
    resume_capture_after_playback_ = false;
}

bool RealtimeAudioBridge::isInterviewerPlaybackComplete() const {
    return started_ && pending_interviewer_utterances_ == 0 && !audio_device_.hasPendingPlayback();
}

void RealtimeAudioBridge::stop() {
    if (!started_) {
        return;
    }

    // 先关闭音频 callback，再由编排器关闭 WSS，避免回调继续堆积永远不会发送的录音块。
    audio_device_.stop();
    started_ = false;
    capture_forwarding_enabled_ = false;
    resume_capture_after_playback_ = false;
    pending_interviewer_utterances_ = 0;
}

} // namespace session
} // namespace interview
