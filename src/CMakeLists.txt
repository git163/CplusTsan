# src/CMakeLists.txt — 通用可复用代码库（common 静态库）
# 封装：配置解析、进程工具、变体解析。其他模块依赖它即可复用。

include(FetchContent)

# header-only JSON 库，封装进 config_parser，避免总控重复造轮子
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(common STATIC
    common/config_parser.cpp
    common/process_util.cpp
    common/variant_resolver.cpp)
target_include_directories(common PUBLIC include)
target_link_libraries(common PUBLIC nlohmann_json::nlohmann_json Threads::Threads)