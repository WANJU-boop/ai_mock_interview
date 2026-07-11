#pragma once

#include "common/realtime_protocol.h"
#include "services/realtime/realtime_client.h"
#include "session/interview_setup.h"
#include "session/realtime_audio_bridge.h"

#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

namespace interview {
namespace app {

// 生成一段默认 mock realtime 脚本：连接成功后，为每道题提供一条高质量 final transcript。
// 这个脚本用于手动 demo 和流程级测试，不依赖真实 WebSocket、麦克风或服务端账号。
std::vector<common::RealtimeEvent> buildDefaultRealtimeDemoScript(std::size_t question_count);

// 运行一次由外部 IRealtimeClient 驱动的 realtime 面试。
// app 层只负责展示结果，不关心事件来自 mock 脚本、火山 WebSocket 还是未来音频服务。
int runConfiguredRealtimeInterview(std::ostream& output,
                                   session::PreparedInterview& prepared_interview,
                                   services::IRealtimeClient& realtime_client,
                                   const std::string& provider_name,
                                   session::RealtimeAudioBridge* audio_bridge = nullptr,
                                   const std::string& report_output_directory = "");

// 对真实 provider 做最小连接检查，不进入完整面试循环。
// text provider 仍只做连接检查；audio provider 会通过 RealtimeAudioBridge 进入完整面试循环。
int runRealtimeConnectionSmoke(std::ostream& output, services::IRealtimeClient& realtime_client,
                               const std::string& provider_name);

// 运行一次基于 MockRealtimeClient 的 realtime 演示流程。
// 它验证应用层已经能通过 realtime 事件驱动面试，但不会接触真实网络。
int runRealtimeDemoInterview(std::ostream& output, session::PreparedInterview& prepared_interview,
                             const std::vector<common::RealtimeEvent>& scripted_events);

} // namespace app
} // namespace interview
