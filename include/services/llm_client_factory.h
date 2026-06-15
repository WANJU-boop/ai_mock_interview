#pragma once

#include "common/config.h"
#include "services/llm_client.h"

#include <memory>

namespace interview {
namespace services {

// 由 services 层统一解析 provider 配置，避免入口层直接依赖具体 LLM 实现类型。
std::unique_ptr<ILlmClient> createLlmClient(const common::LlmConfig& config);

} // namespace services
} // namespace interview
