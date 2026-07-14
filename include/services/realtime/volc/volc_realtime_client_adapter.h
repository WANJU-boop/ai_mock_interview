#pragma once

#include "common/realtime_protocol.h"
#include "services/realtime/realtime_client.h"
#include "services/realtime/volc/volc_realtime_client.h"

#include <deque>
#include <memory>
#include <optional>

namespace interview {
namespace services {

// 把火山供应商 frame 转成项目内部 realtime 事件。
// 无需业务层处理的 ack/结束标记会返回 std::nullopt。
// 这个函数是供应商协议和项目业务事件之间的翻译表，单元测试会直接覆盖它。
std::optional<common::RealtimeEvent>
mapVolcRealtimeFrameToRealtimeEvent(const VolcRealtimeFrame& frame);

// IRealtimeClient 适配器让 session 层继续依赖项目内部接口，
// 不需要认识火山 event id、payload JSON 或二进制音频 frame。
class VolcRealtimeClientAdapter final : public IRealtimeClient {
  public:
    VolcRealtimeClientAdapter(VolcRealtimeRuntimeConfig config,
                              std::shared_ptr<IVolcRealtimeTransport> transport);

    // 建立火山连接和 session，并把成功状态转换成项目内部 kConnected。
    bool connect() override;
    // 告诉 DialogOrchestrator 是否还能继续读事件；收到错误或关闭事件后返回 false。
    bool hasNextEvent() const override;
    // 暴露已脱敏的 Beast/火山失败原因，不返回请求 header。
    std::string getLastErrorMessage() const override;
    // 读取下一条项目内部事件，内部会跳过不需要业务层处理的火山 ack。
    common::RealtimeEvent receiveNextEvent() override;
    // 只消费已经到达的 frame；没有数据时立即返回，避免阻塞麦克风 PCM 的持续发送。
    std::optional<common::RealtimeEvent> tryReceiveNextEvent() override;
    // 发送面试官文本，内部转成火山 ChatTTSText。
    bool sendInterviewerText(const std::string& text) override;
    // 把设备无关的 int16 PCM 编码成小端字节，再转成火山 AudioOnlyRequest。
    bool sendCandidateAudio(const AudioPcmChunk& chunk) override;
    // 收口 session、connection 和底层 WebSocket；关闭阶段尽量不再向上抛异常。
    void close() override;

  private:
    // 对单个供应商 frame 做有状态转换。ASR 最终文本先缓存，到 ASREnded 后才交给业务层，
    // 避免在供应商仍处理本轮语音时过早发送 ChatTTSText。
    std::optional<common::RealtimeEvent> convertFrame(const VolcRealtimeFrame& frame);
    // 弹出 connect 等本地预生成事件；队列为空时返回 std::nullopt。
    std::optional<common::RealtimeEvent> popPendingEvent();

    // 真实供应商客户端，负责火山事件发包和收包；adapter 只做接口转换。
    VolcRealtimeClient client_;
    // connect 成功后要先给业务层一个 kConnected，这个队列保存这类已经生成但尚未消费的事件。
    std::deque<common::RealtimeEvent> pending_events_;
    // 服务端可能先给 final ASRResponse，再给 ASREnded。只有二者都到齐才推进面试评分。
    std::optional<common::RealtimeEvent> pending_final_transcript_;
    // 火山会自动生成 default 闲聊 TTS，本项目只播放自己发送的 chat_tts_text，
    // 否则会同时听到火山回答和本地面试状态机的下一题。
    bool forward_current_tts_audio_ = false;
    // 开场第一段文本必须用 SayHello，ChatTTSText 只能在 ASREnded 后发送。
    bool should_send_say_hello_ = true;
    // SayHello 的 TTS 类型可能是 default，只在明确等待开场语时允许该类型播放。
    bool expecting_say_hello_tts_ = false;
    // connected_ 表示握手和 StartSession 已成功，不等同于 WebSocket 对象一定仍打开。
    bool connected_ = false;
    // closed_ 表示 adapter 事件流已经结束，DialogOrchestrator 不应再继续读取。
    bool closed_ = false;
    // 只保存 exception::what() 或服务端错误文本，从不拼接鉴权 header。
    std::string last_error_message_;
};

} // namespace services
} // namespace interview
