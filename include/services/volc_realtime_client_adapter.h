#pragma once

#include "common/realtime_protocol.h"
#include "services/realtime_client.h"
#include "services/volc_realtime_client.h"

#include <deque>
#include <memory>
#include <optional>

namespace interview {
namespace services {

// 把火山供应商 frame 转成项目内部 realtime 事件。
// 无需业务层处理的 ack/结束标记会返回 std::nullopt。
std::optional<common::RealtimeEvent>
mapVolcRealtimeFrameToRealtimeEvent(const VolcRealtimeFrame& frame);

// IRealtimeClient 适配器让 session 层继续依赖项目内部接口，
// 不需要认识火山 event id、payload JSON 或二进制音频 frame。
class VolcRealtimeClientAdapter final : public IRealtimeClient {
  public:
    VolcRealtimeClientAdapter(VolcRealtimeClientConfig config,
                              std::shared_ptr<IVolcRealtimeTransport> transport);

    bool connect() override;
    bool hasNextEvent() const override;
    common::RealtimeEvent receiveNextEvent() override;
    bool sendInterviewerText(const std::string& text) override;
    void close() override;

  private:
    VolcRealtimeClient client_;
    std::deque<common::RealtimeEvent> pending_events_;
    bool connected_ = false;
    bool closed_ = false;
};

} // namespace services
} // namespace interview
