#pragma once

#include "services/volc_realtime_protocol.h"

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace services {

struct VolcRealtimeHeader {
    // WebSocket 握手 header 名称，例如 X-Api-App-ID。
    std::string name;
    // WebSocket 握手 header 值。测试里只用假值，真实值由入口层从环境变量注入。
    std::string value;
};

// transport connect 所需的最小请求对象。把 URL、header、timeout 打包成结构体，
// 是为了让 VolcRealtimeClient 不直接依赖 Boost.Beast，也方便测试检查握手参数。
struct VolcRealtimeConnectionRequest {
    // 火山 realtime WSS 地址，真实实现只接受 wss://，避免明文 WebSocket 传输密钥。
    std::string url;
    // 鉴权和追踪 header。不要在日志里打印完整 header，里面包含 access key。
    std::vector<VolcRealtimeHeader> headers;
    // 同步 WebSocket 操作的超时时间，防止手动集成 demo 永久卡住。
    int timeout_ms = 30000;
};

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

// WebSocket 传输抽象用于隔离真实网络；单元测试通过 fake transport 验证发包顺序和内容。
class IVolcRealtimeTransport {
  public:
    virtual ~IVolcRealtimeTransport() = default;

    // 建立底层 WSS 连接并完成握手。真实实现会联网；测试 fake 只记录 request。
    virtual void connect(const VolcRealtimeConnectionRequest& request) = 0;
    // 发送一个已经编码好的火山二进制 frame。VolcRealtimeClient 不关心 socket 细节。
    virtual void sendBinary(const std::vector<std::uint8_t>& bytes) = 0;
    // 阻塞读取一个完整 WebSocket binary message，再交给协议层解码。
    virtual std::vector<std::uint8_t> receiveBinary() = 0;
    // 关闭底层连接。调用方可以多次清理，真实实现应尽量做到幂等。
    virtual void close() = 0;
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
