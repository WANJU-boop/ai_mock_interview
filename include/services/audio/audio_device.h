#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace interview {
namespace services {

// 本地 PCM 流的设备无关描述。samples 使用 int16_t 表示 signed 16-bit little-endian
// PCM 的数值；字节序转换只在未来网络协议边界发生，设备和业务层不操作裸字节。
struct AudioPcmFormat {
    int sample_rate_hz = 0;
    int channels = 0;
    int frames_per_buffer = 0;
};

// 一块连续的 PCM 采样值。samples 的数量必须能被 channels 整除，
// 否则最后一帧不完整，不能安全交给 ASR 或扬声器。
struct AudioPcmChunk {
    std::vector<std::int16_t> samples;
};

// IAudioDevice 隔离麦克风/扬声器硬件，使会话层只接触 PCM 块而不依赖 PortAudio。
// 捕获读取采用非阻塞 try 语义：实时 worker 可以轮询或等待自己的队列，
// 而音频回调绝不应在这里等待网络、LLM 或 UI。
class IAudioDevice {
  public:
    virtual ~IAudioDevice() = default;

    // 建立输入流。格式非法、设备不可用或设备已经停止时返回 false；不抛出第三方库对象。
    virtual bool startCapture(const AudioPcmFormat& format) = 0;

    // 建立输出流。输入输出可使用不同格式，调用方必须分别传入 ASR 与 TTS 的实际约定。
    virtual bool startPlayback(const AudioPcmFormat& format) = 0;

    // 取一块已经采集完成的 PCM。没有可读数据时返回 std::nullopt，不把“暂时没有声音”当错误。
    virtual std::optional<AudioPcmChunk> tryReadCapturedChunk() = 0;

    // 把完整 PCM 块交给播放端。设备未启动、块为空或通道未对齐时返回 false。
    virtual bool playPcmChunk(const AudioPcmChunk& chunk) = 0;

    // 停止输入和输出资源。此函数必须幂等，保证连接失败、用户取消和析构收口可以共用同一入口。
    virtual void stop() = 0;
};

} // namespace services
} // namespace interview
