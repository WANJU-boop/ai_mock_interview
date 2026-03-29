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

# =========================
# COMMON 模块转成库
# =========================
add_library(common_lib  src/common/logger.cpp)

target_include_directories(common_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(common_lib
    PUBLIC
        spdlog::spdlog
)


# =========================
# service 模块转成库
# =========================
add_library(services_lib  src/services/services.cpp)

target_include_directories(services_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(services_lib
    PUBLIC
)

# =========================
# 主程序
# =========================
add_executable(${PROJECT_NAME}
    src/main.cpp
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        common_lib
        services_lib
)

# =========================
# 3. 测试
# =========================
enable_testing()
add_subdirectory(test)