#include "common/config_parser.h"

#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>

namespace cplus {
namespace config {

bool load_config(const std::string& path, controller_config& out, std::string& err) {
  try {
    std::ifstream ifs(path);
    if (!ifs) {
      err = "cannot open config file: " + path;
      return false;
    }
    nlohmann::json j;
    ifs >> j;  // JSON 语法错误会抛异常，统一在下方捕获

    out.watch_interval_ms = j.value("watch_interval_ms", 1000);
    out.normal_bin_dir = j.at("normal_bin_dir").get<std::string>();
    out.tsan_bin_dir = j.at("tsan_bin_dir").get<std::string>();
    out.normal_lib_dir = j.at("normal_lib_dir").get<std::string>();
    out.tsan_lib_dir = j.at("tsan_lib_dir").get<std::string>();
    out.tsan_runner = j.value("tsan_runner", std::string());

    out.components.clear();
    if (j.contains("components")) {
      auto& arr = j["components"];
      if (!arr.is_array()) {
        err = "'components' must be an array";
        return false;
      }
      for (auto& e : arr) {
        component_spec s;
        s.name = e.at("name").get<std::string>();
        s.variant = e.at("variant").get<std::string>();
        s.key = e.value("key", s.name);
        if (s.variant != "normal" && s.variant != "tsan") {
          err = "component '" + s.name + "': invalid variant '" + s.variant +
                "' (expect normal|tsan)";
          return false;
        }
        if (e.contains("args")) {
          auto& args = e["args"];
          if (!args.is_array()) {
            err = "component '" + s.name + "': 'args' must be an array";
            return false;
          }
          for (auto& a : args) s.args.push_back(a.get<std::string>());
        }
        out.components.push_back(std::move(s));
      }
    }
  } catch (const std::exception& ex) {
    err = std::string("failed to parse config: ") + ex.what();
    return false;
  }

  // key 唯一性校验（运行槽位标识）
  for (size_t i = 0; i + 1 < out.components.size(); ++i) {
    for (size_t k = i + 1; k < out.components.size(); ++k) {
      if (out.components[i].key == out.components[k].key) {
        err = "duplicate component key: " + out.components[i].key;
        return false;
      }
    }
  }
  return true;
}

}  // namespace config
}  // namespace cplus