# components/heartbeat/CMakeLists.txt — 心跳示例组件
# 链接共享库 core（演示动态库组件）；dladdr 需要链接 ${CMAKE_DL_LIBS}。

add_executable(heartbeat src/heartbeat_main.cpp)
target_link_libraries(heartbeat PRIVATE core ${CMAKE_DL_LIBS} Threads::Threads)