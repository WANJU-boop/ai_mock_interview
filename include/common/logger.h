#pragma once

// clang-format off
#include <memory>
#include <string>

#include <spdlog/spdlog.h>
// clang-format on

namespace interview {
namespace common {

class Logger {
  public:
    // 初始化进程级日志；文件不可写时仍保留控制台输出，避免 GUI 首次运行因日志失败而崩溃。
    static void Init(const std::string& log_file = "interview.log", bool debug_mode = false);

    // 如果调用方忘记显式初始化日志，这里会用默认配置懒加载一个 logger。
    static std::shared_ptr<spdlog::logger>& GetLogger();

  private:
    static std::shared_ptr<spdlog::logger> logger_;
};

// 日志宏让业务代码不用反复写 Logger::GetLogger()，保持调用处简短。
#define LOG_TRACE(...) ::interview::common::Logger::GetLogger()->trace(__VA_ARGS__)
#define LOG_DEBUG(...) ::interview::common::Logger::GetLogger()->debug(__VA_ARGS__)
#define LOG_INFO(...) ::interview::common::Logger::GetLogger()->info(__VA_ARGS__)
#define LOG_WARN(...) ::interview::common::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_WARNING(...) ::interview::common::Logger::GetLogger()->warn(__VA_ARGS__)
#define LOG_ERROR(...) ::interview::common::Logger::GetLogger()->error(__VA_ARGS__)
#define LOG_CRITICAL(...) ::interview::common::Logger::GetLogger()->critical(__VA_ARGS__)

} // namespace common
} // namespace interview
