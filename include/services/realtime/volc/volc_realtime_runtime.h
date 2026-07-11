#pragma once

#include "common/config.h"

#include <string>

namespace interview {
namespace services {

// 火山客户端运行时配置由唯一的应用配置解析得到：
// 1. 普通字段来自 common::RealtimeConfig。
// 2. 真实密钥从配置指定的环境变量读取。
// 3. connect/session ID 在进程运行时生成，不写回配置文件。
// 这个类型没有供应商默认值，避免与 config.h 出现两套可能漂移的默认配置。
struct VolcRealtimeRuntimeConfig {
    // 握手地址和认证字段已经完成环境变量解析，可以直接组装 WebSocket request。
    std::string endpoint;
    std::string app_id;
    std::string access_key;
    std::string resource_id;
    std::string app_key;
    // 两个 ID 每次运行生成，只用于连接/会话追踪，不应持久化或跨会话复用。
    std::string connect_id;
    std::string session_id;
    // Dialog 选项原样进入 StartSession payload。
    std::string model;
    std::string input_mod;
    bool strict_audit = false;
    bool enable_volc_websearch = false;
    // TTS 字段共同描述服务端返回的 PCM 数据，后续播放器必须使用同一格式。
    std::string speaker;
    std::string tts_audio_format;
    int tts_sample_rate_hz = 0;
    int tts_channels = 0;
    // ASR 输入格式来自 realtime.audio；它只描述 PCM，不包含具体 PortAudio 设备 ID。
    int capture_sample_rate_hz = 0;
    int capture_channels = 0;
    int frames_per_buffer = 0;
    // 统一传给底层同步 transport，0 表示运行时配置尚未完整解析。
    int timeout_ms = 0;
};

// 把可持久化配置解析成可直接交给火山客户端的运行时配置。
// 缺少环境变量或 provider 不是 volc 时会在联网前抛出异常。
VolcRealtimeRuntimeConfig resolveVolcRealtimeRuntimeConfig(const common::RealtimeConfig& config);

} // namespace services
} // namespace interview
