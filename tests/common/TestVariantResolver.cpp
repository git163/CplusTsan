// tests/common/TestVariantResolver.cpp
// 覆盖：普通/TSan 二进制路径解析、动态库目录解析、非法变体、路径拼接。

#include <gtest/gtest.h>

#include "common/config_parser.h"
#include "common/variant_resolver.h"

namespace {

cplus::config::controller_config make_config() {
  cplus::config::controller_config c;
  c.normal_bin_dir = "build/bin";
  c.tsan_bin_dir = "build-tsan/bin";
  c.normal_lib_dir = "build/lib";
  c.tsan_lib_dir = "build-tsan/lib";
  return c;
}

}  // namespace

TEST(VariantResolver, ResolvesComponentBinary) {
  auto c = make_config();
  cplus::config::component_spec s;
  s.name = "heartbeat";

  s.variant = "normal";
  auto bin = cplus::resolver::component_bin(c, s);
  ASSERT_TRUE(bin.has_value());
  EXPECT_EQ(*bin, "build/bin/heartbeat");

  s.variant = "tsan";
  bin = cplus::resolver::component_bin(c, s);
  ASSERT_TRUE(bin.has_value());
  EXPECT_EQ(*bin, "build-tsan/bin/heartbeat");

  s.variant = "invalid";
  EXPECT_FALSE(cplus::resolver::component_bin(c, s).has_value());
}

TEST(VariantResolver, ResolvesLibDir) {
  auto c = make_config();
  EXPECT_EQ(*cplus::resolver::variant_lib_dir(c, "normal"), "build/lib");
  EXPECT_EQ(*cplus::resolver::variant_lib_dir(c, "tsan"), "build-tsan/lib");
  EXPECT_FALSE(cplus::resolver::variant_lib_dir(c, "x").has_value());
  EXPECT_FALSE(cplus::resolver::variant_bin_dir(c, "x").has_value());
}

TEST(VariantResolver, JoinPath) {
  EXPECT_EQ(cplus::resolver::join_path("build/lib", "a.so"), "build/lib/a.so");
  EXPECT_EQ(cplus::resolver::join_path("build/lib/", "a.so"), "build/lib/a.so");
  EXPECT_EQ(cplus::resolver::join_path("", "a.so"), "a.so");
}