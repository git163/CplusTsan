# bench/CMakeLists.txt — 性能对比基准（TSan vs 非 TSan）

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)

add_executable(bench_threads src/bench_threads.cpp)
target_link_libraries(bench_threads PRIVATE Threads::Threads)