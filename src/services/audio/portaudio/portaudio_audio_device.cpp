#include "services/audio/portaudio/portaudio_audio_device.h"

#include "services/audio/pcm_sample_queue.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <portaudio.h>
#include <utility>
#include <vector>

namespace interview {
namespace services {

namespace {

std::mutex g_portaudio_mutex;
int g_portaudio_lease_count = 0;

// PortAudio 是进程级 C API。多个会话即使各有自己的设备对象，也只能在第一个开始时初始化、
// 最后一个停止时终止；互斥锁只保护启动/停止阶段，不会进入音频回调。
bool acquirePortAudioLibrary() {
    std::lock_guard<std::mutex> lock(g_portaudio_mutex);
    if (g_portaudio_lease_count == 0 && Pa_Initialize() != paNoError) {
        return false;
    }

    ++g_portaudio_lease_count;
    return true;
}

void releasePortAudioLibrary() {
    std::lock_guard<std::mutex> lock(g_portaudio_mutex);
    if (g_portaudio_lease_count == 0) {
        return;
    }

    --g_portaudio_lease_count;
    if (g_portaudio_lease_count == 0) {
        // 停止阶段即使 PortAudio 报错也不抛异常：析构和错误收口不能因第三方清理失败终止进程。
        Pa_Terminate();
    }
}

bool isValidFormat(const AudioPcmFormat& format) {
    return format.sample_rate_hz > 0 && format.channels > 0 && format.frames_per_buffer > 0;
}

bool isChannelAligned(const AudioPcmChunk& chunk, const AudioPcmFormat& format) {
    return !chunk.samples.empty() &&
           chunk.samples.size() % static_cast<std::size_t>(format.channels) == 0;
}

std::size_t queueCapacitySamples(const AudioPcmFormat& format) {
    // 两秒缓存是“短暂调度抖动”和“持续内存占用”之间的保守折中。过大音频配置在分配前失败，
    // 不让错误 JSON 触发 size_t 溢出或一次非常大的内存申请。
    constexpr std::size_t kQueueSeconds = 2;
    const std::size_t sample_rate = static_cast<std::size_t>(format.sample_rate_hz);
    const std::size_t channels = static_cast<std::size_t>(format.channels);
    if (sample_rate > std::numeric_limits<std::size_t>::max() / channels / kQueueSeconds) {
        return 0;
    }

    const std::size_t frames_per_buffer = static_cast<std::size_t>(format.frames_per_buffer);
    if (frames_per_buffer > std::numeric_limits<std::size_t>::max() / channels) {
        return 0;
    }

    return std::max(frames_per_buffer * channels, sample_rate * channels * kQueueSeconds);
}

void closeStream(PaStream*& stream) {
    if (stream == nullptr) {
        return;
    }

    // Abort 不等待已经排队的 TTS 播放完毕；用户取消、网络失败和析构需要优先释放设备。
    Pa_AbortStream(stream);
    Pa_CloseStream(stream);
    stream = nullptr;
}

} // namespace

class PortAudioAudioDevice::Impl {
  public:
    static int captureCallback(const void* input, void*, unsigned long frame_count,
                               const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags,
                               void* user_data) {
        Impl* impl = static_cast<Impl*>(user_data);
        if (input == nullptr || impl->capture_queue_ == nullptr) {
            return paContinue;
        }

        const std::size_t sample_count = static_cast<std::size_t>(frame_count) *
                                         static_cast<std::size_t>(impl->capture_format_.channels);
        const std::int16_t* samples = static_cast<const std::int16_t*>(input);
        // 空间不够时丢弃“整块最新音频”。回调不等待 worker，避免麦克风线程阻塞导致更大面积丢帧。
        impl->capture_queue_->tryPush(samples, sample_count);
        return paContinue;
    }

    static int playbackCallback(const void*, void* output, unsigned long frame_count,
                                const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags,
                                void* user_data) {
        Impl* impl = static_cast<Impl*>(user_data);
        std::int16_t* samples = static_cast<std::int16_t*>(output);
        if (samples == nullptr) {
            return paContinue;
        }
        const std::size_t sample_count = static_cast<std::size_t>(frame_count) *
                                         static_cast<std::size_t>(impl->playback_format_.channels);
        const std::size_t copied_samples =
            impl->playback_queue_ == nullptr
                ? 0
                : impl->playback_queue_->tryPopUpTo(samples, sample_count);
        // TTS 尾包可能比设备 callback 小。未填满的部分补静音，不等待网络或保留未初始化内存。
        std::fill(samples + copied_samples, samples + sample_count, 0);
        return paContinue;
    }

    PaStream* capture_stream_ = nullptr;
    PaStream* playback_stream_ = nullptr;
    std::unique_ptr<PcmSampleQueue> capture_queue_;
    std::unique_ptr<PcmSampleQueue> playback_queue_;
    AudioPcmFormat capture_format_;
    AudioPcmFormat playback_format_;
    bool capture_started_ = false;
    bool playback_started_ = false;
    bool library_acquired_ = false;
    bool stopped_ = false;
};

PortAudioAudioDevice::PortAudioAudioDevice() : impl_(std::make_unique<Impl>()) {}

PortAudioAudioDevice::~PortAudioAudioDevice() {
    stop();
}

bool PortAudioAudioDevice::startCapture(const AudioPcmFormat& format) {
    if (impl_->stopped_ || impl_->capture_started_ || !isValidFormat(format)) {
        return false;
    }

    if (!impl_->library_acquired_) {
        if (!acquirePortAudioLibrary()) {
            return false;
        }
        impl_->library_acquired_ = true;
    }

    try {
        impl_->capture_queue_ = std::make_unique<PcmSampleQueue>(queueCapacitySamples(format));
    } catch (const std::exception&) {
        if (!impl_->playback_started_) {
            releasePortAudioLibrary();
            impl_->library_acquired_ = false;
        }
        return false;
    }

    impl_->capture_format_ = format;
    if (Pa_OpenDefaultStream(&impl_->capture_stream_, format.channels, 0, paInt16,
                             static_cast<double>(format.sample_rate_hz),
                             static_cast<unsigned long>(format.frames_per_buffer),
                             &Impl::captureCallback, impl_.get()) != paNoError ||
        Pa_StartStream(impl_->capture_stream_) != paNoError) {
        closeStream(impl_->capture_stream_);
        impl_->capture_queue_.reset();
        if (!impl_->playback_started_) {
            releasePortAudioLibrary();
            impl_->library_acquired_ = false;
        }
        return false;
    }

    impl_->capture_started_ = true;
    return true;
}

bool PortAudioAudioDevice::startPlayback(const AudioPcmFormat& format) {
    if (impl_->stopped_ || impl_->playback_started_ || !isValidFormat(format)) {
        return false;
    }

    if (!impl_->library_acquired_) {
        if (!acquirePortAudioLibrary()) {
            return false;
        }
        impl_->library_acquired_ = true;
    }

    try {
        impl_->playback_queue_ = std::make_unique<PcmSampleQueue>(queueCapacitySamples(format));
    } catch (const std::exception&) {
        if (!impl_->capture_started_) {
            releasePortAudioLibrary();
            impl_->library_acquired_ = false;
        }
        return false;
    }

    impl_->playback_format_ = format;
    if (Pa_OpenDefaultStream(&impl_->playback_stream_, 0, format.channels, paInt16,
                             static_cast<double>(format.sample_rate_hz),
                             static_cast<unsigned long>(format.frames_per_buffer),
                             &Impl::playbackCallback, impl_.get()) != paNoError ||
        Pa_StartStream(impl_->playback_stream_) != paNoError) {
        closeStream(impl_->playback_stream_);
        impl_->playback_queue_.reset();
        if (!impl_->capture_started_) {
            releasePortAudioLibrary();
            impl_->library_acquired_ = false;
        }
        return false;
    }

    impl_->playback_started_ = true;
    return true;
}

std::optional<AudioPcmChunk> PortAudioAudioDevice::tryReadCapturedChunk() {
    if (!impl_->capture_started_ || impl_->capture_queue_ == nullptr) {
        return std::nullopt;
    }

    const std::size_t sample_count =
        static_cast<std::size_t>(impl_->capture_format_.frames_per_buffer) *
        static_cast<std::size_t>(impl_->capture_format_.channels);
    AudioPcmChunk chunk;
    chunk.samples.resize(sample_count);
    if (!impl_->capture_queue_->tryPop(chunk.samples.data(), sample_count)) {
        return std::nullopt;
    }

    return chunk;
}

bool PortAudioAudioDevice::playPcmChunk(const AudioPcmChunk& chunk) {
    if (!impl_->playback_started_ || impl_->playback_queue_ == nullptr ||
        !isChannelAligned(chunk, impl_->playback_format_)) {
        return false;
    }

    // realtime worker 是播放队列唯一生产者；PortAudio 回调是唯一消费者，因此 tryPush 无锁安全。
    return impl_->playback_queue_->tryPush(chunk.samples.data(), chunk.samples.size());
}

void PortAudioAudioDevice::stop() {
    if (impl_->stopped_) {
        return;
    }

    // 先停硬件回调，再销毁队列；否则 callback 可能访问已经释放的缓存。
    closeStream(impl_->playback_stream_);
    closeStream(impl_->capture_stream_);
    impl_->capture_started_ = false;
    impl_->playback_started_ = false;
    impl_->capture_queue_.reset();
    impl_->playback_queue_.reset();
    if (impl_->library_acquired_) {
        releasePortAudioLibrary();
        impl_->library_acquired_ = false;
    }
    impl_->stopped_ = true;
}

} // namespace services
} // namespace interview
