#pragma once

#include "session/interview_state.h"

#include <string>

namespace interview {
namespace session {

// 对话观察者把同步编排过程转换成只读进度事件。
// 回调发生在运行 DialogOrchestrator::run() 的线程；Qt 等 UI 必须再通过 queued signal
// 回到自己的主线程，不能在这些回调里直接修改控件。
class IDialogObserver {
  public:
    virtual ~IDialogObserver() = default;

    // 每次业务状态真正写入 DialogSession 后通知展示层。
    virtual void OnStateChanged(InterviewState state) = 0;

    // 只有面试官文本成功发送给 realtime provider 后才通知，避免 UI 展示未实际发出的内容。
    virtual void OnInterviewerText(const std::string& text) = 0;

    // partial 只用于实时展示；is_final=true 时文本才会进入评分与报告主流程。
    virtual void OnCandidateTranscript(const std::string& text, bool is_final) = 0;
};

} // namespace session
} // namespace interview
