#pragma once

#include "services/volc_realtime_protocol.h"

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace services {

struct VolcRealtimeHeader {
    std::string name;
    std::string value;
};

struct VolcRealtimeConnectionRequest {
    std::string url;
    std::vector<VolcRealtimeHeader> headers;
    int timeout_ms = 30000;
};

// 火山 Realtime 配置只保存调用必需字段；真实密钥由入口层从环境变量读取后注入。
struct VolcRealtimeClientConfig {
    std::string endpoint = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    std::string app_id;
    std::string access_key;
    std::string resource_id = "volc.speech.dialog";
    std::string app_key = "PlgvMymc7f3tQnJ6";
    std::string connect_id;
    std::string session_id;
    std::string model = "1.2.1.1";
    std::string input_mod = "text";
    std::string speaker = "zh_female_vv_jupiter_bigtts";
    int timeout_ms = 30000;
};

// WebSocket 传输抽象用于隔离真实网络；单元测试通过 fake transport 验证发包顺序和内容。
class IVolcRealtimeTransport {
  public:
    virtual ~IVolcRealtimeTransport() = default;

    virtual void connect(const VolcRealtimeConnectionRequest& request) = 0;
    virtual void sendBinary(const std::vector<std::uint8_t>& bytes) = 0;
    virtual std::vector<std::uint8_t> receiveBinary() = 0;
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

    void connect();
    void startConnection();
    void startTextSession();
    void sendSayHello(const std::string& content);
    void sendChatTtsText(const std::string& content);
    void sendTextQuery(const std::string& content);
    VolcRealtimeFrame receiveFrame();
    std::vector<VolcRealtimeFrame> receiveUntilEvent(VolcRealtimeEventId event_id);
    std::vector<VolcRealtimeFrame> receiveUntilChatEnded();
    void finishSession();
    void finishConnection();
    void close();

  private:
    void sendJsonEvent(VolcRealtimeEventId event_id, const std::string& payload);
    void validateConfig() const;

    VolcRealtimeClientConfig config_;
    std::shared_ptr<IVolcRealtimeTransport> transport_;
};

} // namespace services
} // namespace interview
