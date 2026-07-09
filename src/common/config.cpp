#include "common/config.h"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

// 配置模块把“不可信 JSON 文件”转换成类型稳定的 AppConfig：
// 1. require/read 辅助函数负责字段存在性、JSON 类型和取值范围。
// 2. provider 校验负责 HTTPS/WSS、密钥注入方式和当前功能边界。
// 3. 对外只返回完整可用配置，任何半合法输入都在启动阶段以异常失败。
namespace interview {
namespace common {

namespace {

// 默认文件名集中在实现层，避免入口程序各自硬编码并产生不同查找规则。
const char kDefaultConfigFileName[] = "config.example.json";

// 必填 section 必须存在且是 JSON object；返回 const 引用避免复制整段配置树。
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

// 必填字符串同时拒绝“缺字段、类型错误、空字符串”三类无效配置。
// 错误信息带字段名，方便新手直接定位 config.example.json 中的问题。
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

// 可选字符串缺失时用空值表达“功能未配置”；一旦出现，类型仍必须正确。
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

// 带默认值的可选字符串用于 realtime 配置迁移：
// 老配置可以省略新字段，但显式写空字符串仍视为配置错误，避免覆盖安全默认值。
std::string readOptionalStringWithDefault(const nlohmann::json& parent, const std::string& key,
                                          const std::string& default_value) {
    if (!parent.contains(key)) {
        return default_value;
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

// 题目数和超时都要求为正数；统一校验能避免不同模块对 0 或负数作出不同解释。
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

// 字段缺失时沿用代码默认值，字段存在时复用严格的正整数校验。
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

// mock provider 不需要网络字段；只有真实 HTTP provider 才强制检查加密传输和密钥来源。
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

// realtime 配置在创建 WebSocket 前先完成供应商和安全边界校验，
// 避免真实密钥读取后才发现 endpoint 或模式根本不受支持。
void validateRealtimeConfig(const AppConfig& config) {
    if (config.realtime.provider == "mock") {
        return;
    }

    if (config.realtime.provider != "volc") {
        throw std::runtime_error("不支持的 realtime provider：" + config.realtime.provider);
    }

    // 真实 realtime provider 的安全边界在配置层先检查：
    // 1. endpoint 必须是加密 WSS
    // 2. 配置文件只保存环境变量名
    // 3. 当前阶段只允许 text 模式，避免误以为音频链路已经完成。
    if (config.realtime.endpoint.empty()) {
        throw std::runtime_error("volc realtime 要求 realtime.endpoint 不能为空");
    }
    if (!startsWith(config.realtime.endpoint, "wss://")) {
        throw std::runtime_error("volc realtime 要求 realtime.endpoint 以 wss:// 开头");
    }
    if (config.realtime.app_id_env.empty()) {
        throw std::runtime_error("volc realtime 要求 realtime.app_id_env 不能为空");
    }
    if (config.realtime.access_key_env.empty()) {
        throw std::runtime_error("volc realtime 要求 realtime.access_key_env 不能为空");
    }
    if (config.realtime.input_mod != "text") {
        throw std::runtime_error("当前阶段只支持 realtime.input_mod=text，audio 模式留到音频模块");
    }
}

// 文件探测使用 error_code 而不是异常，让“候选路径不存在”保持为正常回退条件。
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
    // 文件打开失败和 JSON 解析失败分开报告：前者通常是路径问题，后者是配置内容问题。
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

    AppConfig config;

    // interview 和 llm 是启动主流程必需 section；realtime 则保持可选，兼容只运行 CLI 的旧配置。
    const nlohmann::json& interview = requireObject(root, "interview");
    const nlohmann::json& llm = requireObject(root, "llm");
    config.interview.candidate_name = requireString(interview, "candidate_name");
    config.interview.target_role = requireString(interview, "target_role");
    config.interview.resume_path = readOptionalString(interview, "resume_path");
    config.interview.question_count = requirePositiveInt(interview, "question_count");

    config.llm.provider = requireString(llm, "provider");
    config.llm.model = requireString(llm, "model");
    config.llm.base_url = readOptionalString(llm, "base_url");
    config.llm.api_key_env = readOptionalString(llm, "api_key_env");
    config.llm.timeout_ms = readPositiveIntWithDefault(llm, "timeout_ms", 30000);

    if (root.contains("realtime")) {
        const nlohmann::json& realtime = requireObject(root, "realtime");
        config.realtime.provider = requireString(realtime, "provider");
        config.realtime.endpoint =
            readOptionalStringWithDefault(realtime, "endpoint", config.realtime.endpoint);
        config.realtime.app_id_env =
            readOptionalStringWithDefault(realtime, "app_id_env", config.realtime.app_id_env);
        config.realtime.access_key_env = readOptionalStringWithDefault(
            realtime, "access_key_env", config.realtime.access_key_env);
        config.realtime.resource_id =
            readOptionalStringWithDefault(realtime, "resource_id", config.realtime.resource_id);
        config.realtime.app_key =
            readOptionalStringWithDefault(realtime, "app_key", config.realtime.app_key);
        config.realtime.model =
            readOptionalStringWithDefault(realtime, "model", config.realtime.model);
        config.realtime.input_mod =
            readOptionalStringWithDefault(realtime, "input_mod", config.realtime.input_mod);
        config.realtime.speaker =
            readOptionalStringWithDefault(realtime, "speaker", config.realtime.speaker);
        config.realtime.timeout_ms = readPositiveIntWithDefault(realtime, "timeout_ms", 30000);
    }

    // 所有字段装配完成后再做跨字段/provider 校验，保证校验函数看到的是完整配置。
    validateLlmConfig(config);
    validateRealtimeConfig(config);
    return config;
}

} // namespace common
} // namespace interview
