#include "services/llm/llm_client_factory.h"

#include "services/llm/http/beast_http_transport.h"
#include "services/llm/http/http_llm_client.h"
#include "services/llm/mock/mock_llm_client.h"

#include <memory>
#include <stdexcept>
#include <string>

namespace interview {
namespace services {

std::unique_ptr<ILlmClient> createLlmClient(const common::LlmConfig& config) {
    if (config.provider == "mock") {
        return std::make_unique<MockLlmClient>();
    }
    if (config.provider == "http") {
        // 真实 provider 在工厂里同时注入网络适配器，避免 main 拿到半成品客户端。
        return std::make_unique<HttpLlmClient>(config, std::make_shared<BeastHttpTransport>());
    }

    // 不支持的 provider 直接在服务层返回清晰错误，避免 main 拼装分支判断。
    throw std::runtime_error("不支持的 LLM provider：" + config.provider);
}

} // namespace services
} // namespace interview
