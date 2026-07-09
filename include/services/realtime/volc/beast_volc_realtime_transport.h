#pragma once

#include "services/realtime/volc/volc_realtime_transport.h"

#include <memory>

namespace interview {
namespace services {

// 基于 Boost.Beast 的同步 WSS transport。当前作为手动集成入口使用，
// 默认单元测试只覆盖 VolcRealtimeClient + fake transport，不触发真实网络。
class BeastVolcRealtimeTransport final : public IVolcRealtimeTransport {
  public:
    // 构造时只准备实现对象，不联网；真实连接延迟到 connect()。
    BeastVolcRealtimeTransport();
    // 析构通过 PImpl 自动释放 WebSocket、TLS 和底层 I/O 资源。
    ~BeastVolcRealtimeTransport() override;

    // 同步完成 DNS、TCP、TLS 和 WebSocket 握手；鉴权 header 只写入握手请求，不写日志。
    void connect(const VolcRealtimeConnectionRequest& request) override;
    // 把一个完整火山协议 frame 作为 WebSocket binary message 同步发送。
    void sendBinary(const std::vector<std::uint8_t>& bytes) override;
    // 阻塞读取一个完整 binary message；网络或协议层错误由异常向上层传播。
    std::vector<std::uint8_t> receiveBinary() override;
    // 尝试正常关闭连接并释放资源；允许异常路径重复调用。
    void close() override;

  private:
    // PImpl 把 Boost.Beast / OpenSSL 头文件细节藏到 .cpp，减少其他编译单元的依赖和编译噪音。
    class Impl;
    // Impl 由 transport 独占拥有；析构时自动释放 socket、SSL context 等资源。
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
