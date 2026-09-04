// tests/common/TestProcessUtil.cpp
// 覆盖：spawn_child 启动/进程组信号/回收、exec 失败退出码 127、对已退出进程组 kill 的行为。

#include <gtest/gtest.h>

#include <string>
#include <sys/wait.h>
#include <unistd.h>

#include "common/process_util.h"

TEST(ProcessUtil, SpawnThenGroupKill) {
  std::string err;
  pid_t pid = cplus::proc::spawn_child("/bin/sh", {"-c", "sleep 5"}, "", false, "", err);
  ASSERT_GT(pid, 0) << err;

  EXPECT_TRUE(cplus::proc::is_alive(pid));

  // 对进程组发 SIGTERM，应把 sh（组内）杀掉；随后 waitpid 可正常回收
  EXPECT_TRUE(cplus::proc::kill_group(pid, SIGTERM));
  int status = 0;
  EXPECT_EQ(waitpid(pid, &status, 0), pid);
  EXPECT_FALSE(WIFSTOPPED(status));
  EXPECT_TRUE(WIFSIGNALED(status));
  EXPECT_EQ(WTERMSIG(status), SIGTERM);

  EXPECT_FALSE(cplus::proc::is_alive(pid));
}

TEST(ProcessUtil, SpawnMissingBinaryExits127) {
  // fork 成功，exec 在子进程内失败 → 退出码 127（父进程拿到的是子进程状态，而非错误）
  std::string err;
  pid_t pid = cplus::proc::spawn_child("/no/such/binary", {}, "", false, "", err);
  ASSERT_GT(pid, 0) << err;

  int status = 0;
  EXPECT_EQ(waitpid(pid, &status, 0), pid);
  EXPECT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 127);
}

TEST(ProcessUtil, KillGroupAlreadyGoneIsSuccess) {
  // 进程组不存在 → 视为成功（幂等）
  EXPECT_TRUE(cplus::proc::kill_group(999999, SIGTERM));
}