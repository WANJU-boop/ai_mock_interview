#pragma once

#include "services/llm/llm_client.h"

namespace interview {
namespace services {

// Mock 实现用于当前阶段的确定性测试；真实 HTTP 客户端单独放在 http provider 目录。
class MockLlmClient final : public ILlmClient {
  public:
    std::vector<std::string> generateQuestions(const QuestionGenerationRequest& request) override;
    LlmScoreResult scoreAnswer(const AnswerScoringRequest& request) override;
};

} // namespace services
} // namespace interview
