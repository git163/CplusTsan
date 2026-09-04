#ifndef CPLUSTSAN_COMMON_PROCESS_UTIL_H_
#define CPLUSTSAN_COMMON_PROCESS_UTIL_H_

#include <string>
#include <vector>

#include <sys/types.h>

namespace cplus {
namespace proc {

// 派生子进程并执行给定二进制。
//  - lib_search_dir 非空时，子进程环境会把它前置到 LD_LIBRARY_PATH（动态库变体选择）。
//  - via_runner 为真时，先执行 runner_path（启动器脚本），其第一实参为 bin_path，再接 args。
// 正常返回子进程 pid；失败返回 -1 并把错误写入 err。
pid_t spawn_child(const std::string& bin_path,
                  const std::vector<std::string>& args,
                  const std::string& lib_search_dir,
                  bool via_runner,
                  const std::string& runner_path,
                  std::string& err);

// 向子进程所在的进程组发送信号（进程组 id == child_pid）。组内任意进程，
// 包括孙进程，都会被波及，便于总控整组回收。
bool kill_group(pid_t child_pid, int sig);

// pid 是否存活（kill(pid, 0) 探测）。
bool is_alive(pid_t pid);

}  // namespace proc
}  // namespace cplus

#endif  // CPLUSTSAN_COMMON_PROCESS_UTIL_H_