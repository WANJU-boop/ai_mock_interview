#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "common/logger.h"

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
