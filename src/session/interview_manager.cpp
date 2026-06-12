#include "session/interview_manager.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>
#include <vector>

#include "common/logger.h"
#include "session/dialog_session.h"

namespace interview {
namespace session {

namespace {

// 用最朴素的按空白切分方式统计单词数，足够支撑当前 mock 评分。
std::size_t countWords(const std::string& text) {
    std::istringstream stream(text);
    std::size_t word_count = 0;
    std::string word;
    while (stream >> word) {
        ++word_count;
    }

    return word_count;
}

// 统一转成小写，避免关键词判断受到大小写影响。
std::string toLowerCopy(const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower_text;
}

// 只要命中任意一个技术关键词，就认为回答体现出一定技术上下文。
bool containsKeyword(const std::string& text, const std::vector<std::string>& keywords) {
    const std::string lower_text = toLowerCopy(text);
    for (const std::string& keyword : keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}

}  // namespace

InterviewManager::InterviewManager(std::vector<std::string> questions) : questions_(std::move(questions)) {
    if (questions_.empty()) {
        LOG_WARN("InterviewManager initialized with an empty question list");
        return;
    }

    LOG_DEBUG("InterviewManager initialized with {} questions", questions_.size());
}

bool InterviewManager::hasCurrentQuestion() const {
    return current_question_index_ < questions_.size();
}

const std::string* InterviewManager::getCurrentQuestion() const {
    if (!hasCurrentQuestion()) {
        LOG_WARN("No current question is available");
        return nullptr;
    }

    return &questions_[current_question_index_];
}

bool InterviewManager::recordCandidateAnswer(DialogSession& session, const std::string& answer) {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to record an answer without an active question");
        return false;
    }

    session.addCandidateAnswer(answer);
    LOG_DEBUG("Recorded candidate answer for question index {}", current_question_index_);
    return true;
}

MockScoreResult InterviewManager::scoreCandidateAnswer(const std::string& answer) const {
    const std::size_t word_count = countWords(answer);
    if (word_count == 0) {
        LOG_WARN("Scored an empty candidate answer");
        return {0, "No answer provided."};
    }

    // 先给出一个基础分，再按“长度 + 词数 + 技术关键词”逐步加分。
    int score = 20;
    if (answer.size() >= 20) {
        score += 15;
    }
    if (answer.size() >= 50) {
        score += 15;
    }
    if (word_count >= 8) {
        score += 15;
    }
    if (word_count >= 15) {
        score += 10;
    }
    if (answer.size() >= 100) {
        score += 5;
    }
    if (word_count >= 20) {
        score += 5;
    }

    static const std::vector<std::string> kTechnicalKeywords = {"c++",      "class",   "memory", "pointer",
                                                                "project",  "design",  "debug",  "ownership",
                                                                "template", "testing", "logger", "performance"};
    // 只有回答本身不太短时，技术关键词加分才有意义，避免一句空话被抬高。
    if (answer.size() >= 20 && containsKeyword(answer, kTechnicalKeywords)) {
        score += 10;
    }

    // 模拟评分也保持 100 分上限，方便后续替换成真实评分服务。
    score = std::min(score, 100);

    // 把整数分数映射成一条可以直接展示给用户的反馈语。
    std::string feedback;
    if (score >= 85) {
        feedback = "Strong answer with concrete detail.";
    } else if (score >= 65) {
        feedback = "Good answer, but add one concrete example.";
    } else if (score >= 40) {
        feedback = "Basic answer, but it needs more detail.";
    } else {
        feedback = "Answer is too short. Add more detail.";
    }

    LOG_DEBUG("Scored candidate answer: score={}, words={}", score, word_count);
    return {score, feedback};
}

FollowUpDecision InterviewManager::decideFollowUp(const MockScoreResult& score_result) const {
    if (score_result.score < 70 || score_result.score >= 90) {
        LOG_DEBUG("No follow-up needed for score {}", score_result.score);
        return {false, ""};
    }

    if (score_result.feedback == "Good answer, but add one concrete example.") {
        LOG_DEBUG("Follow-up requested for score {} with example prompt", score_result.score);
        return {true, "Could you give one concrete example from your project or practice?"};
    }

    LOG_DEBUG("Follow-up requested for score {} with detail prompt", score_result.score);
    return {true, "Could you explain one specific design choice or tradeoff in more detail?"};
}

bool InterviewManager::moveToNextQuestion() {
    if (!hasCurrentQuestion()) {
        LOG_WARN("Attempted to move past the available questions");
        return false;
    }

    ++current_question_index_;
    if (hasCurrentQuestion()) {
        // 只要还有题，返回 true，调用方就可以继续下一轮问答。
        LOG_DEBUG("Moved to question index {}", current_question_index_);
        return true;
    }

    // 下标已经越过最后一题，说明本轮面试流程已问完所有问题。
    LOG_DEBUG("InterviewManager reached the end of the question list");
    return false;
}

std::size_t InterviewManager::getQuestionCount() const {
    return questions_.size();
}

}  // namespace session
}  // namespace interview
