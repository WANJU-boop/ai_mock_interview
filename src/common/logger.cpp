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
    try {
        // 控制台和文件两个输出目标统一挂到同一个 logger，业务层只依赖这一处入口。
        std::vector<spdlog::sink_ptr> sinks;

        // 控制台日志面向当前调试过程，debug 模式下显示更详细信息。
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(debug_mode ? spdlog::level::debug : spdlog::level::info);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        sinks.push_back(console_sink);

        // 文件日志保留 trace 级别信息，方便之后复盘问题。
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_file, true);
        file_sink->set_level(spdlog::level::trace);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%s:%#] %v");
        sinks.push_back(file_sink);

        // 多个 sink 组合成一个 logger，避免不同模块各自创建日志实例。
        logger_ = std::make_shared<spdlog::logger>("interview", sinks.begin(), sinks.end());
        logger_->set_level(debug_mode ? spdlog::level::debug : spdlog::level::info);
        logger_->flush_on(spdlog::level::warn);

        // 设置为 spdlog 默认 logger，后续扩展第三方封装时也能复用同一份配置。
        spdlog::set_default_logger(logger_);

        LOG_INFO("Logger initialized - Debug mode: {}", debug_mode);
    } catch (const spdlog::spdlog_ex& ex) {
        // 日志初始化失败时，至少把错误打到标准错误，避免静默失败。
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
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
