// bench/src/bench_threads.cpp
// TSan vs 非 TSan 性能对比基准。
// 负载：多线程并发（互斥计数 + atomic 计数 + 共享读写），全部使用正确同步原语，
// 保持 TSan 报告安静，测的是纯插桩开销，而不是找竞态。
//
// 用法：bench_threads --threads N --seconds S
//   N 默认 4，S 默认 3。
// 输出（英文）：threads/seconds、wall_ms（墙钟）、cpu_ms（CPU 总耗时）、rss_kb（峰值内存）。

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <utility>

#include <sys/resource.h>
#include <time.h>

namespace {

double cpu_seconds() {
  struct timespec ts;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1e9;
}

}  // namespace

int main(int argc, char* argv[]) {
  int nthreads = 4;
  int seconds = 3;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
      nthreads = std::atoi(argv[++i]);
    } else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc) {
      seconds = std::atoi(argv[++i]);
    }
  }

  std::mutex mu;
  long mutex_count = 0;
  std::atomic<long> atomic_count{0};
  std::shared_mutex rw;
  std::vector<long> buf(1024, 0);
  std::atomic<bool> stop{false};

  const double cpu0 = cpu_seconds();
  const auto wall0 = std::chrono::steady_clock::now();

  std::vector<std::thread> pool;
  for (int i = 0; i < nthreads; ++i) {
    pool.emplace_back([&] {
      while (!stop.load(std::memory_order_relaxed)) {
        {
          std::lock_guard<std::mutex> lk(mu);
          mutex_count += 1;
        }
        atomic_count.fetch_add(1, std::memory_order_relaxed);
        {
          std::shared_lock<std::shared_mutex> lk(rw);
          long s = 0;
          for (auto v : buf) s += v;
          volatile long sink = s;
          (void)sink;
        }
        if ((atomic_count.load(std::memory_order_relaxed) & 7) == 0) {
          std::unique_lock<std::shared_mutex> lk(rw);
          for (auto& v : buf) v += 1;
        }
      }
    });
  }

  std::this_thread::sleep_for(std::chrono::seconds(seconds));
  stop.store(true);
  for (auto& t : pool) t.join();

  const auto wall1 = std::chrono::steady_clock::now();
  const double cpu1 = cpu_seconds();
  // 工作量：限时窗口内完成的循环次数（atomic_count 每轮递增，多线程共用）
  const long ops = atomic_count.load(std::memory_order_relaxed);
  volatile long sink_count = mutex_count;
  (void)sink_count;

  const long wall_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(wall1 - wall0).count();
  const long cpu_ms = static_cast<long>((cpu1 - cpu0) * 1000.0);

  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
#ifdef __APPLE__
  const long rss_kb = ru.ru_maxrss / 1024;  // macOS 单位是字节
#else
  const long rss_kb = ru.ru_maxrss;  // Linux 单位是 KB
#endif

  std::printf("bench threads=%d seconds=%d\n", nthreads, seconds);
  std::printf("ops=%ld wall_ms=%ld cpu_ms=%ld rss_kb=%ld\n", ops, wall_ms, cpu_ms, rss_kb);
  return 0;
}