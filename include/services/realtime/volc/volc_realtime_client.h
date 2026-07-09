#pragma once

#include "services/realtime/volc/volc_realtime_protocol.h"
#include "services/realtime/volc/volc_realtime_runtime.h"
#include "services/realtime/volc/volc_realtime_transport.h"

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace services {

// 火山文本模式客户端负责供应商协议流程：
// 1. 建立 WSS 连接并写入鉴权 header
// 2. 发送 StartConnection / StartSession / ChatTextQuery / Finish 事件
// 3. 接收并解码火山 frame
class VolcRealtimeClient final {
  public:
    VolcRealtimeClient(VolcRealtimeRuntimeConfig config,
                       std::shared_ptr<IVolcRealtimeTransport> transport);

    // 只建立 WebSocket；火山业务连接还需要随后发送 StartConnection。
    void connect();
    // 发送连接级开始事件，成功后服务端应返回 ConnectionStarted。
    void startConnection();
    // 发送文本模式 StartSession，成功后服务端应返回 SessionStarted。
    void startTextSession();
    // 让服务端主动说一句欢迎语；当前主流程未使用，保留给手动集成验证。
    void sendSayHello(const std::string& content);
    // 把面试官文本交给火山 TTS 合成。adapter 的 sendInterviewerText 会调用它。
    void sendChatTtsText(const std::string& content);
    // 文本模式下候选人输入一句话，客户端用 ChatTextQuery 交给火山对话模型。
    void sendTextQuery(const std::string& content);
    // 从 transport 读取并解码一个火山 frame；错误 frame 会转成异常。
    VolcRealtimeFrame receiveFrame();
    // 持续读取直到看到指定事件，返回中间所有 frame，便于调用方检查完整服务端响应。
    std::vector<VolcRealtimeFrame> receiveUntilEvent(VolcRealtimeEventId event_id);
    // 文本问答的便利函数：持续读取直到 ChatEnded，表示本轮对话回答结束。
    std::vector<VolcRealtimeFrame> receiveUntilChatEnded();
    // 通知服务端结束当前 session；真正释放 socket 还要调用 finishConnection/close。
    void finishSession();
    // 通知服务端结束 connection 级业务连接。
    void finishConnection();
    // 关闭底层 WebSocket transport。
    void close();

  private:
    // 统一把 JSON 字符串封装成火山事件 frame，避免每个 public 方法重复写协议字段。
    void sendJsonEvent(VolcRealtimeEventId event_id, const std::string& payload);
    // 构造阶段校验配置，尽早暴露缺密钥、缺 session_id 或错误 input_mod。
    void validateConfig() const;

    // 配置按值保存，保证 client 生命周期内不会引用外部临时对象。
    VolcRealtimeRuntimeConfig config_;
    // transport 用 shared_ptr 注入，测试和手动 demo 都可以共享同一个 fake/真实实例观察状态。
    std::shared_ptr<IVolcRealtimeTransport> transport_;
};

} // namespace services
} // namespace interview
