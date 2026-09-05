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

`ops` 是同一限时窗口内两边各自完成的工作量（吞吐近似值）。开销倍数 ≈ **normal.ops / tsan.ops**：

- 经验上 TSan 侧 `ops` 通常是普通侧的 **1/5~1/15**（负载越重、同步越密越接近下限），即跑慢 **5–15 倍**；
- `rss_kb`：TSan 需为每块内存维护 shadow（影子内存），峰值常为普通变体的 **5–10 倍**（小型程序因基线小，倍数可能更大），并预留巨大虚拟地址空间；
- `wall_ms`/`cpu_ms` 仅供参考（受锁争用影响，不作为开销主指标）。

> 示例口径（本机实测，仅示意数值）：
>
> ```
> # normal                          # tsan
> ops=561994 wall_ms=4005 rss_kb=1184   ops=193407 wall_ms=4001 rss_kb=34240
> ```
>
> 即本机此负载下吞吐约 **2.9 倍**变慢、RSS 约 **29 倍**（小型进程基线小，RSS 倍数偏大）。
> 倍数随硬件/负载浮动：文献典型 **5–15 倍**（Linux + 更重负载），macOS TSan 更接近下限。

- **结论落到文档**：`docs/tsan-performance-impact.md`。要点先行——TSan 是编译期插桩，**切换只能靠重启进程换变体**；**每个进程都开 TSan 会让服务器 CPU 迅速饱和**，正确姿势是总控按需、针对性地只给某几个组件开 TSan（见 `docs/controller-guide.md`）。

> 提示：TSan 变体首次启动会较慢（要建立 shadow 映射），`--seconds` 建议 ≥ 5 以摊平启动成本。