#include "session/dialog_session.h"

namespace interview {
namespace session {

// 默认构造即可满足当前 MVP：状态和回答历史都由成员默认值初始化。
DialogSession::DialogSession() = default;

InterviewState DialogSession::getState() const {
    return current_state_;
}

void DialogSession::setState(InterviewState state) {
    current_state_ = state;
}

bool DialogSession::isFinished() const {
    // 只有正常完成或错误终止两种情况，才认为这次会话结束。
    return current_state_ == InterviewState::kCompleted || current_state_ == InterviewState::kError;
}

void DialogSession::addCandidateAnswer(const std::string& answer) {
    // 直接追加到尾部，保证问题顺序和回答顺序一一对应。
    candidate_answers_.push_back(answer);
}

const std::vector<std::string>& DialogSession::getCandidateAnswers() const {
    return candidate_answers_;
}

std::size_t DialogSession::getCandidateAnswerCount() const {
    return candidate_answers_.size();
}

void DialogSession::addScoreResult(int score, const std::string& feedback) {
    score_results_.push_back({score, feedback});
}

const std::vector<ScoreResultRecord>& DialogSession::getScoreResults() const {
    return score_results_;
}

std::size_t DialogSession::getScoreResultCount() const {
    return score_results_.size();
}

void DialogSession::addQuestionAnswerRecord(const QuestionAnswerRecord& record) {
    question_answer_records_.push_back(record);
}

const std::vector<QuestionAnswerRecord>& DialogSession::getQuestionAnswerRecords() const {
    return question_answer_records_;
}

std::size_t DialogSession::getQuestionAnswerRecordCount() const {
    return question_answer_records_.size();
}

}  // namespace session
}  // namespace interview
