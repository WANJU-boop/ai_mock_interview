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
std::string writeConfigFile(const std::string& file_name, const std::string& content) {
    const std::filesystem::path file_path = std::filesystem::temp_directory_path() / file_name;
    std::ofstream output(file_path);
    output << content;
    return file_path.string();
}

} // namespace

TEST(ConfigTest, LoadsValidConfig) {
    const std::string config_path = writeConfigFile("valid_config_test.json",
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
}

TEST(ConfigTest, ThrowsForMissingConfigSection) {
    const std::string config_path = writeConfigFile("missing_section_config_test.json",
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
    const std::string config_path = writeConfigFile("non_object_root_config_test.json",
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
    const std::string config_path = writeConfigFile("non_object_section_config_test.json",
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
    const std::string config_path = writeConfigFile("missing_field_config_test.json",
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
    const std::string config_path = writeConfigFile("wrong_type_config_test.json",
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
    const std::string config_path = writeConfigFile("empty_string_config_test.json",
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
    const std::string config_path = writeConfigFile("invalid_question_count_config_test.json",
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

TEST(ConfigTest, ThrowsForMissingConfigFile) {
    EXPECT_THROW(interview::common::loadConfigFromFile("missing_config_file_test.json"),
                 std::runtime_error);
}

TEST(ConfigTest, ThrowsForMalformedJson) {
    const std::string config_path = writeConfigFile(
        "malformed_config_test.json", R"({ "interview": { "candidate_name": "Demo" })");

    EXPECT_THROW(interview::common::loadConfigFromFile(config_path), std::runtime_error);
}
