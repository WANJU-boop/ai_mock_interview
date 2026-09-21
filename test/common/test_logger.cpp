// clang-format off
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "common/logger.h"
// clang-format on

// 验证初始化后能拿到全局 logger，并且对应日志文件已经创建出来。
TEST(LoggerTest, InitCreatesLogger) {
    const std::string log_file = "logger_init_test.log";

    interview::common::Logger::Init(log_file, true);
    auto logger = interview::common::Logger::GetLogger();

    ASSERT_NE(logger, nullptr);
    EXPECT_TRUE(std::filesystem::exists(log_file));
}

// 验证写入一条日志后，文件内容里确实能找到对应消息。
TEST(LoggerTest, WriteMessageToFile) {
    const std::string log_file = "logger_write_test.log";
    const std::string message = "hello from gtest";

    interview::common::Logger::Init(log_file, true);
    LOG_INFO("{}", message);
    interview::common::Logger::GetLogger()->flush();

    ASSERT_TRUE(std::filesystem::exists(log_file));

    std::ifstream ifs(log_file);
    ASSERT_TRUE(ifs.is_open());

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    const std::string content = buffer.str();

    EXPECT_NE(content.find(message), std::string::npos);
}

// 验证冷启动时文件 sink 创建失败仍获得控制台 logger；目录不能作为日志文件，
// 因此这个边界不依赖当前用户权限，也不需要修改工作目录或模拟真实桌面。
TEST(LoggerTest, FallsBackToConsoleWhenLogFileCannotBeOpened) {
    auto& logger = interview::common::Logger::GetLogger();
    logger.reset();
    interview::common::Logger::Init(".", false);

    // 直接检查同一个共享指针，避免再次 GetLogger 的懒初始化掩盖冷启动失败。
    ASSERT_NE(logger, nullptr);
    EXPECT_EQ(logger->sinks().size(), 1U);
    EXPECT_NO_THROW(LOG_INFO("日志目录不可写时，面试仍可继续。"));
}
