# TSan 接入指南（其他 C++ 项目）

> 目标是其他 C++ 项目**几分钟内接入** TSan：拿到编译插桩、运行启动器、抑制模板这套体系，并理解"双构建目录变体 + 总控切换"的工程模型。**Linux 为一级验证基准。**

## 1. 要拿走的文件

从本仓库复制三个件：

```
cmake/tsan.cmake     # TSan 集成模块（核心，可独立复用）
tsan/tsan.supp       # 抑制模板（TSAN_OPTIONS=suppressions=...）
tsan/tsan-run.sh     # TSan 启动器（统一设置 TSAN_OPTIONS 后执行二进制）
```

如需"总控调度 + 变体切换"，再加一个最小总控（参考 `controller/` 与 `src/common/` 的封装：`config_parser`、`process_util`、`variant_resolver`）。

**第三方库离线依赖（可选推荐）**：把依赖的源码以压缩包形式放进 `third_party/` 并提交 git（仓库只存压缩包，不存解压目录），配合 `cmake/vendor.cmake` 在 configure 时自动解压到构建目录——从此构建离线、定版本，不依赖 FetchContent 网络拉取。用法见 `cmake/vendor.cmake` 注释与 `src/CMakeLists.txt` / `tests/CMakeLists.txt` 的示例。

## 2. 顶层接入步骤（CMake）

顶层 `CMakeLists.txt` 在**任何 `add_subdirectory` 之前**引入：

```cmake
include(cmake/tsan.cmake)          # 必须先于子目录，保证全局插桩 flag 生效
```

之后构建两个变体（独立构建目录，因为插桩不可运行时切换）：

```bash
cmake -S . -B build               -DCMAKE_BUILD_TYPE=RelWithDebInfo   # 普通
cmake -S . -B build-tsan -DENABLE_TSAN=ON                             # TSan
cmake --build build-tsan -j
```

`ENABLE_TSAN=ON` 会：强制 build type `TSan`（`-O1 -g`）、把 `-fsanitize=thread -fno-omit-frame-pointer` 追加到**全局**编译/链接 flags、给所有 TU（含 FetchContent 依赖）加 `-DTSAN_BUILD`。

## 3. 铁律：整进程插桩（whole-program）

TSan 要求**进程内所有代码**都是 TSan 变体，否则：

- 未插桩的 .so / 静态库混进 TSan 进程 → **假竞态**或漏报；
- TSan 插桩的 .so 被 `dlopen` 进未插桩进程 → 运行时直接报 "TSan runtime must come first in the library list"。

所以：**不要**只给主程序加 flag；确保 FetchContent、第三方静态库都用同一套 TSan flags 构建（本模块用全局 flags 实现）。集成检查清单：

- [ ] `-fsanitize=thread` 出现在**编译**与**链接**全部 TU 的命令里；
- [ ] 没有把未插桩的静态库链进来；第三方库要么源码编译（会被全局 flags 覆盖），要么单独产出 TSan 版；
- [ ] 不要混用其他 sanitizer（ASan/UBSan 与 TSan 不兼容）。

## 4. 动态库组件与加载路径（策略 A / B / C）

组件若是动态库形态，TSan 变体 = **可执行文件 + 所有 .so + runtime 本身** 都来自同一变体。动态库按 loader 搜索顺序查找：

```
DT_RPATH > LD_LIBRARY_PATH > DT_RUNPATH > ld.so.cache > 默认路径   （glibc）
```

针对"库路径环境变量已固定"的三种策略：

| 策略 | 机制 | 适用 | 代价 |
|---|---|---|---|
| A. 子进程环境注入（默认） | 启动前把 `<变体lib目录>` 前置到子进程 `LD_LIBRARY_PATH`（首个匹配胜出） | 变体库同名不同目录 | 需统一由总控/启动器拉起 |
| B. 不同 soname 共存 | 普通 `libfoo.so` / TSan `libfoo_tsan.so` 放**同一个**库目录，各二进制靠 DT_NEEDED 自选，不读任何 env | 绝对不允许改 env、需手动运行 | 构建要产出双份库，部署带两套 |
| C. 前缀树 + `$ORIGIN` RPATH | 每变体自包含 `prefix/{bin,lib}`，`-Wl,-rpath,'$ORIGIN/../lib'` 配 `--disable-new-dtags` | 无 env 依赖、可整体搬迁 | 使用旧式 RPATH |

### 策略 B 的构建片段（双 soname）

TSan 变体给库换名、让依赖方链接 `_tsan` 版本：

```cmake
# 在 ENABLE_TSAN 时给共享库追加别名目标
if(ENABLE_TSAN)
    set_target_properties(core PROPERTIES OUTPUT_NAME "core_tsan")   # → libcore_tsan.so
endif()
```

这样普通 `libcore.so` 与 TSan `libcore_tsan.so` 可共存于同一目录，`LD_LIBRARY_PATH` 完全冻结也能按 DT_NEEDED 选对。

### 策略 C 的链接参数

```cmake
if(ENABLE_TSAN)   # 只对可执行目标
    target_link_options(app PRIVATE
        "-Wl,-rpath,'$ORIGIN/../lib'"
        "-Wl,--disable-new-dtags")   # 用旧式 RPATH：优先于 LD_LIBRARY_PATH
endif()
```

> 选用前想清楚：**不要"env 前置 + 旧式 RPATH"混用**——DT_RPATH 优先于 env，注入会失效。默认走策略 A（普通二进制是 DT_RUNPATH，env 可覆盖）。

## 5. 运行启动器

手动跑一个 TSan 组件（或让总控的 tsan 变体统一走它）：

```bash
TSAN_LOG_DIR=/var/log/tsan ./tsan/tsan-run.sh ./build-tsan/bin/myproc --flag
```

启动器会设置：
- `halt_on_error=0` —— 服务进程遇首个竞态**不退出**（TSan 默认直接退出）；
- `log_path=<dir>/tsan` —— 报告写到 `<dir>/tsan.<pid>`，不污染业务日志；
- `suppressions=<dir>/tsan.supp` —— 抑制模板自动生效；
- `second_deadlock_stack=1`。

## 6. 常见坑

- **`ulimit -v` / 容器内存上限**：TSan shadow 映射占用巨大虚拟地址空间，受限时启动报 "FATAL: ThreadSanitizer: memory allocation failed" → 放宽 `ulimit -v`。
- **macOS**：TSan runtime 稳定性差，仅作开发冒烟，结果以 Linux 为准。
- **抑制是只"降噪音"不"降开销"**：大量 suppression 不会让 TSan 变快，性能问题走 `docs/tsan-performance-impact.md`。
- **`-O3` 复现性差**：本模块 TSan build type 用 `-O1 -g`，别手工改成 `-O3`。
- **离线构建**：FetchContent 拉 gtest/json 需要网络；离线环境改用系统包或预置源码。