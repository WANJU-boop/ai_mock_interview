# 测试目标统一链接 gtest、common_lib 和 session_lib。
find_package(GTest CONFIG REQUIRED)

add_executable(Project_test
    common/test_config.cpp
    common/test_logger.cpp
    session/test_dialog_session.cpp
    session/test_interview_manager.cpp
    session/test_interview_report.cpp
)

target_link_libraries(Project_test
    PRIVATE
        GTest::gtest_main
        common_lib
        session_lib
)

include(GoogleTest)
gtest_discover_tests(Project_test)
