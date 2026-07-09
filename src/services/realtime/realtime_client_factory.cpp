#include "services/realtime/realtime_client_factory.h"

#include "services/realtime/mock/mock_realtime_client.h"
#include "services/realtime/volc/beast_volc_realtime_transport.h"
#include "services/realtime/volc/volc_realtime_client_adapter.h"
#include "services/realtime/volc/volc_realtime_runtime.h"

#include <memory>
#include <stdexcept>
#include <vector>

namespace interview {
namespace services {

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
            resolveVolcRealtimeRuntimeConfig(config),
            std::make_shared<BeastVolcRealtimeTransport>());
    }

    throw std::runtime_error("不支持的 realtime provider：" + config.provider);
}

} // namespace services
} // namespace interview
