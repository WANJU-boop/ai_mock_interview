// clang-format off
#include "services/http_llm_client.h"
#include "services/llm_client_factory.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
// clang-format on

namespace {

interview::common::LlmConfig makeLlmConfig(const std::string& provider) {
    interview::common::LlmConfig config;
    config.provider = provider;
    config.model = provider == "http" ? "gpt-4o-mini" : "mock-interviewer";
    config.base_url = "https://api.openai.com/v1";
    config.api_key_env = "OPENAI_API_KEY";
    config.timeout_ms = 30000;
    return config;
}

} // namespace

// 验证 services 层可以根据 provider 创建 mock 客户端，入口层无需再了解具体类型细节。
TEST(LlmClientFactoryTest, CreatesMockClientForMockProvider) {
    const std::unique_ptr<interview::services::ILlmClient> client =
        interview::services::createLlmClient(makeLlmConfig("mock"));

    ASSERT_NE(client, nullptr);

    const std::vector<std::string> questions =
        client->generateQuestions({"Demo Candidate", "C++ Intern", 1});
    ASSERT_EQ(questions.size(), 1u);
    EXPECT_FALSE(questions.front().empty());
}

// 验证不支持的 provider 会在 services 层统一报错，避免入口层散落实现相关分支。
// 验证 services 层已经能把真实 HTTP provider 接到统一抽象，但不要求本轮真的联网。
TEST(LlmClientFactoryTest, CreatesHttpClientForHttpProvider) {
    const interview::common::LlmConfig config = makeLlmConfig("http");

    const std::unique_ptr<interview::services::ILlmClient> client =
        interview::services::createLlmClient(config);

    ASSERT_NE(client, nullptr);
    EXPECT_NE(dynamic_cast<interview::services::HttpLlmClient*>(client.get()), nullptr);
}

// 验证真实 provider 的非法配置会在工厂创建阶段直接失败，避免入口层带着坏客户端继续运行。
TEST(LlmClientFactoryTest, ThrowsWhenHttpProviderConfigIsInvalid) {
    interview::common::LlmConfig config = makeLlmConfig("http");
    config.base_url = "http://api.openai.com/v1";

    EXPECT_THROW(
        {
            const std::unique_ptr<interview::services::ILlmClient> client =
                interview::services::createLlmClient(config);
        },
        std::runtime_error);
}

// 验证不支持的 provider 会在 services 层统一报错，避免入口层散落实现相关分支。
TEST(LlmClientFactoryTest, ThrowsForUnsupportedProvider) {
    const interview::common::LlmConfig config = makeLlmConfig("unknown");

    EXPECT_THROW(
        {
            const std::unique_ptr<interview::services::ILlmClient> client =
                interview::services::createLlmClient(config);
        },
        std::runtime_error);
}
