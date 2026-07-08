#pragma once

#include "common/config.h"
#include "common/realtime_protocol.h"
#include "services/realtime_client.h"

#include <memory>
#include <vector>

namespace interview {
namespace services {

// 由 services 层统一创建 realtime client，入口层只根据配置选择 provider，
// 不直接依赖火山协议、WebSocket transport 或 mock 的构造细节。
std::unique_ptr<IRealtimeClient>
createRealtimeClient(const common::RealtimeConfig& config,
                     const std::vector<common::RealtimeEvent>& mock_scripted_events);

} // namespace services
} // namespace interview
