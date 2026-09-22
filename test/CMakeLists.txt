# 测试目标统一链接 gtest、common_lib 和 session_lib。
find_package(GTest CONFIG REQUIRED)

add_executable(Project_test
    app/test_cli_interview_app.cpp
    app/test_realtime_demo_app.cpp
    common/test_config.cpp
    common/test_logger.cpp
    common/test_realtime_protocol.cpp
    services/test_audio_device.cpp
    services/test_pcm_sample_queue.cpp
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
    session/test_realtime_audio_bridge.cpp
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

# Qt UI 测试使用独立 QApplication，避免和 gtest_main 争用 main() 与事件循环。
add_executable(Qt_ui_test
    ui/test_qt_interview_ui.cpp
)

set_target_properties(Qt_ui_test PROPERTIES AUTOMOC ON)
# 与 Qt 主程序保持相同的 macOS linker 兼容边界，不改变现有纯 C++ 测试目标。
set_target_properties(Qt_ui_test PROPERTIES QT_NO_DISABLE_WARN_DUPLICATE_LIBRARIES ON)

target_link_libraries(Qt_ui_test
    PRIVATE
        Qt6::Test
        Qt6::Widgets
        session_lib
        ui_lib
)

add_test(NAME QtUiTest COMMAND Qt_ui_test)
# 静态 Qt 默认只导入系统平台插件；仅设置环境变量不能把 offscreen 链接进测试程序。
# 测试显式选择无显示器插件，仍运行真实控件与 worker；桌面应用继续使用系统默认插件。
qt_import_plugins(Qt_ui_test INCLUDE_BY_TYPE platforms Qt6::QOffscreenIntegrationPlugin)
set_tests_properties(QtUiTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
