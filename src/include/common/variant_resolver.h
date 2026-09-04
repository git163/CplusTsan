#ifndef CPLUSTSAN_COMMON_VARIANT_RESOLVER_H_
#define CPLUSTSAN_COMMON_VARIANT_RESOLVER_H_

#include <optional>
#include <string>

#include "common/config_parser.h"

namespace cplus {
namespace resolver {

// 变体 → 二进制目录；非法变体返回 nullopt。
std::optional<std::string> variant_bin_dir(const config::controller_config& c,
                                           const std::string& variant);

// 变体 → 动态库目录（策略 A 的注入目标）。
std::optional<std::string> variant_lib_dir(const config::controller_config& c,
                                           const std::string& variant);

// 目录 + 文件名拼接为路径（处理末尾 '/'）。
std::string join_path(const std::string& dir, const std::string& name);

// 组件可执行文件绝对路径：<variant_bin_dir>/<name>。
std::optional<std::string> component_bin(const config::controller_config& c,
                                         const config::component_spec& s);

}  // namespace resolver
}  // namespace cplus

#endif  // CPLUSTSAN_COMMON_VARIANT_RESOLVER_H_