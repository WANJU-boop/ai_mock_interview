#include "services/llm_client.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <sstream>
#include <string>
#include <vector>

namespace interview {
namespace services {

namespace {

// 评分 mock 只需要粗略区分回答长度，因此使用空白切分，避免引入自然语言处理依赖。
std::size_t countWords(const std::string& text) {
    std::istringstream stream(text);
    std::size_t word_count = 0;
    std::string word;
    while (stream >> word) {
        ++word_count;
    }

    return word_count;
}

// 关键词匹配统一转小写，保证候选人输入 CLASS、Class、class 时行为一致。
std::string toLowerCopy(const std::string& text) {
    std::string lower_text = text;
    std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower_text;
}

// 只检查是否命中任意技术关键词；mock 的目标是稳定可测，不追求真实语义理解。
bool containsKeyword(const std::string& text, const std::vector<std::string>& keywords) {
    const std::string lower_text = toLowerCopy(text);
    for (const std::string& keyword : keywords) {
        if (lower_text.find(keyword) != std::string::npos) {
            return true;
        }
    }

    return false;
}

// 目标岗位为空时仍返回可用题目，让 CLI demo 或测试配置不完整时也能暴露清晰行为。
std::string normalizeTargetRole(const std::string& target_role) {
    if (target_role.empty()) {
        return "C++ learner";
    }

    return target_role;
}

} // namespace

std::vector<std::string>
MockLlmClient::generateQuestions(const QuestionGenerationRequest& request) {
    if (request.question_count <= 0) {
        // 非法数量不静默生成默认题，避免调用方误以为已经拿到有效 LLM 输出。
        return {};
    }

    const std::string target_role = normalizeTargetRole(request.target_role);
    // 固定题库让 mock 完全确定，单元测试不会受网络、随机数或系统时间影响。
    const std::vector<std::string> question_bank = {
        "Please introduce yourself and explain why you are interested in the " + target_role +
            " role.",
        "Which C++ concept have you practiced recently for the " + target_role + " role?",
        "Describe one project decision you would improve for a " + target_role + " interview.",
        "How do you debug a C++ issue when preparing for the " + target_role + " role?",
        "How do you explain ownership and resource management in C++ for the " + target_role +
            " role?"};

    std::vector<std::string> questions;
    questions.reserve(static_cast<std::size_t>(request.question_count));
    for (int index = 0; index < request.question_count; ++index) {
        std::string question =
            question_bank[static_cast<std::size_t>(index) % question_bank.size()];
        if (static_cast<std::size_t>(index) >= question_bank.size()) {
            // 当请求数量超过模板数量时加上序号，避免 mock 返回完全重复的问题。
            question += " #" + std::to_string(index + 1);
        }
        questions.push_back(question);
    }

    return questions;
}

LlmScoreResult MockLlmClient::scoreAnswer(const AnswerScoringRequest& request) {
    const std::size_t word_count = countWords(request.candidate_answer);
    if (word_count == 0) {
        // 空回答是最重要的边界条件，直接 0 分能让上层流程决定是否结束或提示重答。
        return {0, "No answer provided."};
    }

    // 当前 mock 评分只看回答本身，保证测试不依赖外部模型，也方便后续替换真实实现。
    int score = 20;
    if (request.candidate_answer.size() >= 20) {
        score += 15;
    }
    if (request.candidate_answer.size() >= 50) {
        score += 15;
    }
    if (word_count >= 8) {
        score += 15;
    }
    if (word_count >= 15) {
        score += 10;
    }
    if (request.candidate_answer.size() >= 100) {
        score += 5;
    }
    if (word_count >= 20) {
        score += 5;
    }

    // 技术关键词只作为额外加分信号，不能让非常短的回答靠一个词拿到高分。
    static const std::vector<std::string> kTechnicalKeywords = {
        "c++",   "class",     "memory",   "pointer", "project", "design",
        "debug", "ownership", "template", "testing", "logger",  "performance"};
    if (request.candidate_answer.size() >= 20 &&
        containsKeyword(request.candidate_answer, kTechnicalKeywords)) {
        score += 10;
    }

    // 分数上限固定为 100，避免后续 UI 进度条、报告百分比和追问阈值出现越界处理。
    score = std::min(score, 100);

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

    return {score, feedback};
}

} // namespace services
} // namespace interview
