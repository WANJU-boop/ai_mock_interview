find_package(GTest CONFIG REQUIRED)

add_executable(Project_test
    common/test_logger.cpp
)

target_link_libraries(Project_test     
    PRIVATE
        GTest::gtest_main
        common_lib
)

include(GoogleTest)
gtest_discover_tests(Project_test)