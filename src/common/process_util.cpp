#include "common/process_util.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <signal.h>
#include <unistd.h>

namespace cplus {
namespace proc {

pid_t spawn_child(const std::string& bin_path,
                  const std::vector<std::string>& args,
                  const std::string& lib_search_dir,
                  bool via_runner,
                  const std::string& runner_path,
                  std::string& err) {
  const std::string& effective_bin = via_runner ? runner_path : bin_path;

  pid_t pid = fork();
  if (pid < 0) {
    err = std::string("fork failed: ") + std::strerror(errno);
    return -1;
  }

  if (pid == 0) {
    // 子进程：独立进程组（pgid == 自身 pid），便于总控整组管理
    setpgid(0, 0);

    // 策略 A：库路径前置注入，首个匹配胜出；外部固定 env 不受影响。
    // Linux 用 LD_LIBRARY_PATH；macOS 开发机额外注入 DYLD_LIBRARY_PATH（SIP 允许时生效）。
    // 部署验证以 Linux 为准。
    if (!lib_search_dir.empty()) {
      const char* cur = getenv("LD_LIBRARY_PATH");
      std::string value =
          (cur && *cur) ? (lib_search_dir + ":" + cur) : lib_search_dir;
      setenv("LD_LIBRARY_PATH", value.c_str(), 1);

      const char* dcur = getenv("DYLD_LIBRARY_PATH");
      std::string dvalue =
          (dcur && *dcur) ? (lib_search_dir + ":" + dcur) : lib_search_dir;
      setenv("DYLD_LIBRARY_PATH", dvalue.c_str(), 1);
    }

    std::vector<const char*> argv;
    if (via_runner) {
      argv.push_back(runner_path.c_str());
      argv.push_back(bin_path.c_str());
    } else {
      argv.push_back(bin_path.c_str());
    }
    for (const auto& a : args) argv.push_back(a.c_str());
    argv.push_back(nullptr);

    execv(effective_bin.c_str(), const_cast<char* const*>(argv.data()));
    std::fprintf(stderr, "[controller] exec failed (%s): %s\n",
                 effective_bin.c_str(), std::strerror(errno));
    std::fflush(stderr);
    _exit(127);
  }

  // 父进程侧 setpgid，与子进程侧形成闭环，规避 fork/exec 竞态窗口
  setpgid(pid, pid);
  return pid;
}

bool kill_group(pid_t child_pid, int sig) {
  if (child_pid <= 0) return false;
  if (::kill(-child_pid, sig) == 0) return true;
  return errno == ESRCH;  // 已退出视作成功
}

bool is_alive(pid_t pid) { return pid > 0 && ::kill(pid, 0) == 0; }

}  // namespace proc
}  // namespace cplus