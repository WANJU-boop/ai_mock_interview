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

    // 把领域请求转换成结构化 chat completions 请求，再校验并返回题目数组。
    // 网络、HTTP 状态码或响应格式错误会通过异常交给启动层统一收口。
    std::vector<std::string> generateQuestions(const QuestionGenerationRequest& request) override;
    // 发送一条结构化评分请求；返回前保证分数位于 0..100 且反馈非空。
    LlmScoreResult scoreAnswer(const AnswerScoringRequest& request) override;

  private:
    // 配置按值保存，保证客户端不会引用入口层的临时配置对象。
    common::LlmConfig config_;
    // shared_ptr 允许测试持有同一个 fake transport 并在调用后检查收到的请求。
    std::shared_ptr<IHttpTransport> transport_;
};

} // namespace services
} // namespace interview
