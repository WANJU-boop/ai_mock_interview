#pragma once

#include "services/realtime/realtime_client.h"
#include "session/dialog_session.h"
#include "session/interview_setup.h"

#include <string>
#include <vector>

namespace interview {
namespace session {

// realtime 编排结果保留会话数据、错误原因和面试官已发送文本，
// 让 CLI demo、后续 Qt UI 和测试都能读取同一份流程输出。
struct DialogOrchestratorResult {
    bool success = false;
    std::string error_message;
    DialogSession session;
    // partial transcript 是实时识别的临时文本，只给 UI 展示，不参与评分和报告。
    std::vector<std::string> partial_transcripts;
    std::vector<std::string> interviewer_messages;
};

// DialogOrchestrator 把 realtime 事件流接到现有面试领域逻辑：
// 它不负责 WebSocket 细节，也不直接生成题目或评分，只协调 IRealtimeClient 和 InterviewManager。
class DialogOrchestrator final {
  public:
    DialogOrchestrator(PreparedInterview& prepared_interview,
                       services::IRealtimeClient& realtime_client);

    // 运行一次确定性的 realtime 面试；当前实现是同步事件循环，方便先用 mock 测试闭环。
    DialogOrchestratorResult run();

  private:
    PreparedInterview& prepared_interview_;
    services::IRealtimeClient& realtime_client_;
};

} // namespace session
} // namespace interview
