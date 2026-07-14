#pragma once

// clang-format off
#include <nlohmann/json.hpp>

#include "session/dialog_session.h"
// clang-format on

#include <filesystem>
#include <string>

namespace interview {
namespace session {

// 把会话里的结构化问答记录转换成独立 JSON，供测试、CLI、未来 UI 和文件导出复用。
nlohmann::json buildInterviewReportJson(const DialogSession& session);

// 根据本地目录生成不含候选人姓名的 JSON 路径。调用方可把路径展示给用户，
// 但不应把报告全文写进日志，因为其中可能包含简历、回答和评分反馈。
std::filesystem::path createInterviewReportPath(const std::string& output_directory);

// 把当前会话的报告原子写入新文件。目标存在、目录不可创建或写入/重命名失败时抛出异常，
// 绝不静默覆盖一份既有面试记录。
void saveInterviewReportJson(const DialogSession& session,
                             const std::filesystem::path& output_path);

} // namespace session
} // namespace interview
