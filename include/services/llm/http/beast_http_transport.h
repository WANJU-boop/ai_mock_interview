#pragma once

#include "services/llm/http/http_transport.h"

namespace interview {
namespace services {

// 基于 Boost.Beast + OpenSSL 的最小同步 HTTPS 传输实现。
// 当前只负责发送 JSON POST，供 HttpLlmClient 在真实 provider 下复用。
class BeastHttpTransport final : public IHttpTransport {
  public:
    // 同步执行一次 HTTPS JSON POST，并把完整网络实现收口成状态码和响应正文。
    // 调用期间当前线程会阻塞到完成或超时；URL、TLS、DNS、连接和读写错误通过异常向上层传播。
    HttpResponse postJson(const HttpRequest& request) override;
};

} // namespace services
} // namespace interview
