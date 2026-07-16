cmake_minimum_required(VERSION 3.21)

# vcpkg 集成
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(AI_mock_interview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 依赖
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(openssl CONFIG REQUIRED)
find_package(boost_asio CONFIG REQUIRED)
find_package(boost_beast CONFIG REQUIRED)
find_package(boost_system CONFIG REQUIRED)
find_package(portaudio CONFIG REQUIRED)
find_package(podofo CONFIG REQUIRED)
find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Widgets Test)


# =========================
# COMMON 模块转成库
# =========================
add_library(common_lib
    src/common/config.cpp
    src/common/logger.cpp
    src/common/realtime_protocol.cpp
)

target_include_directories(common_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common_lib
    PUBLIC
        spdlog::spdlog
    PRIVATE
        nlohmann_json::nlohmann_json
)


# =========================
# service 模块转成库
# =========================
add_library(services_lib
    src/services/audio/portaudio/portaudio_audio_device.cpp
    src/services/audio/mock/fake_audio_device.cpp
    src/services/llm/http/beast_http_transport.cpp
    src/services/llm/http/http_llm_client.cpp
    src/services/llm/llm_client_factory.cpp
    src/services/llm/mock/mock_llm_client.cpp
    src/services/pdf/mock/mock_pdf_parser.cpp
    src/services/pdf/podofo/podofo_pdf_parser.cpp
    src/services/realtime/mock/mock_realtime_client.cpp
    src/services/realtime/realtime_client_factory.cpp
    src/services/realtime/volc/beast_volc_realtime_transport.cpp
    src/services/realtime/volc/volc_realtime_client_adapter.cpp
    src/services/realtime/volc/volc_realtime_client.cpp
    src/services/realtime/volc/volc_realtime_protocol.cpp
    src/services/realtime/volc/volc_realtime_runtime.cpp
    src/services/services.cpp
)

target_include_directories(services_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(services_lib
    PRIVATE
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        OpenSSL::SSL
        Boost::asio
        Boost::beast
        Boost::system
        portaudio_static
        podofo::podofo
)


# =========================
# SESSION 模块转成库
# =========================
add_library(session_lib
    src/session/dialog_cancellation.cpp
    src/session/dialog_orchestrator.cpp
    src/session/dialog_session.cpp
    src/session/interview_manager.cpp
    src/session/interview_report.cpp
    src/session/interview_setup.cpp
    src/session/realtime_audio_bridge.cpp
)

target_include_directories(session_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(session_lib
    PRIVATE
        common_lib
        services_lib
    PUBLIC
        nlohmann_json::nlohmann_json
)


# =========================
# APP 模块转成库
# =========================
add_library(app_lib
    src/app/cli_interview_app.cpp
    src/app/realtime_demo_app.cpp
)

target_include_directories(app_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(app_lib
    PUBLIC
        session_lib
    PRIVATE
        common_lib
        services_lib
)


# =========================
# QT UI 模块
# =========================
add_library(ui_lib
    include/ui/interview_worker.h
    include/ui/main_window.h
    src/ui/interview_worker.cpp
    src/ui/main_window.cpp
)

# 只给包含 Q_OBJECT 的目标开启 AUTOMOC，避免改变既有纯 C++ target 的构建行为。
set_target_properties(ui_lib PROPERTIES AUTOMOC ON)

target_include_directories(ui_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ui_lib
    PUBLIC
        Qt6::Core
        Qt6::Widgets
    PRIVATE
        app_lib
        common_lib
        services_lib
        session_lib
)


# =========================
# 主程序
# =========================
add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        app_lib
        common_lib
        services_lib
)

add_executable(${PROJECT_NAME}_realtime_demo
    src/main_realtime_demo.cpp
)

target_link_libraries(${PROJECT_NAME}_realtime_demo
    PRIVATE
        app_lib
        common_lib
        services_lib
)

add_executable(${PROJECT_NAME}_volc_text_demo
    src/main_volc_realtime_text_demo.cpp
)

add_executable(${PROJECT_NAME}_qt
    src/main_qt.cpp
)

# Qt 6.10 会在 Apple Clang 15+ 默认添加新 linker 才支持的去重告警参数。
# 当前 Xcode linker 不认识该选项，因此只在 Qt 最终可执行目标上关闭这条自动注入。
set_target_properties(${PROJECT_NAME}_qt PROPERTIES QT_NO_DISABLE_WARN_DUPLICATE_LIBRARIES ON)

if(APPLE)
    # macOS 使用标准 .app bundle，并显式声明麦克风用途；否则真实 PortAudio 模式无法获得权限。
    set_target_properties(${PROJECT_NAME}_qt PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "C++ AI 模拟面试"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.interviewlearning.aimockinterview"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos_info.plist.in"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    )
endif()

target_link_libraries(${PROJECT_NAME}_qt
    PRIVATE
        common_lib
        ui_lib
        Qt6::Widgets
)

target_link_libraries(${PROJECT_NAME}_volc_text_demo
    PRIVATE
        common_lib
        services_lib
)


# =========================
# 3. 测试
# =========================
enable_testing()
add_subdirectory(test)
