#pragma once

#include <string>
#include <vector>

namespace interview {
namespace services {

// 轻量 HTTP header 结构，便于测试里直接断言鉴权和内容类型是否正确拼装。
struct HttpHeader {
    std::string name;
    std::string value;
};

// HTTP 请求只保留当前阶段真正需要验证的字段：地址、header、body 和超时。
struct HttpRequest {
    std::string url;
    std::vector<HttpHeader> headers;
    std::string body;
    int timeout_ms = 0;
};

// 真实网络细节先收口成最小响应结构，后续接 Boost.Beast 或别的实现时不影响上层测试。
struct HttpResponse {
    int status_code = 0;
    std::string body;
};

// 传输层接口单独抽出来，让 HttpLlmClient 的请求构造和响应解析可以完全离线测试。
class IHttpTransport {
  public:
    virtual ~IHttpTransport() = default;

    // 统一走 JSON POST，后续真实 OpenAI 兼容接口和测试 fake 都复用这个入口。
    virtual HttpResponse postJson(const HttpRequest& request) = 0;
};

} // namespace services
} // namespace interview
