#include "common/config.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace interview {
namespace common {

namespace {

const nlohmann::json& requireObject(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("Missing config section: " + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_object()) {
        throw std::runtime_error("Config section must be an object: " + key);
    }

    return value;
}

std::string requireString(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("Missing config field: " + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_string()) {
        throw std::runtime_error("Config field must be a string: " + key);
    }

    const std::string result = value.get<std::string>();
    if (result.empty()) {
        throw std::runtime_error("Config field must not be empty: " + key);
    }

    return result;
}

int requirePositiveInt(const nlohmann::json& parent, const std::string& key) {
    if (!parent.contains(key)) {
        throw std::runtime_error("Missing config field: " + key);
    }

    const nlohmann::json& value = parent.at(key);
    if (!value.is_number_integer()) {
        throw std::runtime_error("Config field must be an integer: " + key);
    }

    const int result = value.get<int>();
    if (result <= 0) {
        throw std::runtime_error("Config field must be positive: " + key);
    }

    return result;
}

}  // namespace

AppConfig loadConfigFromFile(const std::string& file_path) {
    std::ifstream input(file_path);
    if (!input.is_open()) {
        throw std::runtime_error("Failed to open config file: " + file_path);
    }

    nlohmann::json root;
    try {
        input >> root;
    } catch (const nlohmann::json::parse_error& error) {
        throw std::runtime_error("Failed to parse config JSON: " + std::string(error.what()));
    }

    if (!root.is_object()) {
        throw std::runtime_error("Config root must be an object");
    }

    const nlohmann::json& interview = requireObject(root, "interview");
    const nlohmann::json& llm = requireObject(root, "llm");

    AppConfig config;
    config.interview.candidate_name = requireString(interview, "candidate_name");
    config.interview.target_role = requireString(interview, "target_role");
    config.interview.question_count = requirePositiveInt(interview, "question_count");
    config.llm.provider = requireString(llm, "provider");
    config.llm.model = requireString(llm, "model");
    return config;
}

}  // namespace common
}  // namespace interview
