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
find_package(podofo CONFIG REQUIRED)


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
    src/services/beast_http_transport.cpp
    src/services/beast_volc_realtime_transport.cpp
    src/services/http_llm_client.cpp
    src/services/llm_client.cpp
    src/services/llm_client_factory.cpp
    src/services/pdf_parser.cpp
    src/services/realtime_client.cpp
    src/services/realtime_client_factory.cpp
    src/services/services.cpp
    src/services/volc_realtime_client_adapter.cpp
    src/services/volc_realtime_client.cpp
    src/services/volc_realtime_protocol.cpp
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
        podofo::podofo
)


# =========================
# SESSION 模块转成库
# =========================
add_library(session_lib
    src/session/dialog_orchestrator.cpp
    src/session/dialog_session.cpp
    src/session/interview_manager.cpp
    src/session/interview_report.cpp
    src/session/interview_setup.cpp
)

target_include_directories(session_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(session_lib
    PRIVATE
        common_lib
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

target_link_libraries(${PROJECT_NAME}_volc_text_demo
    PRIVATE
        services_lib
)


# =========================
# 3. 测试
# =========================
enable_testing()
add_subdirectory(test)
