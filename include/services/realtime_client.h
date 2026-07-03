#pragma once

#include "common/realtime_protocol.h"

#include <cstddef>
#include <string>
#include <vector>

namespace interview {
namespace services {

// Realtime 客户端接口隔离语音服务能力，让 session 层只处理业务事件，不直接依赖 WebSocket。
class IRealtimeClient {
  public:
    virtual ~IRealtimeClient() = default;

    // 建立 realtime 会话。mock 只切换内部状态，真实实现后续再建立 WSS 连接。
    virtual bool connect() = 0;

    // 判断是否还有可消费的服务端事件；真实实现可以映射为阻塞读取或事件队列。
    virtual bool hasNextEvent() const = 0;

    // 取出下一条业务事件。调用方应先检查 hasNextEvent，避免空队列读取。
    virtual common::RealtimeEvent receiveNextEvent() = 0;

    // 发送面试官文本；后续真实实现会把它转成 TTS 或服务端 response 指令。
    virtual bool sendInterviewerText(const std::string& text) = 0;

    // 主动关闭 realtime 会话，确保后续 WebSocket 实现有明确的停止入口。
    virtual void close() = 0;
};

// 确定性 mock：按脚本顺序吐出事件，并记录面试官发出的文本。
// 单元测试用它覆盖 realtime 主流程，避免默认测试依赖网络、麦克风或服务端账号。
class MockRealtimeClient final : public IRealtimeClient {
  public:
    explicit MockRealtimeClient(std::vector<common::RealtimeEvent> scripted_events);

    bool connect() override;
    bool hasNextEvent() const override;
    common::RealtimeEvent receiveNextEvent() override;
    bool sendInterviewerText(const std::string& text) override;
    void close() override;

    // 读取已发送给 realtime 服务的面试官文本，便于测试确认提问、追问和结束语。
    const std::vector<std::string>& getOutboundInterviewerMessages() const;

    // 暴露关闭状态，测试可以确认编排层遇到完成或错误时确实收口。
    bool isClosed() const;

  private:
    std::vector<common::RealtimeEvent> scripted_events_;
    std::vector<std::string> outbound_interviewer_messages_;
    std::size_t next_event_index_ = 0;
    bool connected_ = false;
    bool closed_ = false;
};

} // namespace services
} // namespace interview
