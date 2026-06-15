#include "services/http_llm_client.h"

#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace interview {
namespace services {

namespace {

std::string buildChatCompletionsUrl(const std::string& base_url) {
    if (base_url.size() >= std::string("/chat/completions").size() &&
        base_url.compare(base_url.size() - std::string("/chat/completions").size(),
                         std::string("/chat/completions").size(), "/chat/completions") == 0) {
        return base_url;
    }

    if (!base_url.empty() && base_url.back() == '/') {
        return base_url.substr(0, base_url.size() - 1) + "/chat/completions";
    }

    return base_url + "/chat/completions";
}

std::string requireApiKey(const common::LlmConfig& config) {
    const char* api_key = std::getenv(config.api_key_env.c_str());
    if (api_key == nullptr || std::string(api_key).empty()) {
        throw std::runtime_error("Environment variable is not set for HTTP LLM API key: " +
                                 config.api_key_env);
    }

    return api_key;
}

nlohmann::json parseJsonOrThrow(const std::string& text, const std::string& context) {
    try {
        return nlohmann::json::parse(text);
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("Failed to parse " + context + ": " + std::string(error.what()));
    }
}

const nlohmann::json& requireArrayField(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object.at(key).is_array()) {
        throw std::runtime_error("HTTP LLM response is missing array field: " + key);
    }

    return object.at(key);
}

std::string requireNonEmptyStringField(const nlohmann::json& object, const std::string& key) {
    if (!object.contains(key) || !object.at(key).is_string()) {
        throw std::runtime_error("HTTP LLM response is missing string field: " + key);
    }

    const std::string value = object.at(key).get<std::string>();
    if (value.empty()) {
        throw std::runtime_error("HTTP LLM response field must not be empty: " + key);
    }

    return value;
}

nlohmann::json extractStructuredPayload(const nlohmann::json& root) {
    if (root.contains("questions") || root.contains("score")) {
        return root;
    }

    const nlohmann::json& choices = requireArrayField(root, "choices");
    if (choices.empty() || !choices.front().is_object()) {
        throw std::runtime_error("HTTP LLM response choices array must contain an object");
    }

    const nlohmann::json& first_choice = choices.front();
    if (!first_choice.contains("message") || !first_choice.at("message").is_object()) {
        throw std::runtime_error("HTTP LLM response choice is missing message object");
    }

    const std::string content = requireNonEmptyStringField(first_choice.at("message"), "content");
    return parseJsonOrThrow(content, "structured LLM content");
}

std::vector<std::string> parseQuestions(const std::string& response_body) {
    const nlohmann::json payload =
        extractStructuredPayload(parseJsonOrThrow(response_body, "question generation response"));
    const nlohmann::json& questions_json = requireArrayField(payload, "questions");

    std::vector<std::string> questions;
    questions.reserve(questions_json.size());
    for (const nlohmann::json& item : questions_json) {
        if (!item.is_string()) {
            throw std::runtime_error("HTTP LLM response questions array must contain only strings");
        }

        const std::string question = item.get<std::string>();
        if (question.empty()) {
            throw std::runtime_error(
                "HTTP LLM response questions array must not contain empty strings");
        }
        questions.push_back(question);
    }

    return questions;
}

LlmScoreResult parseScoreResult(const std::string& response_body) {
    const nlohmann::json payload =
        extractStructuredPayload(parseJsonOrThrow(response_body, "answer scoring response"));
    if (!payload.contains("score") || !payload.at("score").is_number_integer()) {
        throw std::runtime_error("HTTP LLM response is missing integer field: score");
    }

    const int score = payload.at("score").get<int>();
    if (score < 0 || score > 100) {
        throw std::runtime_error("HTTP LLM response score must be within 0 and 100");
    }

    const std::string feedback = requireNonEmptyStringField(payload, "feedback");
    return {score, feedback};
}

nlohmann::json buildQuestionRequestBody(const common::LlmConfig& config,
                                        const QuestionGenerationRequest& request) {
    // 先把输出约束成稳定 JSON，后续 UI、CLI 和测试都不用解析自由文本。
    return {{"model", config.model},
            {"response_format", {{"type", "json_object"}}},
            {"messages",
             {{{"role", "system"},
               {"content", "You generate concise C++ mock interview questions. Return JSON only."}},
              {{"role", "user"},
               {"content", "Generate " + std::to_string(request.question_count) +
                               " concise C++ interview questions for the role '" +
                               request.target_role + "' for candidate '" + request.candidate_name +
                               "'. Return JSON with a questions array of strings."}}}}};
}

nlohmann::json buildScoreRequestBody(const common::LlmConfig& config,
                                     const AnswerScoringRequest& request) {
    return {
        {"model", config.model},
        {"response_format", {{"type", "json_object"}}},
        {"messages",
         {{{"role", "system"}, {"content", "You score C++ interview answers. Return JSON only."}},
          {{"role", "user"},
           {"content",
            "Question: " + request.question + "\nAnswer: " + request.candidate_answer +
                "\nReturn JSON with integer score (0-100) and short feedback string."}}}}};
}

void validateHttpConfig(const common::LlmConfig& config) {
    if (config.provider != "http") {
        throw std::runtime_error("HttpLlmClient requires llm.provider to be 'http'");
    }
    if (config.model.empty()) {
        throw std::runtime_error("HttpLlmClient requires non-empty llm.model");
    }
    if (config.base_url.empty()) {
        throw std::runtime_error("HttpLlmClient requires non-empty llm.base_url");
    }
    if (config.api_key_env.empty()) {
        throw std::runtime_error("HttpLlmClient requires non-empty llm.api_key_env");
    }
    if (config.timeout_ms <= 0) {
        throw std::runtime_error("HttpLlmClient requires positive llm.timeout_ms");
    }
}

HttpResponse sendJsonRequest(const common::LlmConfig& config,
                             const std::shared_ptr<IHttpTransport>& transport,
                             const std::string& request_body) {
    if (transport == nullptr) {
        throw std::runtime_error(
            "HTTP transport is not configured yet for HttpLlmClient; inject a transport first");
    }

    const std::string api_key = requireApiKey(config);
    HttpRequest request;
    request.url = buildChatCompletionsUrl(config.base_url);
    request.timeout_ms = config.timeout_ms;
    request.body = request_body;
    request.headers.push_back({"Content-Type", "application/json"});
    request.headers.push_back({"Authorization", "Bearer " + api_key});

    const HttpResponse response = transport->postJson(request);
    if (response.status_code < 200 || response.status_code >= 300) {
        throw std::runtime_error("HTTP LLM request failed with status code: " +
                                 std::to_string(response.status_code));
    }

    return response;
}

} // namespace

HttpLlmClient::HttpLlmClient(const common::LlmConfig& config,
                             std::shared_ptr<IHttpTransport> transport)
    : config_(config), transport_(std::move(transport)) {
    validateHttpConfig(config_);
}

std::vector<std::string>
HttpLlmClient::generateQuestions(const QuestionGenerationRequest& request) {
    const HttpResponse response =
        sendJsonRequest(config_, transport_, buildQuestionRequestBody(config_, request).dump());
    return parseQuestions(response.body);
}

LlmScoreResult HttpLlmClient::scoreAnswer(const AnswerScoringRequest& request) {
    const HttpResponse response =
        sendJsonRequest(config_, transport_, buildScoreRequestBody(config_, request).dump());
    return parseScoreResult(response.body);
}

} // namespace services
} // namespace interview
