#pragma once

#include <cstdint>
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

} // namespace services
} // namespace interview
