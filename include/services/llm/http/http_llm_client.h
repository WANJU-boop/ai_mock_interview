#pragma once

#include "common/config.h"
#include "services/llm/http/http_transport.h"
#include "services/llm/llm_client.h"

#include <memory>

namespace interview {
namespace services {

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
