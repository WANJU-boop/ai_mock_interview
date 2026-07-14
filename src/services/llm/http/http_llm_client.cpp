#include "services/llm/http/http_llm_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace interview {
namespace services {

namespace {

// OpenAI 兼容接口通常把聊天补全能力暴露在 /chat/completions。
// 配置里允许写 base URL 或完整 endpoint，这里统一归一化，避免调用方关心尾斜杠细节。
std::string buildChatCompletionsUrl(const std::string& base_url) {
    // 如果用户已经配置到完整 endpoint，就直接复用，避免重复拼出
    // /chat/completions/chat/completions。
    if (base_url.size() >= std::string("/chat/completions").size() &&
        base_url.compare(base_url.size() - std::string("/chat/completions").size(),
                         std::string("/chat/completions").size(), "/chat/completions") == 0) {
        return base_url;
    }

    // base URL 带尾斜杠是常见写法，先去掉再拼接，保证最终 URL 只有一个分隔斜杠。
    if (!base_url.empty() && base_url.back() == '/') {
        return base_url.substr(0, base_url.size() - 1) + "/chat/completions";
    }

    return base_url + "/chat/completions";
}

// 当前只需要检查 https 前缀，单独抽出来让配置校验和客户端校验表达同一条规则。
bool startsWith(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

// 用户明确要求可在 config.local.json 直写密钥；该文件必须保持 Git ignored。
// 为了不破坏部署环境，直写值为空时仍支持从环境变量读取。
std::string requireApiKey(const common::LlmConfig& config) {
    if (!config.api_key.empty()) {
        return config.api_key;
    }

    const char* api_key = std::getenv(config.api_key_env.c_str());
    if (api_key == nullptr || std::string(api_key).empty()) {
        throw std::runtime_error("HTTP LLM API key 对应的环境变量未设置：" + config.api_key_env);
    }

    return api_key;
}

// LLM 返回的内容可能是顶层 JSON，也可能是 choices.message.content 里的 JSON 字符串。
// 统一在这里解析并带上 context，调用方能知道是哪个阶段的响应坏了。
nlohmann::json parseJsonOrThrow(const std::string& text, const std::string& context) {
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("无法解析 " + context + "：" + std::string(error.what()));
    }
}

// 结构化响应缺字段时直接失败，而不是返回空数组。
// 这样上层能区分“模型返回格式错误”和“确实没有题目”。
const nlohmann::json& requireArrayField(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object.at(key).is_array()) {
        throw std::runtime_error("HTTP LLM 响应缺少数组字段：" + key);
    }

    return object.at(key);
}

// 反馈文本和 message.content 都必须是非空字符串，否则报告和 CLI 会展示没有意义的空内容。
std::string requireNonEmptyStringField(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw std::runtime_error("HTTP LLM 响应缺少字符串字段：" + key);
    }

    std::string value = object.at(key).get<std::string>();
    const auto first_non_space =
        std::find_if_not(value.begin(), value.end(),
                         [](unsigned char character) { return std::isspace(character) != 0; });
    const auto last_non_space =
        std::find_if_not(value.rbegin(), value.rend(),
                         [](unsigned char character) { return std::isspace(character) != 0; });
    if (first_non_space == value.end()) {
        throw std::runtime_error("HTTP LLM 响应字段不能为空：" + key);
    }
    // 去掉首尾空白，让 CLI、报告和追问阈值看到稳定的字段；中间空白保持模型原意。
    return std::string(first_non_space, last_non_space.base());
}

// 支持两种响应形状：
// 1. 测试 fake 或简化服务直接返回 {"questions": [...]} / {"score": ...}
// 2. OpenAI 兼容 chat completions 返回 choices[0].message.content，content 里再放 JSON 字符串。
// 这样 HttpLlmClient 的业务解析逻辑可以同时覆盖离线测试和真实服务响应。
nlohmann::json extractStructuredPayload(const nlohmann::json& root) {
    if (root.contains("questions") || root.contains("score")) {
        return root;
    }

    // 真实 chat completions 响应至少要有一个 choice，且第一个 choice 必须是对象。
    const nlohmann::json& choices = requireArrayField(root, "choices");
    if (choices.empty() || !choices.front().is_object()) {
        throw std::runtime_error("HTTP LLM 响应的 choices 数组必须包含对象");
    }

    // 当前 MVP 只读取第一条 choice；如果未来要支持多候选答案，可以在这里扩展策略。
    const nlohmann::json& first_choice = choices.front();
    if (!first_choice.contains("message") || !first_choice.at("message").is_object()) {
        throw std::runtime_error("HTTP LLM 响应的 choice 缺少 message 对象");
    }

    // response_format 要求模型返回 JSON，但在 chat completions 中它仍然包在 content 字符串里。
    const std::string content = requireNonEmptyStringField(first_choice.at("message"), "content");
    return parseJsonOrThrow(content, "结构化 LLM content");
}

// 把 HTTP 响应体转换成领域层需要的题目列表。
// 所有格式校验都在这里完成，避免 CLI 或 InterviewManager 处理半合法数据。
std::vector<std::string> parseQuestions(const std::string& response_body, int expected_count) {
    const nlohmann::json payload =
        extractStructuredPayload(parseJsonOrThrow(response_body, "题目生成响应"));
    const nlohmann::json& questions_json = requireArrayField(payload, "questions");

    std::vector<std::string> questions;
    questions.reserve(questions_json.size());
    for (const nlohmann::json& item : questions_json) {
        if (!item.is_string()) {
            throw std::runtime_error("HTTP LLM 响应的 questions 数组只能包含字符串");
        }

        const nlohmann::json single_question = {{"question", item.get<std::string>()}};
        const std::string question = requireNonEmptyStringField(single_question, "question");
        if (question.empty()) {
            // 空题目会让交互层打印空白问题，属于模型响应格式错误，必须尽早拒绝。
            throw std::runtime_error("HTTP LLM 响应的 questions 数组不能包含空字符串");
        }
        questions.push_back(question);
    }

    if (static_cast<int>(questions.size()) != expected_count) {
        // 题目数量是面试状态机的固定计划。少题或多题都会让 UI
        // 进度和报告题数产生歧义，不能静默接受。
        throw std::runtime_error("HTTP LLM 返回的题目数量与请求数量不一致");
    }

    return questions;
}

// 把 HTTP 响应体转换成稳定的评分结果。
// 分数范围在服务层锁定为 0..100，后续 UI 进度条和追问阈值就不需要重复防御。
LlmScoreResult parseScoreResult(const std::string& response_body) {
    const nlohmann::json payload =
        extractStructuredPayload(parseJsonOrThrow(response_body, "回答评分响应"));
    if (!payload.contains("score") || !payload.at("score").is_number_integer()) {
        throw std::runtime_error("HTTP LLM 响应缺少整数字段：score");
    }

    const int score = payload.at("score").get<int>();
    if (score < 0 || score > 100) {
        throw std::runtime_error("HTTP LLM 响应的 score 必须在 0 到 100 之间");
    }

    const std::string feedback = requireNonEmptyStringField(payload, "feedback");
    return {score, feedback};
}

// 题目生成请求从项目领域模型转换成 OpenAI 兼容 chat completions 请求体。
// 这里故意要求 JSON object 输出，减少后续解析自由文本的不确定性。
nlohmann::json buildQuestionRequestBody(const common::LlmConfig& config,
                                        const QuestionGenerationRequest& request) {
    std::string prompt = "请为候选人 '" + request.candidate_name + "' 生成 " +
                         std::to_string(request.question_count) + " 道面向 '" +
                         request.target_role + "' 岗位的简洁中文 C++ 面试题。";
    if (!request.resume_context.empty()) {
        const std::size_t max_context_chars =
            static_cast<std::size_t>(config.max_prompt_context_chars);
        const std::string bounded_context = request.resume_context.substr(0, max_context_chars);
        // 简历上下文只进入私有 prompt，不在日志中输出；要求模型不要把原文复述进题目。
        prompt += "\n简历上下文（只用于定制题目，不要原文复述）：\n" + bounded_context;
        if (bounded_context.size() < request.resume_context.size()) {
            // 截断标记明确告诉模型上下文不完整，但不会把被截断的敏感内容重新放入请求。
            prompt += "\n[简历上下文已按长度限制截断]";
        }
    }
    prompt += "\n返回 JSON，格式为包含字符串数组 questions 的对象。";

    // 先把输出约束成稳定 JSON，后续 UI、CLI 和测试都不用解析自由文本。
    return {{"model", config.model},
            {"response_format", {{"type", "json_object"}}},
            {"messages",
             {{{"role", "system"},
               {"content", "你负责生成简洁的 C++ 模拟面试题。只返回 JSON，题目必须使用中文。"}},
              {{"role", "user"}, {"content", prompt}}}}};
}

// 评分请求同样转换成结构化 JSON 输出。
// 真实回答会进入请求体，但当前实现不会把请求体写进日志，避免泄露候选人回答。
nlohmann::json buildScoreRequestBody(const common::LlmConfig& config,
                                     const AnswerScoringRequest& request) {
    return {{"model", config.model},
            {"response_format", {{"type", "json_object"}}},
            {"messages",
             {{{"role", "system"},
               {"content", "你负责给 C++ 面试回答评分。只返回 JSON，feedback 必须使用中文。"}},
              {{"role", "user"},
               {"content",
                "问题：" + request.question + "\n回答：" + request.candidate_answer +
                    "\n请返回 JSON，包含整数 score (0-100) 和简短中文 feedback 字符串。"}}}}};
}

// HttpLlmClient 可能被测试直接构造，也可能由工厂创建。
// 因此这里再次校验配置，不依赖上游一定已经调用 loadConfigFromFile。
void validateHttpConfig(const common::LlmConfig& config) {
    if (config.provider != "http") {
        throw std::runtime_error("HttpLlmClient 要求 llm.provider 必须是 'http'");
    }
    if (config.model.empty()) {
        throw std::runtime_error("HttpLlmClient 要求 llm.model 不能为空");
    }
    if (config.base_url.empty()) {
        throw std::runtime_error("HttpLlmClient 要求 llm.base_url 不能为空");
    }
    if (!startsWith(config.base_url, "https://")) {
        throw std::runtime_error("HttpLlmClient 要求 llm.base_url 以 https:// 开头");
    }
    if (config.api_key.empty() && config.api_key_env.empty()) {
        throw std::runtime_error("HttpLlmClient 要求 llm.api_key 或 api_key_env 至少配置一个");
    }
    if (config.timeout_ms <= 0) {
        throw std::runtime_error("HttpLlmClient 要求 llm.timeout_ms 必须是正数");
    }
    if (config.max_prompt_context_chars <= 0) {
        throw std::runtime_error("HttpLlmClient 要求 llm.max_prompt_context_chars 必须是正数");
    }
}

// 统一发送 JSON 请求的最小公共流程：
// 1. 确认传输层已经注入
// 2. 从环境变量读取 API key
// 3. 拼 URL/header/body/timeout
// 4. 通过 IHttpTransport 发送
// 5. 把非 2xx HTTP 状态码转成清晰异常
HttpResponse sendJsonRequest(const common::LlmConfig& config,
                             const std::shared_ptr<IHttpTransport>& transport,
                             const std::string& request_body) {
    if (transport == nullptr) {
        // transport 是可测试边界，没有它就无法判断请求会发到哪里，因此构造或发送阶段都要拒绝。
        throw std::runtime_error("HttpLlmClient 尚未配置 HTTP transport，请先注入 transport");
    }

    const std::string api_key = requireApiKey(config);
    HttpRequest request;
    request.url = buildChatCompletionsUrl(config.base_url);
    request.timeout_ms = config.timeout_ms;
    request.body = request_body;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"Authorization", "Bearer " + api_key});

    // 这里依赖抽象接口而不是 Beast 具体类型，单元测试可以注入 FakeHttpTransport 离线断言请求内容。
    const HttpResponse response = transport->postJson(request);
    if (response.status_code < 200 || response.status_code >= 300) {
        throw std::runtime_error("HTTP LLM 请求失败，状态码：" +
                                 std::to_string(response.status_code));
    }

    return response;
}

} // namespace

HttpLlmClient::HttpLlmClient(const common::LlmConfig& config,
                             std::shared_ptr<IHttpTransport> transport)
    : config_(config), transport_(std::move(transport)) {
    // 构造函数保证对象一旦创建成功就是可用状态，避免把半初始化 client 传给主流程。
    validateHttpConfig(config_);
    if (transport_ == nullptr) {
        throw std::runtime_error("HttpLlmClient 要求 HTTP transport 不能为空");
    }
}

std::vector<std::string>
HttpLlmClient::generateQuestions(const QuestionGenerationRequest& request) {
    // 公共接口仍然返回领域层的题目列表；HTTP 请求体和响应包裹格式都被封装在本类内部。
    if (request.question_count <= 0) {
        throw std::runtime_error("HTTP LLM 题目请求数量必须是正数");
    }
    const HttpResponse response =
        sendJsonRequest(config_, transport_, buildQuestionRequestBody(config_, request).dump());
    return parseQuestions(response.body, request.question_count);
}

LlmScoreResult HttpLlmClient::scoreAnswer(const AnswerScoringRequest& request) {
    // 评分路径和题目生成路径共用同一套传输边界，只在请求体和响应解析函数上不同。
    const HttpResponse response =
        sendJsonRequest(config_, transport_, buildScoreRequestBody(config_, request).dump());
    return parseScoreResult(response.body);
}

} // namespace services
} // namespace interview
