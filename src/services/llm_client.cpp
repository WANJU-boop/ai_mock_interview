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

bool isUtf8ContinuationByte(unsigned char ch) {
    return (ch & 0xC0) == 0x80;
}

std::size_t utf8CharacterWidth(unsigned char ch) {
    if ((ch & 0x80) == 0) {
        return 1;
    }
    if ((ch & 0xE0) == 0xC0) {
        return 2;
    }
    if ((ch & 0xF0) == 0xE0) {
        return 3;
    }
    if ((ch & 0xF8) == 0xF0) {
        return 4;
    }

    return 1;
}

// 中文回答通常没有空格，不能只用英文单词数判断详细程度。
// 这里统计有效的多字节 UTF-8 字符，作为中文和全角文本的轻量长度信号。
std::size_t countWideUtf8Characters(const std::string& text) {
    std::size_t wide_character_count = 0;
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char ch = static_cast<unsigned char>(text[index]);
        if ((ch & 0x80) == 0) {
            ++index;
            continue;
        }

        const std::size_t width = utf8CharacterWidth(ch);
        if (width == 1 || index + width > text.size()) {
            ++index;
            continue;
        }

        bool valid_character = true;
        for (std::size_t offset = 1; offset < width; ++offset) {
            const unsigned char continuation = static_cast<unsigned char>(text[index + offset]);
            if (!isUtf8ContinuationByte(continuation)) {
                valid_character = false;
                break;
            }
        }

        if (!valid_character) {
            ++index;
            continue;
        }

        ++wide_character_count;
        index += width;
    }

    return wide_character_count;
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
        return "C++ 学习者";
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
        "请先做一个简短自我介绍，并说明你为什么想面试“" + target_role + "”。",
        "最近你练习过哪个 C++ 概念？请结合“" + target_role + "”岗位说明。",
        "如果复盘一个项目决定，你会怎样改进它来准备“" + target_role + "”面试？",
        "准备“" + target_role + "”岗位时，你通常怎样定位和调试一个 C++ 问题？",
        "面向“" + target_role + "”岗位，你会怎样解释 C++ 的所有权和资源管理？"};

    std::vector<std::string> questions;
    questions.reserve(static_cast<std::size_t>(request.question_count));
    for (int index = 0; index < request.question_count; ++index) {
        std::string question =
            question_bank[static_cast<std::size_t>(index) % question_bank.size()];
        if (static_cast<std::size_t>(index) >= question_bank.size()) {
            // 当请求数量超过模板数量时加上序号，避免 mock 返回完全重复的问题。
            question += "（扩展题 #" + std::to_string(index + 1) + "）";
        }
        questions.push_back(question);
    }

    return questions;
}

LlmScoreResult MockLlmClient::scoreAnswer(const AnswerScoringRequest& request) {
    const std::size_t word_count = countWords(request.candidate_answer);
    const std::size_t wide_character_count = countWideUtf8Characters(request.candidate_answer);
    if (word_count == 0 && wide_character_count == 0) {
        // 空回答是最重要的边界条件，直接 0 分能让上层流程决定是否结束或提示重答。
        return {0, "未提供回答。"};
    }

    // 当前 mock 评分只看回答本身；英文看单词数，中文看多字节字符数，保证两种输入都稳定可测。
    int score = 20;
    if (request.candidate_answer.size() >= 20) {
        score += 15;
    }
    if (request.candidate_answer.size() >= 50) {
        score += 15;
    }
    if (word_count >= 8 || wide_character_count >= 12) {
        score += 15;
    }
    if (word_count >= 15 || wide_character_count >= 24) {
        score += 10;
    }
    if (request.candidate_answer.size() >= 100) {
        score += 5;
    }
    if (word_count >= 20 || wide_character_count >= 40) {
        score += 5;
    }

    // 技术关键词只作为额外加分信号，不能让非常短的回答靠一个词拿到高分。
    static const std::vector<std::string> kTechnicalKeywords = {
        "c++",  "class",     "memory",   "pointer", "project", "design",      "debug",
        "raii", "ownership", "template", "testing", "logger",  "performance", "类",
        "内存", "指针",      "项目",     "设计",    "调试",    "所有权",      "模板",
        "测试", "日志",      "性能",     "资源",    "取舍"};
    if (request.candidate_answer.size() >= 20 &&
        containsKeyword(request.candidate_answer, kTechnicalKeywords)) {
        score += 10;
    }

    // 分数上限固定为 100，避免后续 UI 进度条、报告百分比和追问阈值出现越界处理。
    score = std::min(score, 100);

    std::string feedback;
    if (score >= 85) {
        feedback = "回答扎实，包含具体细节。";
    } else if (score >= 65) {
        feedback = "回答不错，但建议补充一个具体例子。";
    } else if (score >= 40) {
        feedback = "回答有基本思路，但还需要更多细节。";
    } else {
        feedback = "回答太短，建议补充更多细节。";
    }

    return {score, feedback};
}

} // namespace services
} // namespace interview
