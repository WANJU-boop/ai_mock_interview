#include "services/realtime_client_factory.h"

#include "services/beast_volc_realtime_transport.h"
#include "services/volc_realtime_client_adapter.h"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace interview {
namespace services {

namespace {

std::string requireEnvValue(const std::string& env_name) {
    // 配置文件只保存环境变量名；真实密钥必须来自本地运行环境，避免被提交进仓库。
    const char* value = std::getenv(env_name.c_str());
    if (value == nullptr || std::string(value).empty()) {
        throw std::runtime_error("请先设置环境变量：" + env_name);
    }

    return value;
}

std::string makeRuntimeId(const std::string& prefix) {
    // 火山需要 connection/session 追踪 ID。这里用时间戳生成运行期 ID，
    // 足够满足手动 smoke test 和服务端排查，不把它写进配置文件。
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    return prefix + "-" + std::to_string(milliseconds);
}

VolcRealtimeClientConfig makeVolcConfig(const common::RealtimeConfig& config) {
    VolcRealtimeClientConfig volc_config;
    volc_config.endpoint = config.endpoint;
    volc_config.app_id = requireEnvValue(config.app_id_env);
    volc_config.access_key = requireEnvValue(config.access_key_env);
    volc_config.resource_id = config.resource_id;
    volc_config.app_key = config.app_key;
    volc_config.connect_id = makeRuntimeId("connect");
    volc_config.session_id = makeRuntimeId("session");
    volc_config.model = config.model;
    volc_config.input_mod = config.input_mod;
    volc_config.speaker = config.speaker;
    volc_config.timeout_ms = config.timeout_ms;
    return volc_config;
}

} // namespace

std::unique_ptr<IRealtimeClient>
createRealtimeClient(const common::RealtimeConfig& config,
                     const std::vector<common::RealtimeEvent>& mock_scripted_events) {
    if (config.provider == "mock") {
        // mock provider 继续使用确定性事件脚本，让默认测试和本地学习流程完全离线。
        return std::make_unique<MockRealtimeClient>(mock_scripted_events);
    }

    if (config.provider == "volc") {
        // 真正联网发生在 connect()，factory 只完成依赖组装。
        // 这样测试可以验证配置和环境变量映射，而不会触发 WebSocket。
        return std::make_unique<VolcRealtimeClientAdapter>(
            makeVolcConfig(config), std::make_shared<BeastVolcRealtimeTransport>());
    }

    throw std::runtime_error("不支持的 realtime provider：" + config.provider);
}

} // namespace services
} // namespace interview
