#pragma once

#include <string>
#include <vector>

namespace interview {
namespace services {

// 题目生成请求只保存生成问题所需的安全上下文，不包含 API key 或完整简历正文。
struct QuestionGenerationRequest {
    // 候选人姓名当前仅作为上下文预留，mock 不直接拼进题目，避免测试暴露个人信息。
    std::string candidate_name;
    // 目标岗位会影响 mock 题目文本，用来验证请求参数确实参与了生成逻辑。
    std::string target_role;
    // 请求生成的问题数量；小于等于 0 时 mock 会返回空列表，调用方可明确处理无题状态。
    int question_count = 0;
    // 可选简历摘要上下文，只用于定制题目，不应该被日志或报告完整输出。
    std::string resume_context;
};

// 评分请求保留题目和候选人回答，后续真实 LLM 客户端可以基于同一接口替换 mock。
struct AnswerScoringRequest {
    // 当前 mock 评分暂不解析题目内容，但真实客户端会需要它构造评分上下文。
    std::string question;
    // 候选人回答是评分主体；测试中只使用短文本，避免单元测试保存敏感长回答。
    std::string candidate_answer;
};

// 统一的评分结果让 mock 和未来真实 LLM 客户端保持相同返回形状。
struct LlmScoreResult {
    // 分数始终约束在 0 到 100，方便 UI、报告和追问策略统一处理。
    int score = 0;
    // 反馈语用于 CLI 或未来 UI 直接展示，mock 中保持固定文本便于测试断言。
    std::string feedback;
};

// LLM 客户端接口隔离外部模型能力，让领域层和测试都不需要直接依赖网络实现。
class ILlmClient {
  public:
    virtual ~ILlmClient() = default;

    // 根据岗位和题目数量生成面试问题；接口层不规定真实来源，便于 mock/HTTP 互换。
    virtual std::vector<std::string>
    generateQuestions(const QuestionGenerationRequest& request) = 0;
    // 对单条回答评分；当前返回同步结果，后续真实网络实现再单独处理超时和错误。
    virtual LlmScoreResult scoreAnswer(const AnswerScoringRequest& request) = 0;
};

} // namespace services
} // namespace interview
