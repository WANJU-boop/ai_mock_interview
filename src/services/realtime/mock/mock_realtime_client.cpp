#include "services/realtime/mock/mock_realtime_client.h"

#include <stdexcept>
#include <utility>

// 这个 mock 复刻 IRealtimeClient 的生命周期约束，而不是只返回固定值：
// connect 后才能读写、事件严格按脚本顺序消费、kClosed/close 后不能继续操作。
// 因此编排层测试能覆盖与真实 WebSocket 相同的关键状态边界，同时完全离线运行。
namespace interview {
namespace services {

MockRealtimeClient::MockRealtimeClient(std::vector<common::RealtimeEvent> scripted_events)
    // 脚本移入对象后由 mock 独占消费位置，调用方无法在运行中改变事件顺序。
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
    // 三个条件共同模拟真实连接：会话已建立、连接仍打开、服务端还有消息。
    return connected_ && !closed_ && next_event_index_ < scripted_events_.size();
}

common::RealtimeEvent MockRealtimeClient::receiveNextEvent() {
    if (!hasNextEvent()) {
        throw std::out_of_range("没有可读取的 realtime mock 事件。");
    }

    const common::RealtimeEvent event = scripted_events_[next_event_index_];
    // 先复制事件再推进游标，返回值不引用内部 vector，调用方可以安全保存或修改它。
    ++next_event_index_;

    if (event.type == common::RealtimeEventType::kClosed) {
        // 服务端关闭事件会立刻停止后续读取，模拟真实连接已经不可再读。
        closed_ = true;
    }

    return event;
}

bool MockRealtimeClient::sendInterviewerText(const std::string& text) {
    if (!connected_ || closed_) {
        // 用 false 表示生命周期错误，让编排层走与真实发送失败相同的错误收口。
        return false;
    }

    // 不模拟 TTS 内容，只记录成功发送顺序；测试通过只读 getter 验证提问和追问。
    outbound_interviewer_messages_.push_back(text);
    return true;
}

void MockRealtimeClient::close() {
    // 单向 closed_ 状态使重复清理安全，也防止 close 后再次 connect 复用旧脚本游标。
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
