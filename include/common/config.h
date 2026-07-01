#pragma once

#include <string>

namespace interview {
namespace common {

struct InterviewConfig {
    std::string candidate_name;
    std::string target_role;
    // 可选简历路径；为空时面试仍按岗位生成通用题目，不触发 PDF 解析边界。
    std::string resume_path;
    int question_count = 0;
};

struct LlmConfig {
    std::string provider; // mock 或者 http
    std::string model;
    // 真实 HTTP 客户端默认按 OpenAI 兼容接口拼接 /chat/completions。
    std::string base_url;
    // 只保存环境变量名，不在配置文件里放真实 API key。
    std::string api_key_env;
    // 超时统一用毫秒表示，后续真实网络实现和手动集成都复用这一个字段。
    int timeout_ms = 30000;
};

struct AppConfig {
    InterviewConfig interview;
    LlmConfig llm;
};

// 启动时优先沿用当前工作目录的默认配置；如果从项目外直接运行，再回退到可执行文件附近查找。
std::string findDefaultConfigPath(const std::string& executable_path);

AppConfig loadConfigFromFile(const std::string& file_path);

} // namespace common
} // namespace interview
