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
    EXPECT_EQ(config.interview.question_count, 3);
    EXPECT_EQ(config.llm.provider, "mock");
    EXPECT_EQ(config.llm.model, "mock-interviewer");
    EXPECT_TRUE(config.llm.base_url.empty());
    EXPECT_TRUE(config.llm.api_key_env.empty());
    EXPECT_EQ(config.llm.timeout_ms, 30000);
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
                "timeout_ms": 45000
            }
        })");

    const interview::common::AppConfig config = interview::common::loadConfigFromFile(config_path);

    EXPECT_EQ(config.llm.provider, "http");
    EXPECT_EQ(config.llm.model, "gpt-4o-mini");
    EXPECT_EQ(config.llm.base_url, "https://api.openai.com/v1");
    EXPECT_EQ(config.llm.api_key_env, "OPENAI_API_KEY");
    EXPECT_EQ(config.llm.timeout_ms, 45000);
}

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

TEST(ConfigTest, ThrowsForNonObjectRoot) {
    // 配置根节点必须是对象；数组虽然是合法 JSON，但不符合本项目配置结构。
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

TEST(ConfigTest, ThrowsForNonObjectConfigSection) {
    // interview 和 llm 这类 section 必须是对象，不能用字符串等简单值替代。
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

TEST(ConfigTest, ThrowsForMissingConfigFile) {
    EXPECT_THROW(interview::common::loadConfigFromFile("missing_config_file_test.json"),
                 std::runtime_error);
}

TEST(ConfigTest, ThrowsForMalformedJson) {
    const std::string config_path =
        writeConfigFile(std::filesystem::temp_directory_path() / "malformed_config_test.json",
                        R"({ "interview": { "candidate_name": "Demo" })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}
