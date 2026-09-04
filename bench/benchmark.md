# 基准使用说明（bench_threads）

`bench_threads` 用同样一份并发代码构建出**普通变体**与 **TSan 变体**，对比插桩带来的真实开销。它只使用正确的同步原语（互斥锁、原子操作、读写锁），不主动制造竞态，因此测的是纯 TSan 运行时代价。

## 构建两个变体

```bash
./scripts/build.sh         # → build/bin/bench_threads（普通）
./scripts/build-tsan.sh    # → build-tsan/bin/bench_threads（TSan）
```

## A/B 对比

控制变量：同一台机器、同样的 `--threads/--seconds`。

```bash
./build/bin/bench_threads --threads 8 --seconds 5
./build-tsan/bin/bench_threads --threads 8 --seconds 5
```

输出示例（单位 ms / KB）：

```
bench threads=8 seconds=5
wall_ms=5000 cpu_ms=12000 rss_kb=180000
```

## 读结果

- **wall_ms / cpu_ms 比值**：TSan 变体通常是普通变体的 **5–15 倍**（负载越重、同步越密越接近上限）。
- **rss_kb**：TSan 需为每块内存维护 shadow（影子内存），峰值常为普通变体的 **5–10 倍**，并预留巨大虚拟地址空间。
- **结论落到文档**：`docs/tsan-performance-impact.md`。要点先行——TSan 是编译期插桩，**切换只能靠重启进程换变体**；**每个进程都开 TSan 会让服务器 CPU 迅速饱和**，正确姿势是总控按需、针对性地只给某几个组件开 TSan（见 `docs/controller-guide.md`）。

> 提示：TSan 变体首次启动会较慢（要建立 shadow 映射），`--seconds` 建议 ≥ 5 以摊平启动成本。