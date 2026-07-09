#pragma once

#include "common/config.h"
#include "services/llm/llm_client.h"
#include "services/pdf/pdf_parser.h"
#include "session/interview_manager.h"

#include <memory>
#include <string>
#include <vector>

namespace interview {
namespace session {

// 预构建好的面试上下文，把入口层需要的候选人信息和题目管理器绑定在一起。
// 这样 app 层只负责编排输入输出，不再直接决定如何向服务层取题。
class PreparedInterview {
  public:
    // 成功路径下持有候选人信息和可直接运行的题目管理器。
    PreparedInterview(const common::InterviewConfig& config, std::vector<std::string> questions,
                      services::ILlmClient& llm_client);

    // 失败路径只保留展示给入口层的错误信息，避免 app 层自己拼装启动失败原因。
    PreparedInterview(std::string candidate_name, std::string target_role,
                      std::string error_message);

    // 是否已经准备好可运行的面试上下文。
    bool isReady() const;

    // 返回启动失败原因；成功路径下为空字符串。
    const std::string& getErrorMessage() const;

    // 暴露候选人姓名，供 CLI 或未来 UI 展示欢迎语。
    const std::string& getCandidateName() const;

    // 暴露目标岗位，供 CLI 或未来 UI 展示当前面试上下文。
    const std::string& getTargetRole() const;

    // 返回题目管理器，供编排层驱动问答流程。
    InterviewManager& getManager();

  private:
    std::string candidate_name_;
    std::string target_role_;
    std::unique_ptr<InterviewManager> manager_;
    std::string error_message_;
};

// 由 interview 层统一负责“根据配置解析简历、向 LLM 取题并准备会话”的启动逻辑。
PreparedInterview prepareInterview(const common::InterviewConfig& config,
                                   services::ILlmClient& llm_client,
                                   services::IPdfParser& pdf_parser);

} // namespace session
} // namespace interview
