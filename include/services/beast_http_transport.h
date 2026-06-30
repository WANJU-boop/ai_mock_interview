#pragma once

#include "services/http_llm_client.h"

namespace interview {
namespace services {

// 基于 Boost.Beast + OpenSSL 的最小同步 HTTPS 传输实现。
// 当前只负责发送 JSON POST，供 HttpLlmClient 在真实 provider 下复用。
class BeastHttpTransport final : public IHttpTransport {
  public:
    HttpResponse postJson(const HttpRequest& request) override;
};

} // namespace services
} // namespace interview
