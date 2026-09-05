# src/CMakeLists.txt — 通用可复用代码库（common 静态库）
# 封装：配置解析、进程工具、变体解析。其他模块依赖它即可复用。

include(FetchContent)

# 第三方库：仓库里只提交压缩包（third_party/json-3.11.3.tar.gz），configure 时自动
# 解压到构建目录后用本地源码（离线、定版本）；压缩包缺失时才回退到网络拉取。
if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/json-3.11.3.tar.gz)
    cplus_vendor_extract(${CMAKE_SOURCE_DIR}/third_party/json-3.11.3.tar.gz
                         json-3.11.3 JSON_SRC_DIR)
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${JSON_SRC_DIR}" CACHE PATH "" FORCE)
endif()

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