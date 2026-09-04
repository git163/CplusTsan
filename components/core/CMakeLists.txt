# components/core/CMakeLists.txt — 共享库 libcore
# 演示"组件依赖动态库"时的变体切换：libcore 与 heartbeat 都在同一构建变体下产出，
# 切变体时总控注入对应 lib 目录，heartbeat 加载到的库随之来自 build/lib 或 build-tsan/lib。

add_library(core SHARED src/core_api.cpp)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set_target_properties(core PROPERTIES VERSION 1.0.0 SOVERSION 1)