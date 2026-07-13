#pragma once

#include "services/audio/audio_device.h"

#include <memory>

namespace interview {
namespace services {

// PortAudioAudioDevice 是唯一直接接触 PortAudio 的 adapter。
// 输入/输出回调只与内部固定容量 SPSC 队列交换 int16 PCM；realtime、LLM 和 UI 必须由
// 其它 worker 消费这些队列，绝不能从 PortAudio 回调发 WebSocket 或更新界面。
class PortAudioAudioDevice final : public IAudioDevice {
  public:
    PortAudioAudioDevice();
    ~PortAudioAudioDevice() override;


    //表示这个类不能被复制
    PortAudioAudioDevice(const PortAudioAudioDevice&) = delete;
    PortAudioAudioDevice& operator=(const PortAudioAudioDevice&) = delete;

    bool startCapture(const AudioPcmFormat& format) override;
    bool startPlayback(const AudioPcmFormat& format) override;
    std::optional<AudioPcmChunk> tryReadCapturedChunk() override;
    bool playPcmChunk(const AudioPcmChunk& chunk) override;
    void stop() override;

  private:
    // Pimpl 隐藏 portaudio.h 和 callback 状态，调用者不会把第三方类型带入业务头文件。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
