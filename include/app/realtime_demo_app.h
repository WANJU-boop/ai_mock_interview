#pragma once

#include "common/realtime_protocol.h"
#include "session/interview_setup.h"

#include <cstddef>
#include <ostream>
#include <vector>

namespace interview {
namespace app {

// 生成一段默认 mock realtime 脚本：连接成功后，为每道题提供一条高质量 final transcript。
// 这个脚本用于手动 demo 和流程级测试，不依赖真实 WebSocket、麦克风或服务端账号。
std::vector<common::RealtimeEvent> buildDefaultRealtimeDemoScript(std::size_t question_count);

// 运行一次基于 MockRealtimeClient 的 realtime 演示流程。
// 它验证应用层已经能通过 realtime 事件驱动面试，但不会接触真实网络。
int runRealtimeDemoInterview(std::ostream& output, session::PreparedInterview& prepared_interview,
                             const std::vector<common::RealtimeEvent>& scripted_events);

} // namespace app
} // namespace interview
