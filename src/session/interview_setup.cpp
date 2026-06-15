#include "session/interview_setup.h"

#include <utility>
#include <vector>

namespace interview {
namespace session {

PreparedInterview::PreparedInterview(const common::InterviewConfig& config,
                                     std::vector<std::string> questions,
                                     services::ILlmClient& llm_client)
    : candidate_name_(config.candidate_name), target_role_(config.target_role),
      manager_(std::make_unique<InterviewManager>(std::move(questions), llm_client)) {}

PreparedInterview::PreparedInterview(std::string candidate_name, std::string target_role,
                                     std::string error_message)
    : candidate_name_(std::move(candidate_name)), target_role_(std::move(target_role)),
      error_message_(std::move(error_message)) {}

bool PreparedInterview::isReady() const {
    return manager_ != nullptr;
}

const std::string& PreparedInterview::getErrorMessage() const {
    return error_message_;
}

const std::string& PreparedInterview::getCandidateName() const {
    return candidate_name_;
}

const std::string& PreparedInterview::getTargetRole() const {
    return target_role_;
}

InterviewManager& PreparedInterview::getManager() {
    return *manager_;
}

PreparedInterview prepareInterview(const common::InterviewConfig& config,
                                   services::ILlmClient& llm_client) {
    // 启动取题逻辑收口在 interview 层，避免 CLI 编排层直接依赖服务调用细节。
    std::vector<std::string> questions = llm_client.generateQuestions(
        {config.candidate_name, config.target_role, config.question_count});
    if (questions.empty()) {
        return {config.candidate_name, config.target_role,
                "Failed to start interview because no questions were generated."};
    }

    return {config, std::move(questions), llm_client};
}

} // namespace session
} // namespace interview
