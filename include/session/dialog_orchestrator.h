#pragma once

#include "services/realtime/realtime_client.h"
#include "session/dialog_session.h"
#include "session/interview_setup.h"
#include "session/realtime_audio_bridge.h"

#include <string>
#include <vector>

namespace interview {
namespace session {

// realtime 编排结果保留会话数据、错误原因和面试官已发送文本，
// 让 CLI demo、后续 Qt UI 和测试都能读取同一份流程输出。
struct DialogOrchestratorResult {
    // 只有所有题目完成且客户端正常收口时才为 true。
    bool success = false;
    // 失败路径给 app/UI 展示的稳定错误，不暴露供应商私有对象。
    std::string error_message;
    // session 保存最终状态、回答、评分和结构化问答记录。
    DialogSession session;
    // partial transcript 是实时识别的临时文本，只给 UI 展示，不参与评分和报告。
    std::vector<std::string> partial_transcripts;
    // 保存本地成功发送的面试官文本，供 CLI 展示和测试验证顺序。
    std::vector<std::string> interviewer_messages;
};

// DialogOrchestrator 把 realtime 事件流接到现有面试领域逻辑：
// 它不负责 WebSocket 细节，也不直接生成题目或评分，只协调 IRealtimeClient 和 InterviewManager。
class DialogOrchestrator final {
  public:
    // 两个依赖都由外部拥有，且必须比 orchestrator 和同步 run() 活得更久。
    DialogOrchestrator(PreparedInterview& prepared_interview,
                       services::IRealtimeClient& realtime_client,
                       RealtimeAudioBridge* audio_bridge = nullptr);

    // 运行一次确定性的 realtime 面试；当前实现是同步事件循环，方便先用 mock 测试闭环。
    DialogOrchestratorResult run();

  private:
    // PreparedInterview 提供候选人上下文和题目管理器，不在编排层复制所有权。
    PreparedInterview& prepared_interview_;
    // 接口引用让同一状态机可以由 mock 脚本或真实火山 adapter 驱动。
    services::IRealtimeClient& realtime_client_;
    // 空指针保持原有 mock/text 流程不变；audio 模式显式注入 bridge，避免 session 自己创建硬件资源。
    RealtimeAudioBridge* audio_bridge_ = nullptr;
};

} // namespace session
} // namespace interview
