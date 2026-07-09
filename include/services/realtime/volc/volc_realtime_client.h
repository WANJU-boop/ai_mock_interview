#pragma once

#include "services/realtime/volc/volc_realtime_protocol.h"
#include "services/realtime/volc/volc_realtime_transport.h"

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace services {

// 火山 Realtime 配置只保存调用必需字段；真实密钥由入口层从环境变量读取后注入。
struct VolcRealtimeClientConfig {
    // 服务端 WSS endpoint。默认值来自火山实时对话文档，测试可覆盖成假地址。
    std::string endpoint = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    // 火山控制台分配的 App ID。不能写死真实值，必须由环境变量或本地忽略配置注入。
    std::string app_id;
    // 火山访问密钥。它是敏感信息，只能在内存里传递，不能提交到仓库或打印到日志。
    std::string access_key;
    // 火山资源 ID，用于告诉服务端当前调用实时对话能力。
    std::string resource_id = "volc.speech.dialog";
    // 文档要求的 App Key。这里保留公开默认值；如果账号侧要求变更，入口层可以覆盖。
    std::string app_key = "PlgvMymc7f3tQnJ6";
    // 连接级追踪 ID，放在握手 header 里，便于服务端排查一次 WebSocket 连接。
    std::string connect_id;
    // 会话级 ID，StartSession、ChatTextQuery、TTS 等会话事件都要携带。
    std::string session_id;
    // 火山 realtime 模型版本。先放在配置里，后续切模型时不用改协议代码。
    std::string model = "1.2.1.1";
    // 当前客户端只实现 text 模式；audio 模式要等音频边界和 PortAudio 完成后再扩展。
    std::string input_mod = "text";
    // TTS speaker，用于 ChatTTSText 或后续服务端播报。
    std::string speaker = "zh_female_vv_jupiter_bigtts";
    // 网络超时，传给具体 transport。
    int timeout_ms = 30000;
};

// 火山文本模式客户端负责供应商协议流程：
// 1. 建立 WSS 连接并写入鉴权 header
// 2. 发送 StartConnection / StartSession / ChatTextQuery / Finish 事件
// 3. 接收并解码火山 frame
class VolcRealtimeClient final {
  public:
    VolcRealtimeClient(VolcRealtimeClientConfig config,
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
    VolcRealtimeClientConfig config_;
    // transport 用 shared_ptr 注入，测试和手动 demo 都可以共享同一个 fake/真实实例观察状态。
    std::shared_ptr<IVolcRealtimeTransport> transport_;
};

} // namespace services
} // namespace interview
