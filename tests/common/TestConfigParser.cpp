// tests/common/TestConfigParser.cpp
// 覆盖：正确解析、字段默认值、坏 JSON 报错（不抛）、非法变体、key 重复。

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "common/config_parser.h"

namespace {

// 在系统临时目录写一个 JSON 文件，返回其路径；用 pid+时间戳避免并发冲突。
std::string write_temp_json(const std::string& content) {
  const auto path = std::filesystem::temp_directory_path() /
                    ("config_test_" + std::to_string(::getpid()) + "_" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ".json");
  std::ofstream ofs(path);
  ofs << content;
  return path.string();
}

}  // namespace

TEST(ConfigParser, ParsesValidConfig) {
  const std::string json = R"({
    "watch_interval_ms": 500,
    "normal_bin_dir": "build/bin",
    "tsan_bin_dir": "build-tsan/bin",
    "normal_lib_dir": "build/lib",
    "tsan_lib_dir": "build-tsan/lib",
    "tsan_runner": "tsan/tsan-run.sh",
    "components": [
      { "key": "a-1", "name": "heartbeat", "variant": "tsan",   "args": ["--instance", "1"] },
      { "key": "a-2", "name": "heartbeat", "variant": "normal" }
    ]
  })";
  const auto path = write_temp_json(json);

  cplus::config::controller_config cfg;
  std::string err;
  ASSERT_TRUE(cplus::config::load_config(path, cfg, err)) << err;

  EXPECT_EQ(cfg.watch_interval_ms, 500);
  EXPECT_EQ(cfg.normal_bin_dir, "build/bin");
  EXPECT_EQ(cfg.tsan_lib_dir, "build-tsan/lib");
  EXPECT_EQ(cfg.tsan_runner, "tsan/tsan-run.sh");
  ASSERT_EQ(cfg.components.size(), 2u);
  EXPECT_EQ(cfg.components[0].key, "a-1");
  EXPECT_EQ(cfg.components[0].name, "heartbeat");
  EXPECT_EQ(cfg.components[0].variant, "tsan");
  EXPECT_EQ(cfg.components[0].args, (std::vector<std::string>{"--instance", "1"}));
  // 未指定 key 时默认取 name
  EXPECT_EQ(cfg.components[1].key, "heartbeat");
  EXPECT_EQ(cfg.components[1].variant, "normal");
  EXPECT_TRUE(cfg.components[1].args.empty());

  std::filesystem::remove(path);
}

TEST(ConfigParser, DefaultsWatchIntervalWhenMissing) {
  const std::string json = R"({
    "normal_bin_dir": "b", "tsan_bin_dir": "t",
    "normal_lib_dir": "bl", "tsan_lib_dir": "tl"
  })";
  const auto path = write_temp_json(json);
  cplus::config::controller_config cfg;
  std::string err;
  ASSERT_TRUE(cplus::config::load_config(path, cfg, err)) << err;
  EXPECT_EQ(cfg.watch_interval_ms, 1000);
  EXPECT_TRUE(cfg.components.empty());
  std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsMalformedJson) {
  const auto path = write_temp_json("{ not valid json ");
  cplus::config::controller_config cfg;
  std::string err;
  EXPECT_FALSE(cplus::config::load_config(path, cfg, err));
  EXPECT_FALSE(err.empty());
  std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsMissingRequiredField) {
  const auto path = write_temp_json(R"({ "watch_interval_ms": 500 })");
  cplus::config::controller_config cfg;
  std::string err;
  EXPECT_FALSE(cplus::config::load_config(path, cfg, err));
  EXPECT_FALSE(err.empty());
  std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsInvalidVariant) {
  const std::string json = R"({
    "normal_bin_dir": "b", "tsan_bin_dir": "t",
    "normal_lib_dir": "bl", "tsan_lib_dir": "tl",
    "components": [ { "name": "heartbeat", "variant": "release" } ]
  })";
  const auto path = write_temp_json(json);
  cplus::config::controller_config cfg;
  std::string err;
  EXPECT_FALSE(cplus::config::load_config(path, cfg, err));
  EXPECT_TRUE(err.find("invalid variant") != std::string::npos);
  std::filesystem::remove(path);
}

TEST(ConfigParser, RejectsDuplicateKey) {
  const std::string json = R"({
    "normal_bin_dir": "b", "tsan_bin_dir": "t",
    "normal_lib_dir": "bl", "tsan_lib_dir": "tl",
    "components": [
      { "name": "a", "variant": "normal" },
      { "name": "a", "variant": "tsan" }
    ]
  })";
  const auto path = write_temp_json(json);
  cplus::config::controller_config cfg;
  std::string err;
  EXPECT_FALSE(cplus::config::load_config(path, cfg, err));
  EXPECT_TRUE(err.find("duplicate component key") != std::string::npos);
  std::filesystem::remove(path);
}

TEST(ConfigParser, MissingFileReturnsFalse) {
  cplus::config::controller_config cfg;
  std::string err;
  EXPECT_FALSE(cplus::config::load_config("/no/such/config.json", cfg, err));
  EXPECT_FALSE(err.empty());
}