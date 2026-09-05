# tests/CMakeLists.txt — 单元测试
# 用 FetchContent 拉 GoogleTest（**无需系统安装 gtest**）。随项目成长可拆成 tests/<module>/CMakeLists.txt。

include(FetchContent)

# 第三方库：仓库里只提交压缩包（third_party/googletest-1.14.0.tar.gz），configure 时
# 自动解压到构建目录后用本地源码（离线、定版本）；压缩包缺失时才回退到网络拉取。
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/googletest-1.14.0.tar.gz)
    cplus_vendor_extract(${CMAKE_SOURCE_DIR}/third_party/googletest-1.14.0.tar.gz
                         googletest-1.14.0 GTEST_SRC_DIR)
    set(FETCHCONTENT_SOURCE_DIR_GOOGLETEST "${GTEST_SRC_DIR}" CACHE PATH "" FORCE)
endif()

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Windows 下强制使用 shared CRT，避免链接错误
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# 自动发现 tests/ 下所有 Test*.cpp（含子目录）
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS "Test*.cpp")

add_executable(unit_tests ${TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE GTest::gtest GTest::gtest_main common)
target_include_directories(unit_tests PRIVATE ../src/include)
# 抑制文件路径由 CMake 传给测试，避免依赖运行目录
target_compile_definitions(unit_tests PRIVATE TSAN_SUPP_PATH="${CMAKE_SOURCE_DIR}/tsan/tsan.supp")

# 让每个 TEST() 自动注册为 CTest 用例
gtest_discover_tests(unit_tests)

# 集成场景/压力测试：真实拉起总控，触发 崩溃恢复/变体热切换/优雅退出 场景。
# 前置：需先执行 scripts/build.sh（build-tsan.sh 可选，缺失时脚本自动跳过 tsan 场景）。
if(UNIX)
    add_test(NAME stress.controller COMMAND ${CMAKE_SOURCE_DIR}/scripts/stress-controller.sh)
    set_tests_properties(stress.controller PROPERTIES LABELS "integration" TIMEOUT 90)
endif()