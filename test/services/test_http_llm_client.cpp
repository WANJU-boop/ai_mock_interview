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

interview::common::LlmConfig
makeHttpConfig(const std::string& base_url = "https://api.openai.com/v1") {
    interview::common::LlmConfig config;
    config.provider = "http";
    config.model = "gpt-4o-mini";
    config.base_url = base_url;
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
    EXPECT_NE(transport->last_request.body.find("生成 2 道面向"), std::string::npos);
    EXPECT_NE(transport->last_request.body.find("题目必须使用中文"), std::string::npos);
    EXPECT_NE(transport->last_request.body.find("C++ Intern"), std::string::npos);
}

// 验证简历摘要会进入 HTTP 题目生成 prompt，同时仍然通过同一个 fake transport 离线测试。
TEST(HttpLlmClientTest, IncludesResumeContextInQuestionRequestBody) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Resume based question"]})"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    const std::vector<std::string> questions = client.generateQuestions(
        {"Demo Candidate", "C++ Intern", 1, "候选人做过 C++ 日志系统，练习 RAII 和单元测试。"});

    ASSERT_EQ(questions.size(), 1u);
    EXPECT_NE(transport->last_request.body.find("简历上下文"), std::string::npos);
    EXPECT_NE(transport->last_request.body.find("不要原文复述"), std::string::npos);
    EXPECT_NE(transport->last_request.body.find("C++ 日志系统"), std::string::npos);
}

// 验证 base_url 带尾斜杠时仍只会追加一次 /chat/completions，避免真实请求地址重复拼接。
TEST(HttpLlmClientTest, AppendsChatCompletionsToBaseUrlWithTrailingSlash) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A"]})"};

    interview::services::HttpLlmClient client(makeHttpConfig("https://api.openai.com/v1/"),
                                              transport);

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 1});

    ASSERT_EQ(questions.size(), 1u);
    EXPECT_EQ(transport->last_request.url, "https://api.openai.com/v1/chat/completions");
}

// 验证 base_url 已经指向 chat/completions 时不会重复拼接，避免工厂配置不同写法导致请求错误。
TEST(HttpLlmClientTest, KeepsExistingChatCompletionsUrl) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A"]})"};

    interview::services::HttpLlmClient client(
        makeHttpConfig("https://api.openai.com/v1/chat/completions"), transport);

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 1});

    ASSERT_EQ(questions.size(), 1u);
    EXPECT_EQ(transport->last_request.url, "https://api.openai.com/v1/chat/completions");
}

// 验证评分接口也能复用同一传输抽象，并把结构化 JSON 直接还原成领域评分结果。
TEST(HttpLlmClientTest, ScoresAnswerFromStructuredJsonResponse) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({
        "score": 81,
        "feedback": "回答不错，但建议补充一个具体例子。"
    })"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    const interview::services::LlmScoreResult result = client.scoreAnswer(
        {"Explain RAII.", "I use RAII in a logger project to manage file ownership safely."});

    EXPECT_EQ(result.score, 81);
    EXPECT_EQ(result.feedback, "回答不错，但建议补充一个具体例子。");
    EXPECT_EQ(transport->call_count, 1);
    EXPECT_NE(transport->last_request.body.find("请返回 JSON，包含整数 score"), std::string::npos);
}

// 验证没有注入 transport 时会在构造阶段尽早失败，不再创建“可用性未知”的半成品客户端。
TEST(HttpLlmClientTest, ThrowsWhenTransportIsNotInjected) {
    EXPECT_THROW(interview::services::HttpLlmClient(makeHttpConfig(), nullptr), std::runtime_error);
}

// 验证没有注入环境变量时会返回清晰错误，避免把“401”这类远端错误混淆成配置问题。
TEST(HttpLlmClientTest, ThrowsWhenApiKeyEnvironmentVariableIsMissing) {
    unsetenv("TEST_OPENAI_API_KEY");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A"]})"};
    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.generateQuestions({"Demo Candidate", "C++ Intern", 1}), std::runtime_error);
}

// 验证结构化题目响应也允许直接返回 questions 根字段，兼容未来不包 choices 的 fake server。
TEST(HttpLlmClientTest, ParsesQuestionResponseWithoutChoicesEnvelope) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A","Question B"]})"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    const std::vector<std::string> questions =
        client.generateQuestions({"Demo Candidate", "C++ Intern", 2});

    ASSERT_EQ(questions.size(), 2u);
    EXPECT_EQ(questions[0], "Question A");
    EXPECT_EQ(questions[1], "Question B");
}

// 验证坏 JSON 会被明确识别成响应解析失败，而不是落到“无题”这类业务错误里。
TEST(HttpLlmClientTest, ThrowsWhenResponseBodyIsMalformedJson) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A"])"};

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

// 验证题目数组里出现空字符串时会明确失败，避免 CLI 启动后显示空白题目。
TEST(HttpLlmClientTest, ThrowsWhenQuestionArrayContainsEmptyString) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"questions":["Question A",""]})"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.generateQuestions({"Demo Candidate", "C++ Intern", 2}), std::runtime_error);
}

// 验证评分越界时会明确拒绝，避免上层 UI 或追问逻辑拿到非法分数。
TEST(HttpLlmClientTest, ThrowsWhenScoreResponseHasOutOfRangeScore) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"score":101,"feedback":"Too high."})"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.scoreAnswer({"Explain RAII.", "Sample answer."}), std::runtime_error);
}

// 验证评分反馈字段缺失时会明确失败，避免总结和报告阶段拿到空反馈。
TEST(HttpLlmClientTest, ThrowsWhenScoreResponseMissesFeedback) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {200, R"({"score":81})"};

    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.scoreAnswer({"Explain RAII.", "Sample answer."}), std::runtime_error);
}

// 验证 HTTP 状态码异常时客户端会原样向上抛出清晰失败，而不是继续解析错误页。
TEST(HttpLlmClientTest, ThrowsWhenHttpStatusCodeIsNotSuccessful) {
    ScopedEnvVar api_key("TEST_OPENAI_API_KEY", "fake-api-key");
    const std::shared_ptr<FakeHttpTransport> transport = std::make_shared<FakeHttpTransport>();
    transport->next_response = {429, R"({"error":"rate limited"})"};
    interview::services::HttpLlmClient client(makeHttpConfig(), transport);

    EXPECT_THROW(client.scoreAnswer({"Explain RAII.", "Sample answer."}), std::runtime_error);
}
