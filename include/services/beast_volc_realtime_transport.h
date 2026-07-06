#pragma once

#include "services/volc_realtime_client.h"

#include <memory>

namespace interview {
namespace services {

// 基于 Boost.Beast 的同步 WSS transport。当前作为手动集成入口使用，
// 默认单元测试只覆盖 VolcRealtimeClient + fake transport，不触发真实网络。
class BeastVolcRealtimeTransport final : public IVolcRealtimeTransport {
  public:
    BeastVolcRealtimeTransport();
    ~BeastVolcRealtimeTransport() override;

    void connect(const VolcRealtimeConnectionRequest& request) override;
    void sendBinary(const std::vector<std::uint8_t>& bytes) override;
    std::vector<std::uint8_t> receiveBinary() override;
    void close() override;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace services
} // namespace interview
