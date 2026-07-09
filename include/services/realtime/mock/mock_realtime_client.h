#pragma once

#include "services/realtime/realtime_client.h"

#include <cstddef>
#include <string>
#include <vector>

namespace interview {
namespace services {

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
