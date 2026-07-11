#pragma once

#include "common/realtime_protocol.h"
#include "services/audio/audio_device.h"
#include "services/realtime/realtime_client.h"

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
    // 停止设备。该入口幂等，DialogOrchestrator 的正常、错误和析构路径都可调用。
    void stop();

  private:
    services::IAudioDevice& audio_device_;
    services::IRealtimeClient& realtime_client_;
    services::AudioPcmFormat capture_format_;
    services::AudioPcmFormat playback_format_;
    bool started_ = false;
};

} // namespace session
} // namespace interview
