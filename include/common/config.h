#pragma once

#include <string>

namespace interview {
namespace common {

// 一次面试启动所需的领域配置。这里不保存运行时对象，只描述“要面试谁、什么岗位、问几题”。
struct InterviewConfig {
    // 候选人姓名只用于交互展示和 LLM 上下文，不应写入日志。
    std::string candidate_name;
    // 目标岗位会传给题目生成服务，用于控制问题方向。
    std::string target_role;
    // 可选简历路径；为空时面试仍按岗位生成通用题目，不触发 PDF 解析边界。
    std::string resume_path;
    // 期望生成的题目数。加载配置时要求为正数，避免启动一个无法推进的空会话。
    int question_count = 0;
};

// LLM provider 配置。真实密钥只通过 api_key_env 指向环境变量，不进入配置对象。
struct LlmConfig {
    // provider 当前支持 mock 和 http；mock 保证默认学习流程可以完全离线运行。
    std::string provider;
    // 模型名原样传给 OpenAI 兼容接口，mock provider 不使用该字段。
    std::string model;
    // 真实 HTTP 客户端默认按 OpenAI 兼容接口拼接 /chat/completions。
    std::string base_url;
    // 只保存环境变量名，不在配置文件里放真实 API key。
    std::string api_key_env;
    // 超时统一用毫秒表示，后续真实网络实现和手动集成都复用这一个字段。
    int timeout_ms = 30000;
};

// Realtime 连接和鉴权来源。这里只保存环境变量名，不保存解析后的真实密钥。
struct RealtimeConnectionConfig {
    // 火山 realtime WSS 地址；真实 provider 必须使用加密的 wss://。
    std::string endpoint = "wss://openspeech.bytedance.com/api/v3/realtime/dialogue";
    // 只保存环境变量名，真实 App ID 和 Access Key 由本地 shell 注入，不能提交到仓库。
    std::string app_id_env = "VOLC_APP_ID";
    std::string access_key_env = "VOLC_ACCESS_KEY";
    // resource_id 和 app_key 是火山握手所需的能力标识，不是运行时生成的连接 ID。
    std::string resource_id = "volc.speech.dialog";
    std::string app_key = "PlgvMymc7f3tQnJ6";
    // 所有同步 realtime 网络操作共用毫秒超时，避免手动集成检查永久阻塞。
    int timeout_ms = 30000;
};

// 火山 Dialog 会话行为。它决定模型和文本/音频输入模式，不负责本地面试追问策略。
struct RealtimeDialogConfig {
    // 火山模型版本可由本地配置覆盖，切换模型不需要修改协议代码。
    std::string model = "1.2.1.1";
    // 当前项目还没有音频边界，先只允许 text 模式；audio 模式等 PortAudio 阶段再打开。
    std::string input_mod = "text";
    // 审核和联网搜索是供应商会话选项，集中配置后不再隐藏在 JSON payload 构造代码里。
    bool strict_audit = true;
    bool enable_volc_websearch = false;
};

// 火山 TTS 输出配置。文本模式也可能返回语音，因此先集中服务端输出格式。
struct RealtimeTtsConfig {
    // speaker 和 PCM 格式必须与后续播放器约定一致，避免音频阶段再追查隐藏常量。
    std::string speaker = "zh_female_vv_jupiter_bigtts";
    std::string audio_format = "pcm_s16le";
    int sample_rate_hz = 24000;
    int channels = 1;
};

// Realtime provider 配置只聚合可持久化的用户设置，不持有 WebSocket、密钥值或运行时 ID。
struct RealtimeConfig {
    // mock 是默认 provider，保证普通构建和单元测试不需要网络、麦克风或服务端账号。
    std::string provider = "mock";
    // connection、dialog、tts 分开保存，调用方可以只把对应子配置映射给外部服务。
    RealtimeConnectionConfig connection;
    RealtimeDialogConfig dialog;
    RealtimeTtsConfig tts;
};

// 应用级配置把三个模块的配置聚合起来，入口层加载一次后再分别注入对应模块。
struct AppConfig {
    // 三个子配置按模块边界保存，入口层只把对应部分传给各自 factory/setup。
    InterviewConfig interview;
    LlmConfig llm;
    RealtimeConfig realtime;
};

// 启动时优先沿用当前工作目录的默认配置；如果从项目外直接运行，再回退到可执行文件附近查找。
std::string findDefaultConfigPath(const std::string& executable_path);

// 从 JSON 文件加载并校验完整应用配置。
// 文件不可读、JSON 结构错误、必填字段缺失或真实 provider 不满足安全约束时会抛出异常。
AppConfig loadConfigFromFile(const std::string& file_path);

} // namespace common
} // namespace interview
