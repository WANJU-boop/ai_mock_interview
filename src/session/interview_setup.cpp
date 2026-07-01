#include "session/interview_setup.h"

#include <exception>
#include <string>
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

namespace {

std::string loadResumeContext(const common::InterviewConfig& config,
                              services::IPdfParser& pdf_parser) {
    if (config.resume_path.empty()) {
        return "";
    }

    // 简历解析属于外部服务边界，统一在启动阶段完成，避免 CLI 循环中混入文件解析细节。
    return pdf_parser.parseResume({config.resume_path}).text;
}

} // namespace

PreparedInterview prepareInterview(const common::InterviewConfig& config,
                                   services::ILlmClient& llm_client,
                                   services::IPdfParser& pdf_parser) {
    std::string resume_context;
    try {
        resume_context = loadResumeContext(config, pdf_parser);
    } catch (const std::exception& error) {
        return {config.candidate_name, config.target_role,
                "面试启动失败：简历解析失败：" + std::string(error.what())};
    }

    if (!config.resume_path.empty() && resume_context.empty()) {
        return {config.candidate_name, config.target_role,
                "面试启动失败：简历没有解析出可用文本。"};
    }

    // 启动取题逻辑收口在 interview 层，避免 CLI 编排层直接依赖服务调用细节。
    std::vector<std::string> questions = llm_client.generateQuestions(
        {config.candidate_name, config.target_role, config.question_count, resume_context});
    if (questions.empty()) {
        return {config.candidate_name, config.target_role, "面试启动失败：没有生成任何问题。"};
    }

    return {config, std::move(questions), llm_client};
}

} // namespace session
} // namespace interview
