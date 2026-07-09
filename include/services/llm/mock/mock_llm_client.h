#pragma once

#include "services/llm/llm_client.h"

namespace interview {
namespace services {

// Mock 实现用于当前阶段的确定性测试；真实 HTTP 客户端单独放在 http provider 目录。
class MockLlmClient final : public ILlmClient {
  public:
    // 从固定题库按请求数量生成题目；不联网、不使用随机数，便于测试稳定断言。
    std::vector<std::string> generateQuestions(const QuestionGenerationRequest& request) override;
    // 按回答长度和技术关键词执行确定性评分，用于驱动主流程的评分与追问分支。
    LlmScoreResult scoreAnswer(const AnswerScoringRequest& request) override;
};

} // namespace services
} // namespace interview
