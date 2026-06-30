#include "session/interview_manager.h"

#include "common/logger.h"
#include "session/dialog_session.h"

#include <utility>

namespace interview {
namespace session {

InterviewManager::InterviewManager(std::vector<std::string> questions,
                                   services::ILlmClient& llm_client)
    : llm_client_(llm_client), questions_(std::move(questions)) {
    if (questions_.empty()) {
        LOG_WARN("面试管理器使用空题目列表初始化");
        return;
    }

    LOG_DEBUG("面试管理器已初始化，题目数量：{}", questions_.size());
}

bool InterviewManager::hasCurrentQuestion() const {
    return current_question_index_ < questions_.size();
}

const std::string* InterviewManager::getCurrentQuestion() const {
    if (!hasCurrentQuestion()) {
        LOG_WARN("当前没有可用题目");
        return nullptr;
    }

    return &questions_[current_question_index_];
}

bool InterviewManager::recordCandidateAnswer(DialogSession& session, const std::string& answer) {
    if (!hasCurrentQuestion()) {
        LOG_WARN("尝试在没有当前题目时记录回答");
        return false;
    }

    session.addCandidateAnswer(answer);
    LOG_DEBUG("已记录第 {} 题的候选人回答", current_question_index_);
    return true;
}

services::LlmScoreResult InterviewManager::scoreCandidateAnswer(const std::string& answer) const {
    if (!hasCurrentQuestion()) {
        LOG_WARN("尝试在没有当前题目时评分回答");
        return {0, "没有可评分的当前题目。"};
    }

    // 评分上下文统一从当前题目读取，后续切到真实 LLM 时也不需要改主流程调用点。
    const services::LlmScoreResult result =
        llm_client_.scoreAnswer({questions_[current_question_index_], answer});
    LOG_DEBUG("已评分第 {} 题的候选人回答，得分：{}", current_question_index_, result.score);
    return result;
}

FollowUpDecision
InterviewManager::decideFollowUp(const services::LlmScoreResult& score_result) const {
    if (score_result.score < 70 || score_result.score >= 90) {
        LOG_DEBUG("得分 {} 不需要追问", score_result.score);
        return {false, ""};
    }

    // 追问策略只依赖分数段，不依赖展示给用户的反馈文案，避免中文化或真实 LLM 文案变化影响流程。
    if (score_result.score < 85) {
        LOG_DEBUG("得分 {} 触发例子追问", score_result.score);
        return {true, "能不能补充一个来自项目或练习的具体例子？"};
    }

    LOG_DEBUG("得分 {} 触发细节追问", score_result.score);
    return {true, "能不能再展开一个具体设计选择或取舍？"};
}

bool InterviewManager::moveToNextQuestion() {
    if (!hasCurrentQuestion()) {
        LOG_WARN("尝试越过可用题目范围");
        return false;
    }

    ++current_question_index_;
    if (hasCurrentQuestion()) {
        // 只要还有题，返回 true，调用方就可以继续下一轮问答。
        LOG_DEBUG("已移动到第 {} 题", current_question_index_);
        return true;
    }

    // 下标已经越过最后一题，说明本轮面试流程已问完所有问题。
    LOG_DEBUG("面试管理器已到达题目列表末尾");
    return false;
}

std::size_t InterviewManager::getQuestionCount() const {
    return questions_.size();
}

} // namespace session
} // namespace interview
