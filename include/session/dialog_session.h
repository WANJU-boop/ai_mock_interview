#pragma once

// clang-format off
#include <cstddef>
#include <string>
#include <vector>

#include "session/interview_state.h"
// clang-format on

namespace interview {
namespace session {

// 保存一题回答对应的评分结果，供总结阶段直接展示。
struct ScoreResultRecord {
    // 分数约定为 0..100；具体校验由评分服务负责。
    int score = 0;
    // 面向候选人的简短反馈，可由 CLI、报告或未来 UI 直接展示。
    std::string feedback;
};

// 保存一题完整问答记录，避免把追问内容拼进普通回答字符串里。
struct QuestionAnswerRecord {
    // 主问题和主回答始终成对保存，保持报告中的题目归属。
    std::string question;
    std::string candidate_answer;
    // false 时追问字段保持空字符串，调用方不需要使用魔法值判断。
    bool has_follow_up = false;
    std::string follow_up_prompt;
    std::string follow_up_answer;
    // 如果发生追问，这里保存组合主回答和追问回答后得到的最终评分。
    ScoreResultRecord final_score;
};

// 对话会话负责保存一次文字面试中的最小状态：
// 1. 当前所处的状态枚举
// 2. 候选人的回答历史
// 3. 每道题对应的评分结果历史
// 4. 每道题的结构化问答记录
class DialogSession {
  public:
    // 默认从“连接中”状态开始，回答历史为空。
    DialogSession();

    // 读取当前会话状态，供主流程决定下一步行为。
    InterviewState getState() const;

    // 更新当前会话状态，例如切到空闲、思考或结束阶段。
    void setState(InterviewState state);

    // 当状态进入完成或错误时，认为这次会话已经结束。
    bool isFinished() const;

    // 追加一条候选人回答，保持输入顺序不变。
    void addCandidateAnswer(const std::string& answer);

    // 只读返回整段回答历史，供总结阶段统一展示。
    const std::vector<std::string>& getCandidateAnswers() const;

    // 返回当前已保存的回答数量，便于测试和流程统计。
    std::size_t getCandidateAnswerCount() const;

    // 追加一条评分结果，保持和题目、回答相同的顺序。
    void addScoreResult(int score, const std::string& feedback);

    // 只读返回整段评分历史，供总结阶段统一展示。
    const std::vector<ScoreResultRecord>& getScoreResults() const;

    // 返回当前已保存的评分数量，便于测试和流程统计。
    std::size_t getScoreResultCount() const;

    // 追加一题完整问答记录，供总结和后续报告功能直接读取。
    void addQuestionAnswerRecord(const QuestionAnswerRecord& record);

    // 只读返回完整问答记录历史，避免调用方修改会话内部状态。
    const std::vector<QuestionAnswerRecord>& getQuestionAnswerRecords() const;

    // 返回当前已保存的完整问答记录数量，便于测试和流程统计。
    std::size_t getQuestionAnswerRecordCount() const;

  private:
    // 当前流程状态，默认从连接阶段开始。
    InterviewState current_state_ = InterviewState::kConnecting;
    // 依次保存用户在每一道题下输入的回答文本。
    std::vector<std::string> candidate_answers_;
    // 依次保存每道题评分后的分数和简短反馈。
    std::vector<ScoreResultRecord> score_results_;
    // 依次保存每道题的主问题、追问、回答和最终评分。
    std::vector<QuestionAnswerRecord> question_answer_records_;
};

} // namespace session
} // namespace interview
