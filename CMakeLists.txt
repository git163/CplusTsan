cmake_minimum_required(VERSION 3.20)
project(CplusTsan LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 第一步 include TSan 集成模块：若 -DENABLE_TSAN=ON，会强制构建类型为 TSan 并把
# 插桩 flag 注入全局 CMAKE_CXX_FLAGS，保证包括 FetchContent 依赖在内的所有 TU 都插桩。
include(cmake/tsan.cmake)

# 默认构建类型 RelWithDebInfo；开启 TSan 时该值已由 tsan.cmake 置为 "TSan"，此处分支跳过。
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# 所有 build type 保留调试信息（含 TSan 的 -O1 -g）
add_compile_options(-g)

find_package(Threads REQUIRED)

# 各模块：src=common 库，controller=总控，components=示例组件，bench=基准
add_subdirectory(src)
add_subdirectory(controller)
add_subdirectory(components)
add_subdirectory(bench)

# 测试
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()