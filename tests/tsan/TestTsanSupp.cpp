// tests/tsan/TestTsanSupp.cpp
// 校验抑制模板文件的合法性：存在、可读、每一行（跳过空行/注释）都是 "<type>:<pattern>"，
// 且 type 属于已知类型集合。防止误改把抑制文件改坏。

#include <gtest/gtest.h>

#include <fstream>
#include <string>

#ifndef TSAN_SUPP_PATH
#error "TSAN_SUPP_PATH 未定义（应在 tests/CMakeLists.txt 里由编译定义注入）"
#endif

namespace {

constexpr const char* kKnownTypes[] = {
    "race", "deadlock", "thread", "signal", "mutex", "atomic", "vptr",
};

bool known_type(const std::string& t) {
  for (const auto* k : kKnownTypes) {
    if (t == k) return true;
  }
  return false;
}

}  // namespace

TEST(TsanSupp, FileExistsAndValidLines) {
  std::ifstream ifs(TSAN_SUPP_PATH);
  ASSERT_TRUE(ifs) << "cannot open suppressions file: " TSAN_SUPP_PATH;

  std::string line;
  int entry_count = 0;
  int line_no = 0;
  while (std::getline(ifs, line)) {
    ++line_no;
    // 去掉行尾回车（Windows 换行），并跳过空行与注释
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;

    ++entry_count;
    const auto colon = line.find(':');
    ASSERT_NE(colon, std::string::npos) << "line " << line_no << " missing ':' : " << line;
    const std::string type = line.substr(0, colon);
    const std::string pattern = line.substr(colon + 1);
    EXPECT_TRUE(known_type(type)) << "line " << line_no << " unknown type: " << type;
    EXPECT_FALSE(pattern.empty()) << "line " << line_no << " empty pattern";
  }

  EXPECT_GT(entry_count, 0);
}