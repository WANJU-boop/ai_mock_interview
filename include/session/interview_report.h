#pragma once

// clang-format off
#include <nlohmann/json.hpp>

#include "session/dialog_session.h"
// clang-format on

namespace interview {
namespace session {

// 把会话里的结构化问答记录转换成独立 JSON，供测试、CLI、未来 UI 和文件导出复用。
nlohmann::json buildInterviewReportJson(const DialogSession& session);

} // namespace session
} // namespace interview
