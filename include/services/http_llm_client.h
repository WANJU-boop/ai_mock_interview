#pragma once

#include "common/config.h"
#include "services/llm_client.h"

#include <memory>
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

// 真实 LLM 客户端负责：
// 1. 把领域请求转成 OpenAI 兼容 JSON 请求体
// 2. 调用注入的 HTTP 传输层
// 3. 把响应解析回当前项目的稳定领域结构
class HttpLlmClient final : public ILlmClient {
  public:
    // transport 必须在构造时注入，避免把“可创建但不可用”的半成品客户端带进主流程。
    explicit HttpLlmClient(const common::LlmConfig& config,
                           std::shared_ptr<IHttpTransport> transport);

    std::vector<std::string> generateQuestions(const QuestionGenerationRequest& request) override;
    LlmScoreResult scoreAnswer(const AnswerScoringRequest& request) override;

  private:
    common::LlmConfig config_;
    std::shared_ptr<IHttpTransport> transport_;
};

} // namespace services
} // namespace interview
