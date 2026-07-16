#pragma once

#include <atomic>

namespace interview {
namespace session {

// 跨线程取消令牌只传递一个原子布尔值，不直接关闭 WebSocket 或 PortAudio。
// 因此真正的资源清理仍由唯一 realtime worker 执行，避免 UI 线程并发操作连接。
class DialogCancellationToken final {
  public:
    // 可从 UI 主线程调用；重复请求保持幂等。
    void RequestStop();

    // worker 在非阻塞事件循环中轮询该值，并在安全点统一停止音频和连接。
    bool IsStopRequested() const;

  private:
    std::atomic_bool stop_requested_{false};
};

} // namespace session
} // namespace interview
