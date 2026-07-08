// clang-format off
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "common/config.h"
#include "common/realtime_protocol.h"
#include "services/realtime_client_factory.h"
// clang-format on

namespace {

class ScopedEnv {
  public:
    ScopedEnv(std::string name, std::string value) : name_(std::move(name)) {
        // 测试会临时写入环境变量，让 factory 能完成火山配置组装。
        // 析构时恢复旧值，避免污染同一进程里的其他测试。
        const char* old_value = std::getenv(name_.c_str());
        if (old_value != nullptr) {
            old_value_ = std::string(old_value);
        }
        setenv(name_.c_str(), value.c_str(), 1);
    }

    ~ScopedEnv() {
        if (old_value_.has_value()) {
            setenv(name_.c_str(), old_value_->c_str(), 1);
        } else {
            unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_;
    std::optional<std::string> old_value_;
};

interview::common::RealtimeEvent makeConnectedEvent() {
    interview::common::RealtimeEvent event;
    event.type = interview::common::RealtimeEventType::kConnected;
    return event;
}

} // namespace

// 验证 mock provider 由 factory 创建后仍是纯离线 client，不需要网络或服务端账号。
TEST(RealtimeClientFactoryTest, CreatesMockRealtimeClientWithScriptedEvents) {
    interview::common::RealtimeConfig config;
    config.provider = "mock";

    std::unique_ptr<interview::services::IRealtimeClient> client =
        interview::services::createRealtimeClient(config, {makeConnectedEvent()});

    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(client->connect());
    ASSERT_TRUE(client->hasNextEvent());
    EXPECT_EQ(client->receiveNextEvent().type, interview::common::RealtimeEventType::kConnected);
}

// 验证不支持的 provider 会在 services 边界给出清晰错误，入口层不需要自己写分支兜底。
TEST(RealtimeClientFactoryTest, ThrowsForUnsupportedProvider) {
    interview::common::RealtimeConfig config;
    config.provider = "unknown";

    EXPECT_THROW(interview::services::createRealtimeClient(config, {}), std::runtime_error);
}

// 验证火山 provider 不允许缺少环境变量；配置文件只能保存变量名，不能保存真实密钥。
TEST(RealtimeClientFactoryTest, ThrowsWhenVolcEnvironmentVariableIsMissing) {
    interview::common::RealtimeConfig config;
    config.provider = "volc";
    config.app_id_env = "MISSING_TEST_VOLC_APP_ID";
    config.access_key_env = "MISSING_TEST_VOLC_ACCESS_KEY";
    unsetenv(config.app_id_env.c_str());
    unsetenv(config.access_key_env.c_str());

    EXPECT_THROW(interview::services::createRealtimeClient(config, {}), std::runtime_error);
}

// 验证火山 provider 在环境变量齐全时只完成对象组装；connect() 才会真正联网。
TEST(RealtimeClientFactoryTest, CreatesVolcRealtimeClientWithoutConnecting) {
    interview::common::RealtimeConfig config;
    config.provider = "volc";
    config.endpoint = "wss://example.com/realtime";
    config.app_id_env = "TEST_VOLC_FACTORY_APP_ID";
    config.access_key_env = "TEST_VOLC_FACTORY_ACCESS_KEY";
    const ScopedEnv app_id(config.app_id_env, "fake-app-id");
    const ScopedEnv access_key(config.access_key_env, "fake-access-key");

    std::unique_ptr<interview::services::IRealtimeClient> client =
        interview::services::createRealtimeClient(config, {});

    ASSERT_NE(client, nullptr);
}
