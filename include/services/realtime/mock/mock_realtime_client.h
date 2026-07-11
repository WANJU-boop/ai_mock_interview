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
    // 脚本按值移入 mock，保证调用方后续修改原 vector 不会改变测试事件序列。
    explicit MockRealtimeClient(std::vector<common::RealtimeEvent> scripted_events);

    // mock connect 不访问网络，只在尚未关闭时切换为可读写状态。
    bool connect() override;
    // 只有已连接、未关闭且脚本仍有剩余事件时才允许继续消费。
    bool hasNextEvent() const override;
    // 按顺序返回下一条脚本事件；空读会抛出 std::out_of_range，避免测试静默越界。
    common::RealtimeEvent receiveNextEvent() override;
    // 记录面试官文本供断言；未连接或已关闭时返回 false。
    bool sendInterviewerText(const std::string& text) override;
    // 记录候选人 PCM 块，让音频桥测试可以确认采集数据已进入 realtime 边界。
    bool sendCandidateAudio(const AudioPcmChunk& chunk) override;
    // 把客户端标记为关闭；允许重复调用，模拟真实实现应提供的幂等清理入口。
    void close() override;

    // 读取已发送给 realtime 服务的面试官文本，便于测试确认提问、追问和结束语。
    const std::vector<std::string>& getOutboundInterviewerMessages() const;
    // 返回已发送的候选人音频块；只在 fake 的测试观察路径使用。
    const std::vector<AudioPcmChunk>& getOutboundCandidateAudioChunks() const;

    // 暴露关闭状态，测试可以确认编排层遇到完成或错误时确实收口。
    bool isClosed() const;

  private:
    std::vector<common::RealtimeEvent> scripted_events_;
    std::vector<std::string> outbound_interviewer_messages_;
    std::vector<AudioPcmChunk> outbound_candidate_audio_chunks_;
    std::size_t next_event_index_ = 0;
    bool connected_ = false;
    bool closed_ = false;
};

} // namespace services
} // namespace interview
