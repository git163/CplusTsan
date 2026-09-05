# CplusTsan

通用的 **ThreadSanitizer（TSan，数据竞争检测）工程模板**：其他 C++ 项目可一键接入 TSan、使用抑制模板，并通过一个可运行的**总控（controller）**按配置在 TSan / 非 TSan 变体之间**针对性热切换**组件进程。Linux 为一级验证基准，macOS 用于开发/冒烟。

## 快速开始

```bash
./scripts/build.sh          # 普通变体（build/，RelWithDebInfo）
./scripts/build-tsan.sh     # TSan 变体（build-tsan/，全量插桩 -O1 -g）

# 单元测试 + 集成场景测试（两套构建树都要跑）
ctest --test-dir build --output-on-failure
ctest --test-dir build-tsan --output-on-failure

# 起总控，演示"改配置即切换组件变体"
./build/bin/controller --config controller/config.json
```

要点速览：

- **双变体双目录**：`build/` 与 `build-tsan/` 产出同名二进制/动态库，`ENABLE_TSAN=ON` 一键切换。
- **整进程插桩**：`cmake/tsan.cmake` 把 `-fsanitize=thread` 注入全局 flags，依赖也被插桩，避免假竞态。
- **抑制模板**：`tsan/tsan.supp` + 启动器 `tsan/tsan-run.sh`（`halt_on_error=0`、报告落盘、抑制自动生效）。
- **总控热切换**：编辑 `controller/config.json` 的 `variant` → 只重启被改的组件，动态库也随变体切换（策略 A：子进程 `LD_LIBRARY_PATH` 前置注入）。
- **压力/集成测试**：`scripts/stress-controller.sh` 自动触发崩溃恢复 / 变体热切换 / 优雅退出场景并断言（注册为 CTest 用例 `stress.controller`）。
- **性能实测**：`bench/bin/bench_threads`（`build/bin` vs `build-tsan/bin`），预期 5–15 倍变慢。

## 目录

- `cmake/tsan.cmake` — TSan 集成模块（可复制到其他项目）
- `cmake/vendor.cmake` — 第三方库离线解压 helper
- `third_party/` — 第三方库压缩包（googletest / nlohmann_json，**只提交压缩包**，构建时由 CMake 自动解压，离线可构建）
- `src/` — `common` 库：配置解析 / 进程工具 / 变体解析（`src/include/common/`）
- `controller/` — 总控示例（调度 + 热切换）
- `components/` — 示例组件：`heartbeat`（可执行）+ `core`（共享库，演示动态库变体切换）
- `bench/` — 性能基准 + 使用说明
- `tsan/` — 抑制模板 + 启动器
- `tests/` — 单元测试（GTest）+ 集成场景
- `docs/` — 设计 / 性能影响 / 接入指南 / 运行指南

## 文档

- [设计文档](docs/tsan-controller-design.md)
- [TSan 性能影响分析](docs/tsan-performance-impact.md)
- [接入指南（其他项目如何复用）](docs/tsan-integration-guide.md)
- [总控与基准运行指南](docs/controller-guide.md)

## 需求

- CMake ≥ 3.20，C++17 编译器（GCC ≥ 7 / Clang ≥ 5）
- Linux（验证基准）；macOS 可用但 TSan 结果仅供参考
- 构建**默认离线**：第三方库由 `third_party/*.tar.gz` 提供，configure 时自动解压；只有 `third_party/` 缺失第三方压缩包时才需要网络拉取