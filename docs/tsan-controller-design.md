# 通用 TSan 工程模板（CplusTsan）

- 日期: 2026-09-05
- 作者: CplusTsan
- 状态: 已批准待实施
- 关联: 顶层 `CMakeLists.txt`、`src/`、`tests/`、`docs/`

## 背景

当前仓库只是 `init-cpp-project` 生成的普通 C++17 脚手架。用户希望把它改造成一个**通用的 ThreadSanitizer（TSan，数据竞争检测）工程模板**，方便其他 C++ 项目集成 TSan，并满足以下使用场景：

> 有一个总控（controller）调度多个组件进程，可随时按配置**针对性地把某个组件切换到 TSan / 非 TSan 变体**。

已确认的决策：总控用 C++17 且在本仓库实现（可运行示例）；切换触发走**配置文件热加载**；性能影响交付**文档分析 + 实测基准**。

### 用户问题的直接回答（完整版落进 `docs/tsan-performance-impact.md`）

- TSan 是**编译期插桩**，运行时无法改变插桩状态 → "随时切换"只能靠**重启目标组件进程、换用另一变体的二进制**。两个变体用两个构建目录（`build` / `build-tsan`）产出**同名二进制**。
- 性能影响：运行时典型 **5–15 倍变慢**、内存 **5–10 倍**、预留巨大虚拟地址空间（受 `ulimit -v` / 容器限制会在启动时报错）。
- **每个进程都开 TSan**：CPU 迅速饱和、时延/吞吐劣化、内存膨胀有 OOM 风险、调度与时序被扰动（可能掩盖或改变真实竞态）。不建议全量，适合**按需 / 部分实例选择性开启**，或在 dev/CI 使用——这正是"总控按组件切换"模式的价值。
- **suppression 只降低报告噪音，不降低性能开销**（文档中必须明确）。
- `halt_on_error=0` 对服务进程很重要（TSan 默认遇到首个 race 就退出）。macOS 下 TSan 可靠性差，**以 Linux 为验证基准**。

## 目标

- 可复用的 `cmake/tsan.cmake` 模块（选项 `ENABLE_TSAN`），其他项目可 `include()` 直接接入。
- 双构建目录变体工作流 + `scripts/build.sh` / `scripts/build-tsan.sh`。
- `tsan/tsan.supp` 抑制模板 + `tsan/tsan-run.sh` 启动器（单一 `TSAN_OPTIONS` 来源）。
- 可运行的 C++17 总控示例：按 `config.json` 调度组件、热重载切换变体、生命周期与信号管理。
- `components/heartbeat/` 示例组件，演示总控调度与切换。
- `bench/` 基准程序：TSan vs 非 TSan 实测对比（并发工作负载）。
- 3 个中文文档 + 本 plan 按规则 8 落盘 `docs/`。
- 单测覆盖可复用部件（配置解析、变体解析、抑制文件格式）。

非目标：HTTP 管理接口（未选）；生产级 supervisor（systemd 集成、崩溃守护原本就是 controller 的职责，但不过度堆功能）；故意制造竞态的演示（bench 只用正确加锁，保持 TSan 安静）。

## 方案

### 目标目录结构

```
CplusTsan/
├── CMakeLists.txt               # 重写：include cmake/tsan.cmake + add_subdirectory 各模块
├── cmake/tsan.cmake             # 可复用 TSan 集成模块（ENABLE_TSAN）
├── scripts/
│   ├── build.sh                 # cmake -S . -B build
│   └── build-tsan.sh            # cmake -S . -B build-tsan -DENABLE_TSAN=ON
├── tsan/
│   ├── tsan.supp                # 抑制模板（中文注释，race/deadlock/thread 示例）
│   └── tsan-run.sh              # 导出 TSAN_OPTIONS 后 exec "$@"
├── src/
│   ├── CMakeLists.txt           # 静态库 common
│   ├── common/                  # config_parser.cpp, process_util.cpp
│   └── include/common/          # config_parser.h, process_util.h
├── controller/
│   ├── CMakeLists.txt
│   ├── src/controller_main.cpp  # 监视循环、热重载、信号、子进程管理
│   └── config.json              # 组件→变体 配置（热重载源）
├── components/
│   ├── CMakeLists.txt
│   ├── core/                    # 共享库 libcore，演示“动态库组件的变体切换”
│   │   ├── CMakeLists.txt       # add_library(core SHARED)
│   │   └── src/core_api.cpp     # core_version() 等小接口，供 heartbeat 调用
│   └── heartbeat/
│       ├── CMakeLists.txt       # target_link_libraries(heartbeat PRIVATE core)
│       └── src/heartbeat_main.cpp  # 启动时用 dladdr 打印 libcore 实际加载路径
├── bench/
│   ├── CMakeLists.txt
│   ├── src/bench_threads.cpp
│   └── benchmark.md             # 中文 A/B 运行说明
├── tests/
│   ├── common/TestConfigParser.cpp
│   ├── common/TestVariantResolver.cpp
│   └── tsan/TestTsanSupp.cpp
└── docs/
    ├── tsan-controller-design.md    # 本 plan 落盘
    ├── tsan-integration-guide.md    # 其他 C++ 项目接入指南
    ├── controller-guide.md          # 总控 + 基准运行指南
    └── tsan-performance-impact.md   # 性能影响分析
```

### A. `cmake/tsan.cmake`（核心）

- `option(ENABLE_TSAN ... OFF)`；开启时强制 `build type = TSan`，并定义 `-fsanitize=thread -fno-omit-frame-pointer -O1 -g`（C/C++ 编译）与链接 flag，**同时 append 到全局 `CMAKE_CXX_FLAGS` / 链接 flags**，让 FetchContent 依赖和静态库也全部插桩 —— 满足"整进程插桩"要求（任一 TU 未插桩会带来假竞态）。
- `find_package(Threads REQUIRED)`，链接 `-pthread`。
- 顶部 `include()`，先于任何 `add_subdirectory`。
- macOS vs Linux 差异在注释中说明（模块可移植，但验证以 Linux 为准）。

### B. suppression + 启动器

- `tsan/tsan.supp`：中文注释说明格式（函数名子串匹配、`^...$` 锚定、无通配符），示例 `race:^ns::foo$`、`deadlock:`、`thread:` 等。文件头标注**可靠类型**（`race`/`deadlock`/`thread`；`signal`/`mutex` 等依赖版本，实施时用 `TestTsanSupp` + 本地运行时确认）。
- `tsan/tsan-run.sh`：`export TSAN_OPTIONS=halt_on_error=0:log_path=${TSAN_LOG_DIR:-/tmp/tsan}:suppressions=.../tsan.supp:second_deadlock_stack=1` 后 `exec "$@"`。

### C. bench

- `bench/bench_threads.cpp`：`--threads N --seconds S`，N 个线程做正确加锁的并发负载（互斥计数 + atomic 计数 + 读写锁段），按墙钟限时，输出墙钟 / CPU 时间（`clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`）/ 峰值 RSS（`getrusage`，注意 Linux `ru_maxrss` 是 KB、macOS 是字节，需归一化）。
- 构建两遍，A/B 对比；预期墙钟 5–15 倍变慢。

### D. 总控 `controller/` + 示例组件

- `config.json`：`watch_interval_ms`、普通/TSan **二进制目录**与**动态库目录**、组件列表（`name`、`variant`、`args`）。
- `controller_main.cpp` 单线程 poll 循环（避免父进程 TSan-after-fork 风险）：
  1. 解析配置 → 组件 → `{variant, args}`。
  2. `fork()` + child `setpgid(0,0)` + `execvp`（TSan 变体经 `tsan/tsan-run.sh`，普通变体直接 exec）；parent 记录 pid。**spawn 前为子进程组合环境**：`LD_LIBRARY_PATH=<变体lib目录>:$LD_LIBRARY_PATH`（前置优先），在“库路径环境变量已固定”的前提下实现动态库变体选择（见 F）。
  3. poll 配置 mtime；`SIGCHLD` 置位 + `waitpid(WNOHANG)` 收割（handler 内不做异步不安全操作）；子进程退出记日志 + 封顶指数退避重启。
  4. 变体变更：`kill(-pgid, SIGTERM)` → 等 ≤2s → `SIGKILL` 兜底 → 用新变体重启；**只重启受影响的组件**。
  5. `SIGTERM`/`SIGINT` 组杀子进程后退出；`SIGHUP` 立即重载配置；配置解析失败保留上一份 good config。
- **二进制解析**：`variant_resolver::resolve(name, variant)` → `<bin_dir>/<name>`，纯函数可单测，与仓库布局解耦。
- 组件名 == 二进制 basename；bin 路径放配置里。

### E. 测试

- `TestConfigParser.cpp`：解析样例 JSON、变体查询、坏 JSON 报错不抛。
- `TestVariantResolver.cpp`：normal vs tsan 路径、缺名报错。
- `TestTsanSupp.cpp`：`tsan/tsan.supp` 存在，且每行非注释匹配 `^(race|deadlock|thread|signal):`。

### F. 动态库组件与“固定环境变量”的处理（核心关切）

组件若是动态库形态，**TSan 变体 = 整个进程**（主程序 + 所有依赖 .so + TSan runtime 本身）必须是同一变体：

- 混装会出问题：未插桩的 .so 混进 TSan 进程 → 假竞态；TSan 的 .so 被 `dlopen` 进未插桩进程 → “TSan runtime 未在 library list 首位”直接报错。
- 动态库按 loader 搜索顺序查找：**DT_RPATH > LD_LIBRARY_PATH > DT_RUNPATH > ld.so.cache > 默认路径**（glibc）；macOS 为 `DYLD_LIBRARY_PATH` / `@rpath`。

针对“库路径环境变量已固定”的三种落地策略：

| 策略 | 机制 | 适用 | 代价 |
|---|---|---|---|
| A. 子进程环境注入（模板默认） | 总控 spawn 前把 `LD_LIBRARY_PATH` 前置为 `<变体lib目录>:$LD_LIBRARY_PATH`（首个匹配胜出），系统级固定 env 不动 | 变体库同名不同目录 | 需由总控（或同款启动器）拉起 |
| B. 不同 soname 共存（env 完全冻结） | 普通 `libfoo.so` / TSan `libfoo_tsan.so` 放**同一个**库目录，各二进制靠 DT_NEEDED 自选，不读任何环境变量 | 绝对不允许改 env / 手动运行 | 构建需产出双份库，部署带两套 |
| C. 前缀树 + `$ORIGIN` RPATH | 每变体自包含 `prefix/{bin,lib}`，`-Wl,-rpath,'$ORIGIN/../lib'` 配 `--disable-new-dtags`（DT_RPATH 优先于 LD_LIBRARY_PATH） | 无 env 依赖、可整体搬迁 | 使用旧式 RPATH（仍被普遍支持） |

要点：**不要“依赖 env 前置 + 旧式 RPATH”**——DT_RPATH 优先于 LD_LIBRARY_PATH，env 注入会失效；反过来普通二进制默认是 DT_RUNPATH，env 才能盖过它，这正是策略 A 生效的机理。策略 B/C 的构建细节与 CMake 片段写进集成指南；模板运行示例走 A。

## 关键设计决策（理由）

1. **同名二进制放两个 build 目录**（而非 `comp`/`comp_tsan` 双命名）：整进程插桩按构建强制，变体切换 = 换目录重启，语义最清晰。
2. **插桩 flag 用全局变量**而非 `target_compile_options`：只有全局 flag 能传导到 FetchContent / 静态库 TU。
3. **自定义 TSan build type `-O1 -g`**：绕开 RelWithDebInfo 的 `-O2 -DNDEBUG`；`-O1` 让竞态可复现、assert 生效。
4. `ENABLE_TSAN=ON` ⇔ build type `TSan` 都在模块里固定，避免"插桩但 -O3"。
5. 总控对 TSan 变体统一走 `tsan-run.sh`，TSan 配置单一来源。
6. 配置用 JSON + `nlohmann/json`（FetchContent，header-only，复用 gtest 的现有模式），封装成 `config_parser`（符合规则 6）。
7. 变体变更只重启受影响组件，运营扰动最小。
8. **不重写**已有 `tests/CMakeLists.txt` 的 `file(GLOB_RECURSE Test*.cpp)` 发现机制，子目录测试自动纳入。
9. **动态库变体按“变体库目录 + 每子进程 env 前置”解析**（策略 A）：库路径与二进制一样按变体解析，由总控统一注入子进程环境；B/C 作为文档化备选。

## 影响范围

- 重写顶层 `CMakeLists.txt`：去掉单入口 `GLOB_RECURSE ONE`，改为 `add_subdirectory(src|controller|components|bench|tests)`（避免把新模块编译进主 exe）。
- `src/main.cpp` 演进为 `common` 静态库（`src/common/` + `src/include/common/`）。
- 新增 `cmake/`、`scripts/`、`tsan/`、`controller/`、`components/`（含共享库 `core`）、`bench/` 及 3 个测试文件。
- `controller` 需为子进程组合环境（按变体注入动态库搜索路径），`config.json` 增加普通/TSan 库目录。
- `docs/` 4 个新文档（动态库部署策略并入集成指南）。

## 风险与对策

- **macOS 下 TSan 不稳定** → 文档明确 Linux + gcc/clang 为验证基准，模块保持可移植。
- **离线构建不可用**（FetchContent 拉 gtest/nlohmann）→ 集成指南注明需网络。
- **整程序插桩被破坏**（未插桩静态库）→ 全局 flag + 集成指南检查清单。
- **配置解析失败** → 保留 last-good 配置、记日志、不动子进程。
- **子进程崩溃循环** → 封顶指数退避（1s→2s→4s）。
- **TSan 巨大虚拟地址空间 vs 容器/`ulimit -v`** → 性能文档给出对策（`ulimit -v` 放宽、预期 RSS 5–10 倍、不要硬上限）。

## 实施步骤

- [ ] P0 — 重写顶层 `CMakeLists.txt` + `cmake/tsan.cmake` + `scripts/` + 最小 `heartbeat`。Gate：`build/` 与 `build-tsan/` 均 configure + build，heartbeat 在 TSan 下运行无报告。
- [ ] P1 — `src/common/config_parser`、`tsan/tsan.supp`、`tsan/tsan-run.sh`、`variant_resolver` + 3 个 `Test*.cpp`。Gate：两个构建树 `ctest` 全绿。
- [ ] P2 — `controller_main.cpp` + `config.json` + 启动/收割/信号逻辑。Gate：能起两种变体；改配置变体 → 对应组件重启；`SIGTERM` 级联；`SIGHUP` 重载。
- [ ] P3 — `bench_threads` + `benchmark.md`，再写 `tsan-performance-impact.md` / `tsan-integration-guide.md` / `controller-guide.md` / `docs/tsan-controller-design.md`。
- [ ] P4 — 端到端验证（见下），并确认本地 TSan 实际支持的 suppression 类型，写进集成指南。

## 验证

```bash
./scripts/build.sh && ./scripts/build-tsan.sh                 # 两个变体构建
ctest --test-dir build --output-on-failure                    # 双树单测
ctest --test-dir build-tsan --output-on-failure
./build/controller                                            # 起总控；改 config.json 的 variant → 观察对应组件被切重启
#   ↑ 切换前后观察 heartbeat 日志 `libcore loaded from:` 路径在 build/lib ↔ build-tsan/lib 间切换，验证动态库变体生效
./build/bench_threads --threads 8 --seconds 5                 # 基准 A/B
./build-tsan/bench_threads --threads 8 --seconds 5            # 预期 5–15x 变慢
./tsan/tsan-run.sh ./build-tsan/components/heartbeat/heartbeat --period-ms 500   # TSan 自检：应无报告
```