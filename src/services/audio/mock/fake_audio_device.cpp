#include "services/audio/mock/fake_audio_device.h"

#include <utility>

namespace interview {
namespace services {

namespace {

// 设备启动前先锁定基础格式约束，避免把 0 Hz、0 声道或 0 帧交给未来 PortAudio adapter。
bool isValidFormat(const AudioPcmFormat& format) {
    return format.sample_rate_hz > 0 && format.channels > 0 && format.frames_per_buffer > 0;
}

// PCM 数据必须以完整帧结尾；网络音频、播放端和 ASR 都不能从半个多声道帧恢复语义。
bool isChannelAligned(const AudioPcmChunk& chunk, const AudioPcmFormat& format) {
    return !chunk.samples.empty() &&
           chunk.samples.size() % static_cast<std::size_t>(format.channels) == 0;
}

} // namespace

FakeAudioDevice::FakeAudioDevice(std::vector<AudioPcmChunk> captured_chunks)
    : captured_chunks_(std::move(captured_chunks)) {}

bool FakeAudioDevice::startCapture(const AudioPcmFormat& format) {
    if (stopped_ || capture_started_ || !isValidFormat(format)) {
        // 停止后的 fake 不允许重新打开，刻意模拟一次会话只拥有一组设备资源的真实生命周期。
        return false;
    }

    capture_format_ = format;
    capture_started_ = true;
    return true;
}

bool FakeAudioDevice::startPlayback(const AudioPcmFormat& format) {
    if (stopped_ || playback_started_ || !isValidFormat(format)) {
        return false;
    }

    playback_format_ = format;
    playback_started_ = true;
    return true;
}

std::optional<AudioPcmChunk> FakeAudioDevice::tryReadCapturedChunk() {
    if (!capture_started_ || stopped_ || next_captured_chunk_index_ >= captured_chunks_.size()) {
        return std::nullopt;
    }

    const AudioPcmChunk& next_chunk = captured_chunks_[next_captured_chunk_index_];
    if (!isChannelAligned(next_chunk, capture_format_)) {
        // 真实设备 adapter 也应在进入 WebSocket 前拒绝损坏帧；fake 用 nullopt 保持轮询 API 无异常。
        ++next_captured_chunk_index_;
        return std::nullopt;
    }

    ++next_captured_chunk_index_;
    return next_chunk;
}

bool FakeAudioDevice::playPcmChunk(const AudioPcmChunk& chunk) {
    if (!playback_started_ || stopped_ || !isChannelAligned(chunk, playback_format_)) {
        return false;
    }

    // 复制而非保存引用，确保调用方复用自己的发送缓冲区不会篡改测试观察到的播放数据。
    played_chunks_.push_back(chunk);
    return true;
}

bool FakeAudioDevice::hasPendingPlayback() const {
    // fake 的 playPcmChunk 代表同步消费并保存观察值，没有真实硬件播放队列。
    return playback_pending_for_testing_;
}

void FakeAudioDevice::stop() {
    // 清理只改变生命周期标记，不清除 played_chunks_，让测试在收口后仍能验证已播放数据。
    stopped_ = true;
    capture_started_ = false;
    playback_started_ = false;
}

const std::vector<AudioPcmChunk>& FakeAudioDevice::getPlayedChunks() const {
    return played_chunks_;
}

bool FakeAudioDevice::isCaptureStarted() const {
    return capture_started_;
}

bool FakeAudioDevice::isPlaybackStarted() const {
    return playback_started_;
}

bool FakeAudioDevice::isStopped() const {
    return stopped_;
}

void FakeAudioDevice::setPlaybackPendingForTesting(bool pending) {
    playback_pending_for_testing_ = pending;
}

} // namespace services
} // namespace interview
