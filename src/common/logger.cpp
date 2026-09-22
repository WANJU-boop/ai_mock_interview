#include "common/logger.h"

// clang-format off
#include <iostream>
#include <memory>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
// clang-format on

namespace interview {
namespace common {

std::shared_ptr<spdlog::logger> Logger::logger_ = nullptr;

void Logger::Init(const std::string& log_file, bool debug_mode) {
    // 先准备不依赖文件权限的控制台输出；磁盘日志失败不能让业务代码解引用空 logger。
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台日志面向当前调试过程，debug 模式下显示更详细信息。
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(debug_mode ? spdlog::level::debug : spdlog::level::info);
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    sinks.push_back(console_sink);

    try {
        // 文件日志保留 trace 级别信息，方便之后复盘问题。
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        sinks.push_back(file_sink);
    } catch (const spdlog::spdlog_ex&) {
        // 桌面启动时工作目录可能不可写。保留控制台 sink，不回显路径或中断面试。
        std::cerr << "日志文件不可写，继续使用控制台日志。" << std::endl;
    }

    // 无论文件 sink 是否建立成功，对外始终提供可用的 logger。
    logger_ = std::make_shared<spdlog::logger>("interview", sinks.begin(), sinks.end());
    logger_->set_level(debug_mode ? spdlog::level::debug : spdlog::level::info);
    logger_->flush_on(spdlog::level::warn);

    // 设置为 spdlog 默认 logger，后续扩展第三方封装时也能复用同一份配置。
    spdlog::set_default_logger(logger_);

    LOG_INFO("日志系统已初始化，debug 模式：{}", debug_mode);
}

std::shared_ptr<spdlog::logger>& Logger::GetLogger() {
    if (!logger_) {
        // 懒初始化保证测试或小工具忘记调用 Init 时仍然可以安全记录日志。
        Init();
    }
    return logger_;
}

} // namespace common
} // namespace interview
