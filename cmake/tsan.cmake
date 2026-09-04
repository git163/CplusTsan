# tsan.cmake — 可复用的 ThreadSanitizer 集成模块
# 用法：其他 C++ 项目在顶层 CMakeLists.txt 中、任何 add_subdirectory 之前：
#     include(cmake/tsan.cmake)
# 然后以独立构建目录产出变体：
#     cmake -S . -B build-tsan -DENABLE_TSAN=ON
#     cmake --build build-tsan -j
# 说明：
#   - 整程序插桩：TSan 要求进程内所有代码（含静态库 / FetchContent 依赖）都用 TSan
#     编译，否则未插桩代码会制造"假竞态"。因此这里把 -fsanitize=thread 追加到全局
#     CMAKE_C/CXX_FLAGS 与链接 flags，而不是只用 target_compile_options。
#   - 编译期插桩不可运行时切换：TSan 变体必须产出于独立构建目录（build-tsan/），
#     运行时"切换"＝重启进程换用另一变体的二进制。
#   - Linux 为验证基准；macOS 的 TSan runtime 可靠性差（Apple clang / brew clang 均可能
#     出现误报或 crash），模块本身可移植，但结果请以 Linux 为准。
#   - 定制 build type "TSan"：-O1 -g（绕开 RelWithDebInfo 的 -O2 -DNDEBUG）。
#     -O1 保证竞态可复现、assert 生效；-DNDEBUG 缺失可捕获更多竞态。

option(ENABLE_TSAN "构建含 ThreadSanitizer 的变体" OFF)

if(ENABLE_TSAN)
    # 强制 build type，避免出现"已插桩但 -O3"的混合形态
    set(CMAKE_BUILD_TYPE TSan CACHE STRING "Build type" FORCE)

    # TSan build type 的编译/链接 flags（对 src/controller/components/bench 子目录生效）
    set(CMAKE_C_FLAGS_TSAN "-fsanitize=thread -fno-omit-frame-pointer -O1 -g")
    set(CMAKE_CXX_FLAGS_TSAN "-fsanitize=thread -fno-omit-frame-pointer -O1 -g")
    set(CMAKE_EXE_LINKER_FLAGS_TSAN "-fsanitize=thread")
    set(CMAKE_SHARED_LINKER_FLAGS_TSAN "-fsanitize=thread")

    # 整程序插桩：全局追加，保证 FetchContent、静态库等所有 TU 都被插桩
    string(APPEND CMAKE_C_FLAGS " -fsanitize=thread -fno-omit-frame-pointer -DTSAN_BUILD")
    string(APPEND CMAKE_CXX_FLAGS " -fsanitize=thread -fno-omit-frame-pointer -DTSAN_BUILD")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=thread")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fsanitize=thread")

    find_package(Threads REQUIRED)
    message(STATUS "TSan enabled: whole-program instrumentation (build type=TSan, -O1 -g)")
else()
    message(STATUS "TSan disabled: normal variant build")
endif()