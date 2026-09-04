#include "common/variant_resolver.h"

namespace cplus {
namespace resolver {

std::optional<std::string> variant_bin_dir(const config::controller_config& c,
                                           const std::string& variant) {
  if (variant == "normal") return c.normal_bin_dir;
  if (variant == "tsan") return c.tsan_bin_dir;
  return std::nullopt;
}

std::optional<std::string> variant_lib_dir(const config::controller_config& c,
                                           const std::string& variant) {
  if (variant == "normal") return c.normal_lib_dir;
  if (variant == "tsan") return c.tsan_lib_dir;
  return std::nullopt;
}

std::string join_path(const std::string& dir, const std::string& name) {
  if (dir.empty()) return name;
  if (dir.back() == '/') return dir + name;
  return dir + "/" + name;
}

std::optional<std::string> component_bin(const config::controller_config& c,
                                         const config::component_spec& s) {
  auto dir = variant_bin_dir(c, s.variant);
  if (!dir) return std::nullopt;
  return join_path(*dir, s.name);
}

}  // namespace resolver
}  // namespace cplus