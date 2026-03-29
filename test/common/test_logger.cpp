#include <gtest/gtest.h>
#include "common/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using interview::common::Logger;

//创建一个log 用GetLogger得到logger指针 判断logger指针是否是空指针 用这个std::filesystem::exists判断 到底有没有log_file这个文件
//判断初始化有没有问题
TEST(LoggerTest, InitCreatesLogger) {
    const std::string log_file = "logger_init_test.log";// 定义log名

    Logger::Init(log_file, true);                       // 初始化log
    auto logger = Logger::GetLogger();                  // 获取指针

    ASSERT_NE(logger, nullptr);                         // 判断是否为空指针
    EXPECT_TRUE(std::filesystem::exists(log_file));
}

//
TEST(LoggerTest, WriteMessageToFile) {
    const std::string log_file = "logger_write_test.log";// 定义log名
    const std::string message = "hello from gtest";      // 定义要输入的内容

    Logger::Init(log_file, true); 
    LOG_INFO("{}", message);                             //写入
    Logger::GetLogger()->flush();//刷新

    ASSERT_TRUE(std::filesystem::exists(log_file));

    std::ifstream ifs(log_file);                //打开日志
    ASSERT_TRUE(ifs.is_open());

    std::stringstream buffer;        //定义一个buffer 来获取日志里面的内容
    buffer << ifs.rdbuf();
    const std::string content = buffer.str();

    EXPECT_NE(content.find(message), std::string::npos); //判断是否log 和我们定义的是否相等
}
