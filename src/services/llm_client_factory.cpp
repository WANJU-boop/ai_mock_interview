#include "services/llm_client_factory.h"

#include "services/http_llm_client.h"

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
        // 先把真实客户端类型、配置边界和主流程接线打通；底层传输放到下一步独立收口。
        return std::make_unique<HttpLlmClient>(config);
    }

    // 不支持的 provider 直接在服务层返回清晰错误，避免 main 拼装分支判断。
    throw std::runtime_error("Unsupported LLM provider: " + config.provider);
}

} // namespace services
} // namespace interview
