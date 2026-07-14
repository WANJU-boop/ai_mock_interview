#pragma once

#include "common/realtime_protocol.h"
#include "services/audio/audio_device.h"
#include "services/realtime/realtime_client.h"

#include <chrono>
#include <cstddef>

namespace interview {
namespace session {

// RealtimeAudioBridge 是同一 realtime worker 内“设备 PCM <-> 服务客户端”的窄连接。
// 它不创建线程：PortAudio callback 只操作设备内部的 SPSC 队列，而调用 bridge 的线程独占
// IRealtimeClient 的发送、接收和关闭，避免同一 WebSocket 被多个线程并发访问。
class RealtimeAudioBridge final {
  public:
    RealtimeAudioBridge(services::IAudioDevice& audio_device,
                        services::IRealtimeClient& realtime_client,
                        services::AudioPcmFormat capture_format,
                        services::AudioPcmFormat playback_format);

    // 先启动捕获和播放；任一失败时停止已成功的一侧，调用方可以统一走会话错误收口。
    bool start();
    // 把目前已经采集到的所有完整块发送给 realtime。没有数据是正常轮询状态，返回 true。
    bool pumpCapturedAudio();
    // 消费带 PCM payload 的面试官事件并送到播放队列。非音频事件无需处理，返回 true。
    bool consumeRealtimeEvent(const common::RealtimeEvent& event);
    // 发送面试官文本前暂停真实麦克风上传；期间仍发送静音保活，避免扬声器回放被 ASR 当成候选人回答。
    void suspendCaptureForwarding();
    // LLM 评分期间只暂停麦克风，不增加待完成 TTS 计数。
    void muteCaptureForwarding();
    // 结束语只有在服务端 TTS 结束且本地播放队列清空后才能关闭。
    bool isInterviewerPlaybackComplete() const;
    // 停止设备。该入口幂等，DialogOrchestrator 的正常、错误和析构路径都可调用。
    void stop();

  private:
    services::IAudioDevice& audio_device_;
    services::IRealtimeClient& realtime_client_;
    services::AudioPcmFormat capture_format_;
    services::AudioPcmFormat playback_format_;
    bool started_ = false;
    bool capture_forwarding_enabled_ = false;
    bool resume_capture_after_playback_ = false;
    std::size_t pending_interviewer_utterances_ = 0;
    // 只用于低频诊断麦克风是否采到非静音数据，不保存任何录音内容。
    std::size_t forwarded_capture_chunks_ = 0;
    // 统计本轮收到的采样数量，用于提示实际播放时长，不保存 PCM 内容。
    std::size_t current_tts_samples_ = 0;
    std::chrono::steady_clock::time_point tts_playback_started_at_{};
    std::chrono::steady_clock::time_point capture_resume_deadline_{};
};

} // namespace session
} // namespace interview
