#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace interview {
namespace session {

class DialogSession;

// 模拟评分结果，同时返回分数和一条可直接展示的反馈语。
struct MockScoreResult {
    int score = 0;
    std::string feedback;
};

// 追问决策结果：说明当前回答是否需要补充追问，以及追问文本。
struct FollowUpDecision {
    bool needs_follow_up = false;
    std::string prompt;
};

// 面试流程管理器负责：
// 1. 持有固定问题列表
// 2. 暴露当前题目
// 3. 记录回答
// 4. 提供简单的规则评分
// 5. 根据评分结果决定是否追问
class InterviewManager {
public:
    explicit InterviewManager(std::vector<std::string> questions);

    // 判断当前是否还有可供提问的问题。
    bool hasCurrentQuestion() const;

    // 返回当前问题指针；如果已经没有问题，则返回 nullptr。
    const std::string* getCurrentQuestion() const;

    // 在存在当前题目的前提下，把回答写入会话对象。
    bool recordCandidateAnswer(DialogSession& session, const std::string& answer);

    // 对单条回答做规则驱动的模拟评分，不修改流程状态。
    MockScoreResult scoreCandidateAnswer(const std::string& answer) const;

    // 根据评分结果决定是否需要追问，以及给出一条固定追问提示。
    FollowUpDecision decideFollowUp(const MockScoreResult& score_result) const;

    // 推进到下一题；如果已经到末尾，则返回 false。
    bool moveToNextQuestion();

    // 返回总题数，主要用于 CLI 展示“第几题/共几题”。
    std::size_t getQuestionCount() const;

private:
    // 固定问题列表，当前 MVP 不接入题目生成服务。
    std::vector<std::string> questions_;
    // 当前正在处理的问题下标。
    std::size_t current_question_index_ = 0;
};

}  // namespace session
}  // namespace interview
