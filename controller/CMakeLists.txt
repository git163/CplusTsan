# controller/CMakeLists.txt — 总控示例
# 单线程 poll 循环：调度组件、配置热重载切换 TSan/非 TSan 变体、自动重启。

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_executable(controller src/controller_main.cpp)
target_link_libraries(controller PRIVATE common)