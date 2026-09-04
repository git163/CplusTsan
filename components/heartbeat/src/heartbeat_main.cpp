// components/heartbeat/src/heartbeat_main.cpp
// 心跳示例组件：周期打印心跳日志（英文）。作为"被总控调度的组件"演示。
//   - 启动时用 dladdr 打印 libcore 实际加载路径，便于验证动态库变体切换。
//   - 支持 SIGTERM/SIGINT 优雅退出。
// 用法：heartbeat --period-ms <ms> --instance <n>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include <dlfcn.h>
#include <unistd.h>

#include "core_api.h"

namespace {
volatile sig_atomic_t g_stop = 0;

extern "C" void on_term(int) { g_stop = 1; }
}  // namespace

int main(int argc, char* argv[]) {
  int period_ms = 1000;
  int instance = 1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--period-ms") == 0 && i + 1 < argc) {
      period_ms = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--instance") == 0 && i + 1 < argc) {
      instance = std::atoi(argv[++i]);
    }
  }

  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_term;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

  // 打印 libcore 实际加载路径：切换变体后应观察到 build/lib ↔ build-tsan/lib 变化
  Dl_info info;
  if (dladdr(reinterpret_cast<void*>(&core_version), &info) != 0 && info.dli_fname) {
    std::printf("[heartbeat] instance=%d libcore loaded from: %s (core_version=%s)\n",
                instance, info.dli_fname, core_version());
  } else {
    std::printf("[heartbeat] instance=%d libcore: dladdr failed\n", instance);
  }
  std::fflush(stdout);

  int seq = 0;
  while (!g_stop) {
    std::this_thread::sleep_for(std::chrono::milliseconds(period_ms));
    std::printf("[heartbeat] instance=%d tick seq=%d pid=%d\n", instance, ++seq,
                static_cast<int>(getpid()));
    std::fflush(stdout);
  }

  std::printf("[heartbeat] instance=%d shutting down\n", instance);
  std::fflush(stdout);
  return 0;
}