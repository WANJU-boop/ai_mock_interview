#pragma once

#include <string>

namespace interview {
namespace common {

struct InterviewConfig {
    std::string candidate_name;
    std::string target_role;
    int question_count = 0;
};

struct LlmConfig {
    std::string provider;
    std::string model;
};

struct AppConfig {
    InterviewConfig interview;
    LlmConfig llm;
};

AppConfig loadConfigFromFile(const std::string& file_path);

}  // namespace common
}  // namespace interview
