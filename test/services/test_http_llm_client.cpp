// clang-format off
#include "services/http_llm_client.h"

#include <cstdlib>

#include <gtest/gtest.h>
// clang-format on

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

class ScopedEnvVar {
  public:
    ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
        const char* current_value = std::getenv(name_.c_str());
        if (current_value != nullptr) {
            had_original_value_ = true;
            original_value_ = current_value;
        }

        setenv(name_.c_str(), value.c_str(), 1);
    }

    ~ScopedEnvVar() {
        if (had_original_value_) {
            setenv(name_.c_str(), original_value_.c_str(), 1);
            return;
        }

        unsetenv(name_.c_str());
    }

  private:
    bool had_original_value_ = false;
    std::string name_;
    std::string original_value_;
};

class FakeHttpTransport final : public interview::services::IHttpTransport {
  public:
    interview::services::HttpResponse
    postJson(const interview::services::HttpRequest& request) override {
        ++call_count;
        last_request = request;
        return next_response;
    }

    int call_count = 0;
    interview::services::HttpRequest last_request;
    interview::services::HttpResponse next_response;
};

interview::common::LlmConfig makeHttpConfig() {
    interview::common::LlmConfig config;
    config.provider = "http";
    config.model = "gpt-4o-mini";
    config.base_url = "https://api.openai.com/v1";
    config.api_key_env = "TEST_OPENAI_API_KEY";
    config.timeout_ms = 12000;
    return config;
}

std::string findHeaderValue(const std::vector<interview::services::HttpHeader>& headers,
                            const std::string& name) {
    for (const interview::services::HttpHeader& header : headers) {
        if (header.name == name) {
            return header.value;
        }
    }

    return "";
}

} // namespace

// 验证客户端会把领域请求转成 OpenAI 兼容请求，并能从 choices.message.content 中解析结构化题目。
TEST(HttpLlmClientTest, BuildsQuestionRequestAndParsesStructuredQuestionResponse) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({
        "choices": [
            {
                "message": {
                    "content": "{\"questions\":[\"Question A\",\"Question B\"]}"
                }
            }
        ]
    })"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 2});

    ASSERT_EQ(questions.size(), 2u);
    EXPECT_EQ(questions[0], "Question A");
    EXPECT_EQ(questions[1], "Question B");
    EXPECT_EQ(transport->call_count, 1);
    EXPECT_EQ(transport->last_request.url, "https://api.openai.com/v1/chat/completions");
    EXPECT_EQ(transport->last_request.timeout_ms, 12000);
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "Content-Type"), "application/json");
    EXPECT_EQ(findHeaderValue(transport->last_request.headers, "Authorization"),
              "Bearer fake-api-key");
    EXPECT_NE(transport->last_request.body.find("\"model\":\"gpt-4o-mini\""), std::string::npos);
    EXPECT_NE(transport->last_request.body.find("Generate 2 concise C++ interview questions"),
              std::string::npos);
    EXPECT_NE(transport->last_request.body.find("C++ Intern"), std::string::npos);
}

// 验证评分接口也能复用同一传输抽象，并把结构化 JSON 直接还原成领域评分结果。
TEST(HttpLlmClientTest, ScoresAnswerFromStructuredJsonResponse) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({
        "score": 81,
        "feedback": "Good answer, but add one concrete example."
    })"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    const interview::services::LlmScoreResult result = client.scoreAnswer(
        {"Explain RAII.", "I use RAII in a logger project to manage file ownership safely."});

    EXPECT_EQ(result.score, 81);
    EXPECT_EQ(result.feedback, "Good answer, but add one concrete example.");
    EXPECT_EQ(transport->call_count, 1);
    EXPECT_NE(transport->last_request.body.find("Return JSON with integer score"),
              std::string::npos);
}

// 验证没有注入环境变量时会返回清晰错误，避免把“401”这类远端错误混淆成配置问题。
TEST(HttpLlmClientTest, ThrowsWhenApiKeyEnvironmentVariableIsMissing) {
    unsetenv("TEST_OPENAI_API_KEY");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A"]})"};
    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.generateQuestions({"Demo Candidate", "C++ Intern", 1}), std::runtime_error);
}

// 验证结构化响应缺字段时会明确失败，避免上层误把坏响应当成“无题可问”。
TEST(HttpLlmClientTest, ThrowsWhenStructuredQuestionResponseMissesQuestionsArray) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({
        "choices": [
            {
                "message": {
                    "content": "{\"items\":[\"Question A\"]}"
                }
            }
        ]
    })"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.generateQuestions({"Demo Candidate", "C++ Intern", 1}), std::runtime_error);
}

// 验证 HTTP 状态码异常时客户端会原样向上抛出清晰失败，而不是继续解析错误页。
TEST(HttpLlmClientTest, ThrowsWhenHttpStatusCodeIsNotSuccessful) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {429, R"({"error":"rate limited"})"};
    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.scoreAnswer({"Explain RAII.", "Sample answer."}), std::runtime_error);
}
