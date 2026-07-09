#include "services/realtime/mock/mock_realtime_client.h"

#include <stdexcept>
#include <utility>

namespace interview {
namespace services {

MockRealtimeClient::MockRealtimeClient(std::vector<common::RealtimeEvent> scripted_events)
    : scripted_events_(std::move(scripted_events)) {}

bool MockRealtimeClient::connect() {
    if (closed_) {
        return false;
    }

    // mock 不做网络连接，只记录“已连接”，让编排层按真实客户端的入口执行。
    connected_ = true;
    return true;
}

bool MockRealtimeClient::hasNextEvent() const {
    return connected_ && !closed_ && next_event_index_ < scripted_events_.size();
}

common::RealtimeEvent MockRealtimeClient::receiveNextEvent() {
    if (!hasNextEvent()) {
        throw std::out_of_range("没有可读取的 realtime mock 事件。");
    }

    const common::RealtimeEvent event = scripted_events_[next_event_index_];
    ++next_event_index_;

    if (event.type == common::RealtimeEventType::kClosed) {
        // 服务端关闭事件会立刻停止后续读取，模拟真实连接已经不可再读。
        closed_ = true;
    }

    return event;
}

bool MockRealtimeClient::sendInterviewerText(const std::string& text) {
    if (!connected_ || closed_) {
        return false;
    }

    outbound_interviewer_messages_.push_back(text);
    return true;
}

void MockRealtimeClient::close() {
    closed_ = true;
}

const std::vector<std::string>& MockRealtimeClient::getOutboundInterviewerMessages() const {
    return outbound_interviewer_messages_;
}

bool MockRealtimeClient::isClosed() const {
    return closed_;
}

} // namespace services
} // namespace interview
