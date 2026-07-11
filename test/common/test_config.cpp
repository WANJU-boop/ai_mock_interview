// clang-format off
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "common/config.h"
// clang-format on

namespace {

// 测试里把 JSON 内容写到临时文件，模拟真实配置文件加载路径。
std::string writeConfigFile(const std::filesystem::path& file_path, const std::string& content) {
    std::filesystem::create_directories(file_path.parent_path());
    std::ofstream output(file_path);
    output << content;
    return file_path.string();
}

// 路径查找测试会临时切换进程工作目录；RAII 保证断言失败或异常时也能恢复原目录，
// 避免一个测试的环境变化污染后续测试。
class ScopedCurrentPath {
  public:
    explicit ScopedCurrentPath(const std::filesystem::path& path)
        : original_path_(std::filesystem::current_path()) {
        std::filesystem::current_path(path);
    }

    ~ScopedCurrentPath() {
        std::filesystem::current_path(original_path_);
    }

  private:
    std::filesystem::path original_path_;
};

} // namespace

// 验证默认配置路径会优先沿用当前工作目录，保持已有命令行使用习惯不变。
TEST(ConfigTest, FindsDefaultConfigPathInCurrentWorkingDirectory) {
    const std::filesystem::path test_directory =
        std::filesystem::temp_directory_path() / "config_path_current_directory_test";
    std::filesystem::create_directories(test_directory);
    const ScopedCurrentPath scoped_current_path(test_directory);

    writeConfigFile(
        test_directory / "config.example.json",
        R"({"interview":{"candidate_name":"Demo","target_role":"C++","question_count":1},"llm":{"provider":"mock","model":"mock"}})");

    EXPECT_EQ(interview::common::findDefaultConfigPath("/tmp/fake/build/AI_mock_interview"),
              "config.example.json");
}

// 验证从项目外直接运行可执行文件时，会自动回退到可执行文件上级目录查找默认配置。
TEST(ConfigTest, FindsDefaultConfigPathNearExecutableDirectory) {
    const std::filesystem::path root_directory =
        std::filesystem::temp_directory_path() / "config_path_executable_directory_test";
    const std::filesystem::path build_directory = root_directory / "build";
    std::filesystem::create_directories(build_directory);
    writeConfigFile(
        root_directory / "config.example.json",
        R"({"interview":{"candidate_name":"Demo","target_role":"C++","question_count":1},"llm":{"provider":"mock","model":"mock"}})");

    {
        const ScopedCurrentPath scoped_current_path(std::filesystem::temp_directory_path());
        const std::string resolved_path = interview::common::findDefaultConfigPath(
            (build_directory / "AI_mock_interview").string());

        EXPECT_EQ(resolved_path,
                  (root_directory / "config.example.json").lexically_normal().string());
    }
}

// 验证完全找不到默认配置时，仍保留旧的文件名，方便错误信息继续直观指向缺失文件。
TEST(ConfigTest, ReturnsDefaultConfigFileNameWhenNoFallbackPathExists) {
    const ScopedCurrentPath scoped_current_path(std::filesystem::temp_directory_path());

    EXPECT_EQ(interview::common::findDefaultConfigPath("/tmp/nonexistent/build/AI_mock_interview"),
              "config.example.json");
}

// 验证最小合法配置能完整加载，并为省略的可选字段填入安全默认值。
TEST(ConfigTest, LoadsValidConfig) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "valid_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.interview.candidate_name, "Demo Candidate");
    EXPECT_EQ(config.interview.target_role, "C++ Intern");
    EXPECT_TRUE(config.interview.resume_path.empty());
    EXPECT_EQ(config.interview.question_count, 3);
    EXPECT_EQ(config.llm.provider, "mock");
    EXPECT_EQ(config.llm.model, "mock-interviewer");
    EXPECT_TRUE(config.llm.base_url.empty());
    EXPECT_TRUE(config.llm.api_key_env.empty());
    EXPECT_EQ(config.llm.timeout_ms, 30000);
    EXPECT_EQ(config.llm.max_prompt_context_chars, 8000);
    EXPECT_TRUE(config.report.save_json);
    EXPECT_EQ(config.report.output_directory, "reports");
    EXPECT_EQ(config.realtime.provider, "mock");
    EXPECT_EQ(config.realtime.dialog.input_mod, "text");
    EXPECT_EQ(config.realtime.connection.timeout_ms, 30000);
    EXPECT_EQ(config.realtime.tts.sample_rate_hz, 24000);
    EXPECT_EQ(config.realtime.audio.capture_sample_rate_hz, 16000);
    EXPECT_EQ(config.realtime.audio.capture_channels, 1);
    EXPECT_EQ(config.realtime.audio.frames_per_buffer, 320);
}

// 验证可选简历路径可以从配置进入 InterviewConfig，后续启动阶段再决定是否解析。
TEST(ConfigTest, LoadsOptionalResumePath) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "resume_path_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "resume_path": "/tmp/demo_resume.pdf",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.interview.resume_path, "/tmp/demo_resume.pdf");
}

// 验证真实 HTTP provider 需要的字段可以被正常加载，后续 factory 和客户端可直接复用。
TEST(ConfigTest, LoadsValidHttpConfig) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "valid_http_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "base_url": "https://api.openai.com/v1",
                "api_key_env": "OPENAI_API_KEY",
                "timeout_ms": 45000,
                "max_prompt_context_chars": 12000
            },
            "report": {
                "save_json": false,
                "output_directory": "custom_reports"
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.llm.provider, "http");
    EXPECT_EQ(config.llm.model, "gpt-4o-mini");
    EXPECT_EQ(config.llm.base_url, "https://api.openai.com/v1");
    EXPECT_EQ(config.llm.api_key_env, "OPENAI_API_KEY");
    EXPECT_EQ(config.llm.timeout_ms, 45000);
    EXPECT_EQ(config.llm.max_prompt_context_chars, 12000);
    EXPECT_FALSE(config.report.save_json);
    EXPECT_EQ(config.report.output_directory, "custom_reports");
}

// 验证 LLM prompt 上下文长度必须为正数，避免超长简历保护逻辑退化成 0 或负数的歧义行为。
TEST(ConfigTest, ThrowsWhenLlmPromptContextLimitIsNotPositive) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_prompt_context_limit_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "base_url": "https://api.openai.com/v1",
                "api_key_env": "OPENAI_API_KEY",
                "max_prompt_context_chars": 0
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 realtime mock 配置可以显式写入，后续 demo 入口能通过统一配置选择离线事件脚本。
TEST(ConfigTest, LoadsValidMockRealtimeConfig) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "valid_mock_realtime_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "mock"
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.realtime.provider, "mock");
}

// 验证火山 realtime 的连接参数可以从配置进入 RealtimeConfig，但真实密钥仍只保存环境变量名。
TEST(ConfigTest, LoadsValidVolcRealtimeConfig) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "valid_volc_realtime_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "connection": {
                    "endpoint": "wss://example.com/realtime",
                    "app_id_env": "TEST_VOLC_APP_ID",
                    "access_key_env": "TEST_VOLC_ACCESS_KEY",
                    "resource_id": "volc.speech.dialog",
                    "app_key": "public-app-key",
                    "timeout_ms": 45000
                },
                "dialog": {
                    "model": "1.2.1.1",
                    "input_mod": "text",
                    "strict_audit": false,
                    "enable_volc_websearch": true
                },
                "tts": {
                    "speaker": "demo-speaker",
                    "audio_format": "pcm_s16le",
                    "sample_rate_hz": 16000,
                    "channels": 2
                },
                "audio": {
                    "capture_sample_rate_hz": 48000,
                    "capture_channels": 2,
                    "frames_per_buffer": 960
                }
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.realtime.provider, "volc");
    EXPECT_EQ(config.realtime.connection.endpoint, "wss://example.com/realtime");
    EXPECT_EQ(config.realtime.connection.app_id_env, "TEST_VOLC_APP_ID");
    EXPECT_EQ(config.realtime.connection.access_key_env, "TEST_VOLC_ACCESS_KEY");
    EXPECT_EQ(config.realtime.connection.timeout_ms, 45000);
    EXPECT_FALSE(config.realtime.dialog.strict_audit);
    EXPECT_TRUE(config.realtime.dialog.enable_volc_websearch);
    EXPECT_EQ(config.realtime.tts.speaker, "demo-speaker");
    EXPECT_EQ(config.realtime.tts.sample_rate_hz, 16000);
    EXPECT_EQ(config.realtime.tts.channels, 2);
    EXPECT_EQ(config.realtime.audio.capture_sample_rate_hz, 48000);
    EXPECT_EQ(config.realtime.audio.capture_channels, 2);
    EXPECT_EQ(config.realtime.audio.frames_per_buffer, 960);
}

// 验证本地采集格式必须为正数。虽然本阶段尚未打开真实音频模式，错误配置也应在启动时被拒绝。
TEST(ConfigTest, ThrowsWhenRealtimeAudioFramesPerBufferIsNotPositive) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_audio_buffer_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "mock",
                "audio": {
                    "frames_per_buffer": 0
                }
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证真实 realtime provider 必须使用 WSS，避免把鉴权信息放到明文 WebSocket 里。
TEST(ConfigTest, ThrowsWhenVolcRealtimeEndpointIsNotSecure) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_volc_realtime_endpoint_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "connection": {
                    "endpoint": "ws://example.com/realtime"
                }
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 audio 模式在 PortAudio/PCM 边界完成后可以加载，入口层才能据此选择真实音频主链路。
TEST(ConfigTest, LoadsVolcRealtimeAudioMode) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_volc_audio_mode_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "dialog": {
                    "input_mod": "audio"
                }
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.realtime.dialog.input_mod, "audio");
    EXPECT_EQ(config.realtime.audio.capture_sample_rate_hz, 16000);
}

// 验证 audio 模式不接受未知 TTS 格式。当前播放器只实现 pcm_s16le，静默接收其它格式会播出错误音频。
TEST(ConfigTest, ThrowsWhenAudioModeUsesUnsupportedTtsFormat) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_audio_tts_format_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "dialog": {
                    "input_mod": "audio"
                },
                "tts": {
                    "audio_format": "mp3"
                }
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证安全相关开关必须是 JSON 布尔值，字符串 "false" 不能被隐式当作开启或关闭。
TEST(ConfigTest, ThrowsWhenRealtimeDialogFlagHasWrongType) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_volc_dialog_flag_type_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "dialog": {
                    "strict_audit": "false"
                }
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 TTS 采样率必须为正数；0 无法描述可播放的 PCM 输出格式。
TEST(ConfigTest, ThrowsWhenRealtimeTtsSampleRateIsNotPositive) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_volc_tts_sample_rate_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "tts": {
                    "sample_rate_hz": 0
                }
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证旧版平铺字段不会被静默忽略；显式迁移错误能防止自定义 endpoint 意外退回默认地址。
TEST(ConfigTest, ThrowsForLegacyFlatRealtimeConfig) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "legacy_flat_realtime_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            },
            "realtime": {
                "provider": "volc",
                "endpoint": "wss://legacy.example.com/realtime"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 interview/llm 这类必需 section 缺失时立即失败，不能返回部分 AppConfig。
TEST(ConfigTest, ThrowsForMissingConfigSection) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "missing_section_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 JSON 根节点必须是对象；合法 JSON 数组仍不符合本项目配置结构。
TEST(ConfigTest, ThrowsForNonObjectRoot) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "non_object_root_config_test.json",
                        R"([
            {
                "interview": {
                    "candidate_name": "Demo Candidate",
                    "target_role": "C++ Intern",
                    "question_count": 3
                },
                "llm": {
                    "provider": "mock",
                    "model": "mock-interviewer"
                }
            }
        ])");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 section 必须保持对象形状，否则字段读取无法提供明确语义。
TEST(ConfigTest, ThrowsForNonObjectConfigSection) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "non_object_section_config_test.json",
                                                    R"({
            "interview": "not-an-object",
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 target_role 等必填字段不能省略，避免错误配置被空值掩盖。
TEST(ConfigTest, ThrowsForMissingRequiredField) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "missing_field_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证字段存在但 JSON 类型错误时也会失败，不能依赖隐式字符串/整数转换。
TEST(ConfigTest, ThrowsForWrongFieldType) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "wrong_type_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": "three"
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证 resume_path 虽然是可选字段，但一旦提供就必须是字符串，避免 PDF 边界拿到歧义输入。
TEST(ConfigTest, ThrowsWhenResumePathHasWrongType) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "wrong_resume_path_type_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "resume_path": 123,
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证必填字符串不能用空值绕过“字段存在”检查，否则欢迎语和 LLM 上下文会失去语义。
TEST(ConfigTest, ThrowsForEmptyRequiredString) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "empty_string_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证题目数必须为正数；0 题无法形成可推进、可完成的面试流程。
TEST(ConfigTest, ThrowsForInvalidQuestionCount) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "invalid_question_count_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 0
            },
            "llm": {
                "provider": "mock",
                "model": "mock-interviewer"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证切到真实 provider 时，缺少 base_url 会在配置层尽早失败，而不是运行到服务层才报错。
TEST(ConfigTest, ThrowsWhenHttpProviderMissesBaseUrl) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "http_missing_base_url_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "api_key_env": "OPENAI_API_KEY"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证真实 provider 仍然要求显式声明密钥环境变量，避免把 401 混进启动错误里。
TEST(ConfigTest, ThrowsWhenHttpProviderMissesApiKeyEnv) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "http_missing_api_key_env_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "base_url": "https://api.openai.com/v1"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证配置层会拒绝明文 HTTP，避免未来真实请求绕过 TLS 校验预期。
TEST(ConfigTest, ThrowsWhenHttpProviderUsesNonHttpsBaseUrl) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "http_non_https_config_test.json",
                        R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "base_url": "http://api.openai.com/v1",
                "api_key_env": "OPENAI_API_KEY"
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证超时字段必须保持正整数，避免后续真实网络调用在配置层出现歧义。
TEST(ConfigTest, ThrowsWhenTimeoutMsIsNotPositive) {
    const std::string config_path = writeConfigFile(std::filesystem::temp_directory_path() /
                                                        "http_invalid_timeout_config_test.json",
                                                    R"({
            "interview": {
                "candidate_name": "Demo Candidate",
                "target_role": "C++ Intern",
                "question_count": 3
            },
            "llm": {
                "provider": "http",
                "model": "gpt-4o-mini",
                "base_url": "https://api.openai.com/v1",
                "api_key_env": "OPENAI_API_KEY",
                "timeout_ms": 0
            }
        })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}

// 验证不可读配置文件会抛出明确异常，入口层才能统一转换成启动失败。
TEST(ConfigTest, ThrowsForMissingConfigFile) {
    EXPECT_THROW(interview::common::loadConfigFromFile("missing_config_file_test.json"),
                 std::runtime_error);
}

// 验证语法损坏的 JSON 不会被当作默认配置继续运行，防止错误字段静默生效。
TEST(ConfigTest, ThrowsForMalformedJson) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "malformed_config_test.json",
                        R"({ "interview": { "candidate_name": "Demo" })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}
