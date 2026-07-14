#include "services/realtime/volc/volc_realtime_runtime.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace interview {
namespace services {

namespace {

std::string requireEnvValue(const std::string& env_name) {
    // 这是直写值缺失时的兼容回退；取到的密钥不输出到日志。
    const char* value = std::getenv(env_name.c_str());
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error("请先设置环境变量：" + env_name);
    }

    return value;
}

std::string makeRuntimeId(const std::string& prefix) {
    // 连接和会话 ID 属于一次运行的追踪信息，不能作为静态配置复用。
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "-" + std::to_string(milliseconds);
}

} // namespace

VolcRealtimeRuntimeConfig resolveVolcRealtimeRuntimeConfig(const common::RealtimeConfig& config) {
    if (config.provider != "volc") {
        throw std::runtime_error("只有 realtime.provider=volc 才能解析火山运行时配置");
    }

    // 这里是应用配置进入供应商客户端的唯一映射点。新增字段时只需要更新这一处和对应测试，
    // 手动 demo 与主流程 factory 不会再各自维护一份密钥读取和默认值。
    VolcRealtimeRuntimeConfig runtime;
    runtime.endpoint = config.connection.endpoint;
    runtime.app_id = config.connection.app_id.empty()
                         ? requireEnvValue(config.connection.app_id_env)
                         : config.connection.app_id;
    runtime.access_key = config.connection.access_key.empty()
                             ? requireEnvValue(config.connection.access_key_env)
                             : config.connection.access_key;
    runtime.resource_id = config.connection.resource_id;
    runtime.app_key = config.connection.app_key;
    runtime.connect_id = makeRuntimeId("connect");
    runtime.session_id = makeRuntimeId("session");
    runtime.model = config.dialog.model;
    runtime.input_mod = config.dialog.input_mod;
    runtime.strict_audit = config.dialog.strict_audit;
    runtime.enable_volc_websearch = config.dialog.enable_volc_websearch;
    runtime.speaker = config.tts.speaker;
    runtime.tts_audio_format = config.tts.audio_format;
    runtime.tts_sample_rate_hz = config.tts.sample_rate_hz;
    runtime.tts_channels = config.tts.channels;
    runtime.capture_sample_rate_hz = config.audio.capture_sample_rate_hz;
    runtime.capture_channels = config.audio.capture_channels;
    runtime.frames_per_buffer = config.audio.frames_per_buffer;
    runtime.timeout_ms = config.connection.timeout_ms;
    return runtime;
}

} // namespace services
} // namespace interview
