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
#include "services/realtime/realtime_client_factory.h"
#include "services/realtime/volc/volc_realtime_runtime.h"
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
    config.connection.app_id_env = "MISSING_TEST_VOLC_APP_ID";
    config.connection.access_key_env = "MISSING_TEST_VOLC_ACCESS_KEY";
    unsetenv(config.connection.app_id_env.c_str());
    unsetenv(config.connection.access_key_env.c_str());

    EXPECT_THROW(interview::services::createRealtimeClient(config, {}), std::runtime_error);
}

// 验证火山 provider 在环境变量齐全时只完成对象组装；connect() 才会真正联网。
TEST(RealtimeClientFactoryTest, CreatesVolcRealtimeClientWithoutConnecting) {
    interview::common::RealtimeConfig config;
    config.provider = "volc";
    config.connection.endpoint = "wss://example.com/realtime";
    config.connection.app_id_env = "TEST_VOLC_FACTORY_APP_ID";
    config.connection.access_key_env = "TEST_VOLC_FACTORY_ACCESS_KEY";
    const ScopedEnv app_id(config.connection.app_id_env, "fake-app-id");
    const ScopedEnv access_key(config.connection.access_key_env, "fake-access-key");

    std::unique_ptr<interview::services::IRealtimeClient> client =
        interview::services::createRealtimeClient(config, {});

    ASSERT_NE(client, nullptr);
}

// 验证统一解析函数会把连接、Dialog、TTS 和环境变量密钥完整映射到运行时对象。
// 这个边界重要，因为主 factory 和手动 demo 都依赖同一映射，不能再各自维护默认值。
TEST(RealtimeClientFactoryTest, ResolvesCompleteVolcRuntimeConfig) {
    interview::common::RealtimeConfig config;
    config.provider = "volc";
    config.connection.endpoint = "wss://example.com/realtime";
    config.connection.app_id_env = "TEST_VOLC_RUNTIME_APP_ID";
    config.connection.access_key_env = "TEST_VOLC_RUNTIME_ACCESS_KEY";
    config.connection.resource_id = "test-resource";
    config.connection.app_key = "test-public-app-key";
    config.connection.timeout_ms = 45000;
    config.dialog.model = "test-model";
    config.dialog.input_mod = "text";
    config.dialog.strict_audit = false;
    config.dialog.enable_volc_websearch = true;
    config.tts.speaker = "test-speaker";
    config.tts.audio_format = "pcm_s16le";
    config.tts.sample_rate_hz = 16000;
    config.tts.channels = 2;
    config.audio.capture_sample_rate_hz = 48000;
    config.audio.capture_channels = 2;
    config.audio.frames_per_buffer = 960;
    const ScopedEnv app_id(config.connection.app_id_env, "fake-app-id");
    const ScopedEnv access_key(config.connection.access_key_env, "fake-access-key");

    const interview::services::VolcRealtimeRuntimeConfig runtime =
        interview::services::resolveVolcRealtimeRuntimeConfig(config);

    EXPECT_EQ(runtime.endpoint, "wss://example.com/realtime");
    EXPECT_EQ(runtime.app_id, "fake-app-id");
    EXPECT_EQ(runtime.access_key, "fake-access-key");
    EXPECT_EQ(runtime.resource_id, "test-resource");
    EXPECT_EQ(runtime.app_key, "test-public-app-key");
    EXPECT_EQ(runtime.model, "test-model");
    EXPECT_EQ(runtime.input_mod, "text");
    EXPECT_FALSE(runtime.strict_audit);
    EXPECT_TRUE(runtime.enable_volc_websearch);
    EXPECT_EQ(runtime.speaker, "test-speaker");
    EXPECT_EQ(runtime.tts_audio_format, "pcm_s16le");
    EXPECT_EQ(runtime.tts_sample_rate_hz, 16000);
    EXPECT_EQ(runtime.tts_channels, 2);
    EXPECT_EQ(runtime.capture_sample_rate_hz, 48000);
    EXPECT_EQ(runtime.capture_channels, 2);
    EXPECT_EQ(runtime.frames_per_buffer, 960);
    EXPECT_EQ(runtime.timeout_ms, 45000);
    EXPECT_EQ(runtime.connect_id.rfind("connect-", 0), 0u);
    EXPECT_EQ(runtime.session_id.rfind("session-", 0), 0u);
}

// 验证 mock 配置不能误走火山运行时解析，避免手动 demo 忽略 provider 后意外联网。
TEST(RealtimeClientFactoryTest, RejectsRuntimeResolutionForNonVolcProvider) {
    interview::common::RealtimeConfig config;
    config.provider = "mock";

    EXPECT_THROW(interview::services::resolveVolcRealtimeRuntimeConfig(config), std::runtime_error);
}
