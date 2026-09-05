# 总控 & 基准 运行指南

本仓库的"总控（controller）"是一个**最小可运行的工程化示例**：调度多个组件进程，按配置文件热重载，把某个组件在 **TSan 变体 / 普通变体**之间针对性切换。配套一个基准程序，用于实测 TSan 开销。

## 1. 构建两种变体

```bash
./scripts/build.sh         # → build/bin、build/lib（普通，RelWithDebInfo）
./scripts/build-tsan.sh    # → build-tsan/bin、build-tsan/lib（TSan，-O1 -g 全量插桩）
```

两个变体产出的二进制/动态库**同名**，靠目录区分：`build/bin/heartbeat` 与 `build-tsan/bin/heartbeat`。

## 2. 启动总控

```bash
./build/bin/controller --config controller/config.json
```

启动后总控按配置调度组件。默认配置调度两个心跳实例：

```json
{ "key": "heartbeat-1", "name": "heartbeat", "variant": "tsan",   "args": ["--period-ms", "1000", "--instance", "1"] },
{ "key": "heartbeat-2", "name": "heartbeat", "variant": "normal", "args": ["--period-ms", "1000", "--instance", "2"] }
```

字段说明：`key` 是运行槽位唯一标识；`name` 对应二进制 basename；`variant` 取 `normal` 或 `tsan`；`args` 传给组件。

## 3. 针对性热切换（核心演示）

TSan 是**编译期插桩**，运行时切不了 → "切换"＝**杀旧进程、用另一变体的二进制+库目录重启**。总控把这一流程自动化了：

1. 编辑 `controller/config.json`，把某个组件的 `variant` 翻转（如把 `heartbeat-2` 从 `normal` 改为 `tsan`）后保存；
2. 总控检测到 mtime 变化 → **只重启被改的组件**，其余组件不动；
3. 关闭 `controller` 的终端，观察日志：该组件先 `exited code=0`（SIGTERM 优雅退出），再 `started pid=... variant=tsan bin=build-tsan/bin/heartbeat`。

**验证动态库真的切了**：心跳组件启动时会打印所加载的 `libcore` 绝对路径与版本：

```
[heartbeat] instance=2 libcore loaded from: .../build/lib/libcore.1.dylib    (core_version=core-v1)
[heartbeat] instance=2 libcore loaded from: .../build-tsan/lib/libcore.1.dylib (core_version=core-v1-tsan)
```

切换前后路径在 `build/lib` 与 `build-tsan/lib` 间变化（策略 A：总控为子进程前置 `LD_LIBRARY_PATH=<变体lib目录>`）。

### 手动控制

- `kill -HUP <controller_pid>` —— 立即重载配置（不等 mtime 轮询）；
- `kill -TERM <controller_pid>` —— 级联 SIGTERM 停掉所有组件后退出（2 秒后 SIGKILL 兜底）。

## 4. 组件崩溃自动重启

总控是 supervisor：组件意外退出会被自动拉起，退避 1s→2s→4s 封顶，稳定运行后衰减。可在运行时 `kill -9` 一个心跳实例观察自动恢复。

## 5. 压力/集成测试：`scripts/stress-controller.sh`

真实拉起总控，自动触发四类场景并断言：

```
0) 初始调度       两组件都在跑
1) 崩溃恢复        SIGKILL 后组件被自动重启、h1 不受影响
2) 热切换 normal→tsan   组件优雅退出后换变体，并断言真的加载了 tsan 库
3) 热切换 tsan→normal   切回
4) 优雅退出         TERM 后级联停掉所有组件、无残留进程
```

```bash
./scripts/stress-controller.sh      # PASS: stress-controller
```

该脚本也已注册为 CTest 集成用例 `stress.controller`（`build-tsan` 缺失时自动跳过 tsan 场景）。它同时是 **Linux 移植的回归冒烟**：不依赖 `/proc`，mac/linux 通用。

## 6. 性能基准 A/B

```bash
./build/bin/bench_threads --threads 8 --seconds 5
./build-tsan/bin/bench_threads --threads 8 --seconds 5
```

- 预期：TSan 墙钟/CPU **5–15 倍**、RSS **5–10 倍**（配套说明见 `bench/benchmark.md`）。
- 解读与服务器影响分析见 `docs/tsan-performance-impact.md`。

## 7. 手动跑一个 TSan 组件（不经总控）

```bash
TSAN_LOG_DIR=/tmp/tsan ./tsan/tsan-run.sh \
  ./build-tsan/bin/heartbeat --period-ms 500 --instance 9
```

报告落在 `/tmp/tsan/tsan.<pid>`；macOS 上 `DYLD_LIBRARY_PATH` 注入受 SIP 影响，动态库切换验证以 Linux 为准。