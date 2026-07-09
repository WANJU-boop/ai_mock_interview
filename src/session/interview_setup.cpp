#include "session/interview_setup.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

// 面试准备层把三个独立输入收口成可运行上下文：
// 配置 -> 可选 PDF 文本 -> LLM 题目 -> PreparedInterview/InterviewManager。
// 外部服务异常在这一层转换成入口可展示的失败对象，app 层无需认识 PDF 或 LLM 实现细节。
namespace interview {
namespace session {

PreparedInterview::PreparedInterview(const common::InterviewConfig& config,
                                     std::vector<std::string> questions,
                                     services::ILlmClient& llm_client)
    // 成功对象独占 manager；manager 再引用由入口层拥有、生命周期更长的 llm_client。
    : candidate_name_(config.candidate_name), target_role_(config.target_role),
      manager_(std::make_unique<InterviewManager>(std::move(questions), llm_client)) {}

PreparedInterview::PreparedInterview(std::string candidate_name, std::string target_role,
                                     std::string error_message)
    // 失败对象故意不创建 manager，isReady() 可以用唯一、稳定的条件判断是否可运行。
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
    // 调用方必须先检查 isReady()；成功路径保证 manager_ 非空，避免接口返回可空指针。
    return *manager_;
}

namespace {

std::string loadResumeContext(const common::InterviewConfig& config,
                              services::IPdfParser& pdf_parser) {
    if (config.resume_path.empty()) {
        // 没有简历是合法路径，不调用 parser，避免 mock/真实实现误读空文件名。
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
        // PDF 库的具体异常不穿透到 app 层；保留 what() 便于用户定位文件或格式问题。
        return {config.candidate_name, config.target_role,
                "面试启动失败：简历解析失败：" + std::string(error.what())};
    }

    if (!config.resume_path.empty() && resume_context.empty()) {
        // “提供了路径但没有文字”通常是扫描件或无文字层 PDF，不能假装简历定制已经成功。
        return {config.candidate_name, config.target_role,
                "面试启动失败：简历没有解析出可用文本。"};
    }

    // 启动取题逻辑收口在 interview 层，避免 CLI 编排层直接依赖服务调用细节。
    std::vector<std::string> questions = llm_client.generateQuestions(
        {config.candidate_name, config.target_role, config.question_count, resume_context});
    if (questions.empty()) {
        // 空题目无法形成可推进的 InterviewManager，统一返回失败对象而不是半初始化成功对象。
        return {config.candidate_name, config.target_role, "面试启动失败：没有生成任何问题。"};
    }

    // questions 按值移入 manager，避免复制题目列表；llm_client 的所有权仍由入口层保留。
    return {config, std::move(questions), llm_client};
}

} // namespace session
} // namespace interview
