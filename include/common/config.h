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

struct RealtimeConfig {
    // mock 是默认 provider，保证普通构建和单元测试不需要网络、麦克风或服务端账号。
    std::string provider = "mock";
    // 火山 realtime WSS 地址；只有 provider=volc 时才会使用。
    std::string endpoint = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    // 只保存环境变量名，真实 App ID 和 Access Key 由本地 shell 注入，不能提交到仓库。
    std::string app_id_env = "VOLC_APP_ID";
    std::string access_key_env = "VOLC_ACCESS_KEY";
    // 供应商侧资源、模型和 speaker 都放在配置里，后续调试真实服务时不用改协议代码。
    std::string resource_id = "volc.speech.dialog";
    std::string app_key = "PlgvMymc7f3tQnJ6";
    std::string model = "1.2.1.1";
    // 当前项目还没有音频边界，先只允许 text 模式；audio 模式等 PortAudio 阶段再打开。
    std::string input_mod = "text";
    std::string speaker = "zh_female_vv_jupiter_bigtts";
    int timeout_ms = 30000;
};

struct AppConfig {
    InterviewConfig interview;
    LlmConfig llm;
    RealtimeConfig realtime;
};

// 启动时优先沿用当前工作目录的默认配置；如果从项目外直接运行，再回退到可执行文件附近查找。
std::string findDefaultConfigPath(const std::string& executable_path);

AppConfig loadConfigFromFile(const std::string& file_path);

} // namespace common
} // namespace interview
