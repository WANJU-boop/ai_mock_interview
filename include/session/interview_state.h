#pragma once

namespace interview {
namespace session {

// 文字版面试流程的核心状态枚举。
enum class InterviewState {
    // 准备会话资源、建立上下文的阶段。
    kConnecting,
    // 面试官正在输出开场白、题目或总结。
    kInterviewerSpeaking,
    // 当前没有人说话，系统等待下一步动作。
    kIdle,
    // 候选人正在输入回答。
    kCandidateSpeaking,
    // 系统正在处理回答，例如评分或决定后续流程。
    kInterviewerThinking,
    // 开始做结束收尾，例如打印总结。
    kSessionEnding,
    // 一次会话已正常完成。
    kCompleted,
    // 流程中遇到异常，无法继续。
    kError,
};

}  // namespace session
}  // namespace interview
