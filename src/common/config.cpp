#include "common/config.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace interview {
namespace common {

namespace {

const char kDefaultConfigFileName[] = "config.example.json";

const nlohmann::json& requireObject(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("配置缺少 section：" + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_object()) {
        throw std::runtime_error("配置 section 必须是对象：" + key);
    }

    return value;
}

std::string requireString(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("配置缺少字段：" + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_string()) {
        throw std::runtime_error("配置字段必须是字符串：" + key);
    }

    const std::string result = value.get<std::string>();
    if (result.empty()) {
        throw std::runtime_error("配置字段不能为空：" + key);
    }

    return result;
}

std::string readOptionalString(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        return "";
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_string()) {
        throw std::runtime_error("配置字段必须是字符串：" + key);
    }

    return value.get<std::string>();
}

int requirePositiveInt(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("配置缺少字段：" + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_number_integer()) {
        throw std::runtime_error("配置字段必须是整数：" + key);
    }

    const int result = value.get<int>();
    if (result <= 0) {
        throw std::runtime_error("配置字段必须是正数：" + key);
    }

    return result;
}

int readPositiveIntWithDefault(const nlohmann::json& parent, const std::string& key,
                               int default_value) {
    if (!parent.contains(key)) {
        return default_value;
    }

    return requirePositiveInt(parent, key);
}

bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

void validateLlmConfig(const AppConfig& config) {
    if (config.llm.provider != "http") {
        return;
    }

    // 真实 provider 的关键字段尽早在配置层报错，避免启动后才在服务层走到半截失败。
    if (config.llm.base_url.empty()) {
        throw std::runtime_error("HTTP provider 要求 llm.base_url 不能为空");
    }
    if (!startsWith(config.llm.base_url, "https://")) {
        throw std::runtime_error("HTTP provider 要求 llm.base_url 以 https:// 开头");
    }
    if (config.llm.api_key_env.empty()) {
        throw std::runtime_error("HTTP provider 要求 llm.api_key_env 不能为空");
    }
}

bool isExistingFile(const std::filesystem::path& path) {
    std::error_code error_code;
    return std::filesystem::exists(path, error_code) &&
           std::filesystem::is_regular_file(path, error_code);
}

} // namespace

std::string findDefaultConfigPath(const std::string& executable_path) {
    const std::filesystem::path current_directory_candidate = kDefaultConfigFileName;
    if (isExistingFile(current_directory_candidate)) {
        // 当前目录已经有默认配置时，继续保持相对路径行为，避免改变已有命令行习惯。
        return current_directory_candidate.string();
    }

    if (executable_path.empty()) {
        return kDefaultConfigFileName;
    }

    std::error_code error_code;
    std::filesystem::path executable_directory =
        std::filesystem::absolute(executable_path, error_code).parent_path();
    if (error_code) {
        return kDefaultConfigFileName;
    }

    // 从可执行文件所在目录向上回退几层，兼容 `build/AI_mock_interview` 这类从项目外直接启动的场景。
    for (int depth = 0; depth < 4 && !executable_directory.empty(); ++depth) {
        const std::filesystem::path candidate = executable_directory / kDefaultConfigFileName;
        if (isExistingFile(candidate)) {
            return candidate.lexically_normal().string();
        }
        executable_directory = executable_directory.parent_path();
    }

    return kDefaultConfigFileName;
}

AppConfig loadConfigFromFile(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("无法打开配置文件：" + file_path);
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("无法解析配置 JSON：" + std::string(error.what()));
    }

    if (!root.is_object()) {
        throw std::runtime_error("配置根节点必须是对象");
    }

    const nlohmann::json& interview = requireObject(root, "interview");
    const nlohmann::json& llm = requireObject(root, "llm");

    AppConfig config;
    config.interview.candidate_name = requireString(interview, "candidate_name");
    config.interview.target_role = requireString(interview, "target_role");
    config.interview.resume_path = readOptionalString(interview, "resume_path");
    config.interview.question_count = requirePositiveInt(interview, "question_count");
    config.llm.provider = requireString(llm, "provider");
    config.llm.model = requireString(llm, "model");
    config.llm.base_url = readOptionalString(llm, "base_url");
    config.llm.api_key_env = readOptionalString(llm, "api_key_env");
    config.llm.timeout_ms = readPositiveIntWithDefault(llm, "timeout_ms", 30000);
    validateLlmConfig(config);
    return config;
}

} // namespace common
} // namespace interview
