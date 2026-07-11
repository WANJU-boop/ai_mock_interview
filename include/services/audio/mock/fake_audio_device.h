#pragma once

#include "services/audio/audio_device.h"

#include <cstddef>
#include <vector>

namespace interview {
namespace services {

// FakeAudioDevice 用预置 PCM 块模拟麦克风，并记录送往扬声器的块。
// 它不申请系统权限、不创建线程，因此默认单元测试可以稳定覆盖音频生命周期和格式边界。
class FakeAudioDevice final : public IAudioDevice {
  public:
    // 预置块按值移入，测试脚本可在构造后离开作用域而不会影响待消费的录音数据。
    explicit FakeAudioDevice(std::vector<AudioPcmChunk> captured_chunks);

    bool startCapture(const AudioPcmFormat& format) override;
    bool startPlayback(const AudioPcmFormat& format) override;
    std::optional<AudioPcmChunk> tryReadCapturedChunk() override;
    bool playPcmChunk(const AudioPcmChunk& chunk) override;
    void stop() override;

    // 下列只读查询仅用于测试和手动 demo，生产设备不应把内部音频缓存暴露给业务层。
    const std::vector<AudioPcmChunk>& getPlayedChunks() const;
    bool isCaptureStarted() const;
    bool isPlaybackStarted() const;
    bool isStopped() const;

  private:
    std::vector<AudioPcmChunk> captured_chunks_;
    std::vector<AudioPcmChunk> played_chunks_;
    AudioPcmFormat capture_format_;
    AudioPcmFormat playback_format_;
    std::size_t next_captured_chunk_index_ = 0;
    bool capture_started_ = false;
    bool playback_started_ = false;
    bool stopped_ = false;
};

} // namespace services
} // namespace interview
