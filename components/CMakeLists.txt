# components/CMakeLists.txt — 示例组件
# 统一输出目录：可执行文件 → <build>/bin，动态库 → <build>/lib（策略 A 的变体解析约定）。

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

add_subdirectory(core)
add_subdirectory(heartbeat)