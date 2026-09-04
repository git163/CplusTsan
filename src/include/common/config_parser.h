#ifndef CPLUSTSAN_COMMON_CONFIG_PARSER_H_
#define CPLUSTSAN_COMMON_CONFIG_PARSER_H_

#include <string>
#include <vector>

namespace cplus {
namespace config {

// 组件规格：name 为二进制 basename；key 为运行槽位唯一标识（未指定时取 name）。
struct component_spec {
  std::string key;
  std::string name;
  std::string variant;  // "normal" | "tsan"
  std::vector<std::string> args;
};

// 总控配置：双变体各自对应独立的二进制/动态库目录（策略 A：库目录靠子进程 LD_LIBRARY_PATH 前置注入）。
struct controller_config {
  int watch_interval_ms = 1000;
  std::string normal_bin_dir;
  std::string tsan_bin_dir;
  std::string normal_lib_dir;
  std::string tsan_lib_dir;
  std::string tsan_runner;  // tsan 变体的启动器脚本路径（统一设置 TSAN_OPTIONS）
  std::vector<component_spec> components;
};

// 从 JSON 文件加载配置。解析失败返回 false 并把原因写入 err（不抛异常）。
// 校验：key 唯一、variant 合法（normal|tsan）。
bool load_config(const std::string& path, controller_config& out, std::string& err);

}  // namespace config
}  // namespace cplus

#endif  // CPLUSTSAN_COMMON_CONFIG_PARSER_H_