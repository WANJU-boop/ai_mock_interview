# 测试目标统一链接 gtest、common_lib 和 session_lib。
find_package(GTest CONFIG REQUIRED)

add_executable(Project_test
    app/test_cli_interview_app.cpp
    app/test_realtime_demo_app.cpp
    common/test_config.cpp
    common/test_logger.cpp
    common/test_realtime_protocol.cpp
    services/test_audio_device.cpp
    services/test_http_llm_client.cpp
    services/test_llm_client.cpp
    services/test_llm_client_factory.cpp
    services/test_pdf_parser.cpp
    services/test_realtime_client.cpp
    services/test_realtime_client_factory.cpp
    services/test_volc_realtime_client_adapter.cpp
    services/test_volc_realtime_client.cpp
    services/test_volc_realtime_protocol.cpp
    session/test_dialog_orchestrator.cpp
    session/test_dialog_session.cpp
    session/test_interview_manager.cpp
    session/test_interview_report.cpp
    session/test_interview_setup.cpp
)

target_link_libraries(Project_test
    PRIVATE
        GTest::gtest_main
        app_lib
        common_lib
        services_lib
        session_lib
)

include(GoogleTest)
gtest_discover_tests(Project_test)
