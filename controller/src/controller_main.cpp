// controller/src/controller_main.cpp
//
// 总控示例：调度多个组件进程，按配置文件热重载实现 TSan / 非 TSan 针对性切换。
//
// 使用：./build/bin/controller --config controller/config.json
//
// 交互：
//   - 编辑 controller/config.json 中某组件的 variant → 总控检测到 mtime 变化，
//     按 key 定位对应组件并重启（旧进程组先 SIGTERM；TSan 是编译期插桩，
//     "切换"本质上就是重启进程换用另一变体的二进制及其动态库目录）。
//   - kill -HUP <pid> 立即重载配置；kill -TERM <pid> 优雅停掉全部组件后退出。
//
// 设计要点：
//   - 单线程 poll 循环 + fork/exec（避免多线程进程 fork 的 TSan 风险）。
//   - 每组件独立进程组（pgid==pid），kill(-pid) 组杀可连带孙进程。
//   - 崩溃自动重启，指数退避 1s→2s→4s 封顶；稳定运行后计数衰减。
//   - 动态库变体（策略 A）：spawn 前把 <变体lib目录> 前置到子进程 LD_LIBRARY_PATH。
//
// 日志使用英文。

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <chrono>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/config_parser.h"
#include "common/process_util.h"
#include "common/variant_resolver.h"

namespace {

using clock_t = std::chrono::steady_clock;

volatile sig_atomic_t g_reload = 0;
volatile sig_atomic_t g_stop = 0;

extern "C" void on_signal(int sig) {
  if (sig == SIGHUP) {
    g_reload = 1;
  } else if (sig == SIGTERM || sig == SIGINT) {
    g_stop = 1;
  }
  // SIGCHLD：不加 SA_RESTART，信号会打断阻塞中的 poll → 循环立即处理
}

struct component_state {
  cplus::config::component_spec desired;                    // 当前生效规格
  std::optional<cplus::config::component_spec> pending;     // 旧进程退出后按此重启
  bool running = false;
  bool remove_on_exit = false;                              // 已从配置移除，等退出后清理
  pid_t pid = -1;
  int fail_count = 0;
  clock_t::time_point started_at{};
  clock_t::time_point next_restart_at{};
};

using state_map = std::map<std::string, component_state>;

state_map g_states;
cplus::config::controller_config g_conf;
std::string g_config_path;
std::time_t g_last_mtime = 0;

long backoff_ms(int fail_count) {
  long delay = 1000;
  for (int i = 1; i < fail_count && delay < 4000; ++i) delay *= 2;
  return std::min(delay, 4000L);
}

// 拉起一个组件。失败时设置 2 秒后重试；desired 总是先写入，供重试复用。
bool spawn_state(const std::string& key, component_state& st, const cplus::config::component_spec& spec) {
  st.desired = spec;

  auto bin = cplus::resolver::component_bin(g_conf, spec);
  if (!bin) {
    std::printf("[controller] %s: cannot resolve binary for variant=%s\n",
                key.c_str(), spec.variant.c_str());
    st.next_restart_at = clock_t::now() + std::chrono::seconds(2);
    std::fflush(stdout);
    return false;
  }

  auto lib = cplus::resolver::variant_lib_dir(g_conf, spec.variant);
  const bool via_runner = (spec.variant == "tsan");
  std::string err;
  pid_t pid = cplus::proc::spawn_child(*bin, spec.args, lib.value_or(""), via_runner,
                                       g_conf.tsan_runner, err);
  if (pid < 0) {
    std::printf("[controller] %s: spawn failed: %s\n", key.c_str(), err.c_str());
    st.next_restart_at = clock_t::now() + std::chrono::seconds(2);
    std::fflush(stdout);
    return false;
  }

  st.pid = pid;
  st.running = true;
  st.pending.reset();
  st.remove_on_exit = false;
  st.started_at = clock_t::now();
  st.next_restart_at = {};
  std::printf("[controller] %s started pid=%d variant=%s bin=%s\n", key.c_str(), pid,
              spec.variant.c_str(), bin->c_str());
  std::fflush(stdout);
  return true;
}

// 收割已退出子进程（非阻塞），并按状态机处理：移除 / 按 pending 换变体 / 退避重启。
void reap_children() {
  int status = 0;
  pid_t pid = 0;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (auto it = g_states.begin(); it != g_states.end(); ++it) {
      auto& st = it->second;
      if (!st.running || st.pid != pid) continue;
      st.running = false;
      st.pid = -1;

      if (WIFEXITED(status)) {
        std::printf("[controller] %s exited code=%d\n", it->first.c_str(), WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        std::printf("[controller] %s killed signal=%d\n", it->first.c_str(), WTERMSIG(status));
      } else {
        std::printf("[controller] %s exited\n", it->first.c_str());
      }
      std::fflush(stdout);

      if (st.remove_on_exit) {
        g_states.erase(it);
      } else if (st.pending.has_value()) {
        auto spec = *st.pending;
        st.pending.reset();
        spawn_state(it->first, st, spec);
      } else {
        st.fail_count += 1;
        st.next_restart_at = clock_t::now() + std::chrono::milliseconds(backoff_ms(st.fail_count));
      }
      break;
    }
  }
}

// 把期望配置落到状态机：移除缺失项、新增项、规格变更项（按 key 定向重启）。
void apply_desired(const std::vector<cplus::config::component_spec>& desired) {
  // 移除已从配置消失的组件
  for (auto it = g_states.begin(); it != g_states.end();) {
    const bool wanted = std::any_of(desired.begin(), desired.end(),
                                    [&](const auto& s) { return s.key == it->first; });
    if (wanted) {
      ++it;
      continue;
    }
    auto& st = it->second;
    if (st.running) {
      st.remove_on_exit = true;
      cplus::proc::kill_group(st.pid, SIGTERM);
    } else {
      g_states.erase(it);
      it = g_states.begin();  // erase 使迭代器失效，重置后重新遍历；列表都很小，可接受
      continue;
    }
    ++it;
  }

  // 新增 / 变更
  for (const auto& spec : desired) {
    auto it = g_states.find(spec.key);
    if (it == g_states.end()) {
      auto ret = g_states.emplace(spec.key, component_state{});
      spawn_state(spec.key, ret.first->second, spec);
      continue;
    }
    auto& st = it->second;
    const bool changed = st.desired.name != spec.name || st.desired.variant != spec.variant ||
                         st.desired.args != spec.args;
    if (!changed) continue;
    if (st.running) {
      // 先停旧进程，退出后由 reap 用 pending 规格拉起新变体
      st.pending = spec;
      cplus::proc::kill_group(st.pid, SIGTERM);
    } else {
      spawn_state(spec.key, st, spec);
    }
  }
}

// 到点重启处于"待重启"状态的组件。
void restart_due() {
  const auto now = clock_t::now();
  for (auto& kv : g_states) {
    auto& st = kv.second;
    if (st.running || st.remove_on_exit || st.pending.has_value()) continue;
    if (now < st.next_restart_at) continue;
    spawn_state(kv.first, st, st.desired);
  }
}

// 稳定运行一段时间后衰减失败计数，避免"一次抖动导致长期低速重启"。
void decay_fail_counts() {
  const auto now = clock_t::now();
  for (auto& kv : g_states) {
    auto& st = kv.second;
    if (st.running && now - st.started_at > std::chrono::seconds(8)) st.fail_count = 0;
  }
}

// 读取并应用配置。force=false 时仅当 mtime 变化才重载；解析失败保留 last-good。
void load_and_apply(const std::string& path, bool force) {
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    std::printf("[controller] config stat failed: %s (keep current)\n", path.c_str());
    std::fflush(stdout);
    return;
  }
  if (!force && st.st_mtime == g_last_mtime) return;

  cplus::config::controller_config cfg;
  std::string err;
  if (!cplus::config::load_config(path, cfg, err)) {
    std::printf("[controller] config reload failed: %s (keep current)\n", err.c_str());
    std::fflush(stdout);
    return;
  }

  g_last_mtime = st.st_mtime;
  g_conf = std::move(cfg);
  std::printf("[controller] config reloaded\n");
  std::fflush(stdout);
  apply_desired(g_conf.components);
}

// 停掉全部组件：SIGTERM → 最多等 2 秒 → SIGKILL 兜底 → 全部收割。
void shutdown_all() {
  for (auto& kv : g_states) {
    if (kv.second.running) cplus::proc::kill_group(kv.second.pid, SIGTERM);
  }

  const auto deadline = clock_t::now() + std::chrono::seconds(2);
  while (clock_t::now() < deadline) {
    bool any_alive = false;
    for (auto& kv : g_states) {
      if (kv.second.running && cplus::proc::is_alive(kv.second.pid)) {
        any_alive = true;
        break;
      }
    }
    if (!any_alive) break;
    poll(nullptr, 0, 50);
  }

  for (auto& kv : g_states) {
    if (kv.second.running && cplus::proc::is_alive(kv.second.pid)) {
      cplus::proc::kill_group(kv.second.pid, SIGKILL);
    }
  }
  while (waitpid(-1, nullptr, 0) > 0) {}
  std::printf("[controller] all components stopped\n");
  std::fflush(stdout);
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string config_path = "controller/config.json";
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) config_path = argv[++i];
  }

  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;  // 不加 SA_RESTART，让 poll 能被信号打断
  sigaction(SIGHUP, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGCHLD, &sa, nullptr);
  signal(SIGPIPE, SIG_IGN);

  std::string err;
  if (!cplus::config::load_config(config_path, g_conf, err)) {
    std::printf("[controller] config error: %s\n", err.c_str());
    return 1;
  }
  g_config_path = config_path;
  struct stat st;
  if (::stat(config_path.c_str(), &st) == 0) g_last_mtime = st.st_mtime;

  apply_desired(g_conf.components);
  std::printf("[controller] running, watch_interval_ms=%d\n", g_conf.watch_interval_ms);
  std::fflush(stdout);

  while (!g_stop) {
    if (g_reload) {
      g_reload = 0;
      load_and_apply(g_config_path, true);
    }
    reap_children();
    restart_due();
    decay_fail_counts();
    load_and_apply(g_config_path, false);

    const int timeout = g_conf.watch_interval_ms > 0 ? g_conf.watch_interval_ms : 1000;
    poll(nullptr, 0, timeout);
  }

  shutdown_all();
  return 0;
}