#pragma once

#include <string>
#include <vector>

namespace interview {
namespace services {

// 轻量 HTTP header 结构，便于测试里直接断言鉴权和内容类型是否正确拼装。
struct HttpHeader {
    // name/value 原样交给具体传输实现；调用方负责避免记录敏感鉴权值。
    std::string name;
    std::string value;
};

// HTTP 请求只保留当前阶段真正需要验证的字段：地址、header、body 和超时。
struct HttpRequest {
    // 真实传输只接受 HTTPS URL，避免 API key 和候选人回答明文传输。
    std::string url;
    // headers 包含 Content-Type 和 Authorization；不得完整输出到日志。
    std::vector<HttpHeader> headers;
    // body 是序列化后的 JSON；可能含简历上下文或候选人回答，同样不得记录全文。
    std::string body;
    // 同步连接、写入和读取阶段使用的毫秒超时。
    int timeout_ms = 0;
};

// 真实网络细节先收口成最小响应结构，后续接 Boost.Beast 或别的实现时不影响上层测试。
struct HttpResponse {
    // 保留原始 HTTP 状态码，由 HttpLlmClient 统一判断 2xx 与错误响应。
    int status_code = 0;
    // 响应正文保持字符串，结构化 JSON 解析属于 LLM 客户端职责。
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
