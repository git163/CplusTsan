#!/usr/bin/env bash
# 总控集成/压力测试：反复冲击 崩溃恢复 / 变体热切换 / 优雅退出，验证无残留进程。
# 兼容 Linux（一级验证基准）与 macOS（开发冒烟）。不依赖 /proc。
#
# 前置：./scripts/build.sh（必需）+ ./scripts/build-tsan.sh（可选）
#   build-tsan 缺失时自动跳过"变体热切换"相关步骤，其余照常执行。
# 用法：./scripts/stress-controller.sh
# 通过输出 "PASS: stress-controller"；此外可经 ctest 触发（集成用例 stress.controller）。

set -euo pipefail
cd "$(dirname "$0")/.."

die() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

[[ -x build/bin/controller ]] || die "build 缺失：先执行 ./scripts/build.sh"
[[ -x build/bin/heartbeat ]] || die "build 缺失：先执行 ./scripts/build.sh"

TSAN=0
if [[ -x build-tsan/bin/heartbeat ]]; then TSAN=1; else
  printf 'NOTE: build-tsan/bin/heartbeat 不存在，跳过变体热切换步骤。先执行 ./scripts/build-tsan.sh 可获得完整场景。\n'
fi

TMP="$(mktemp -d)"
CTRL=""
# 定位心跳进程：命令行形如 "build/bin/heartbeat --period-ms 200 --instance N"，
# 用 .* 跨过中间参数（/bin/...-f 走 ERE 正则匹配）
hb() { pgrep -f "heartbeat.*--instance $1"; }
cleanup() {
  # 优雅停总控（SIGTERM→其会级联 SIGTERM 给子进程并清场），避免 kill -9 留下孤儿心跳
  [[ -n "$CTRL" ]] && kill -TERM "$CTRL" 2>/dev/null || true
  wait "$CTRL" 2>/dev/null || true
  rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

write_config() { # $1 = normal | tsan（h2 的变体）
  local h2="$1"
  cat > "$TMP/config.json" <<JSON
{
  "watch_interval_ms": 300,
  "normal_bin_dir": "build/bin",
  "tsan_bin_dir": "build-tsan/bin",
  "normal_lib_dir": "build/lib",
  "tsan_lib_dir": "build-tsan/lib",
  "tsan_runner": "tsan/tsan-run.sh",
  "components": [
    { "key": "h1", "name": "heartbeat", "variant": "normal", "args": ["--period-ms", "200", "--instance", "1"] },
    { "key": "h2", "name": "heartbeat", "variant": "$h2",  "args": ["--period-ms", "200", "--instance", "2"] }
  ]
}
JSON
  sleep 1   # 保证两次写配置的 mtime 秒级不同，控制器必然感知重载
}

: > "$TMP/controller.log"
write_config normal
./build/bin/controller --config "$TMP/config.json" >>"$TMP/controller.log" 2>&1 &
CTRL=$!
sleep 3

# —— 0) 初始调度：两个组件都应被拉起，normal 变体 ——
grep -q "h1 started pid" "$TMP/controller.log" || die "h1 未被拉起"
grep -q "h2 started pid" "$TMP/controller.log" || die "h2 未被拉起"

# —— 1) 崩溃恢复：SIGKILL h2，应被自动重启为新进程；h1 不受影响 ——
H2=$(hb 2 | head -1) || true
[[ -n "$H2" ]] || die "找不到 h2 进程"
kill -9 "$H2"
sleep 2
grep -q "h2 killed signal=9" "$TMP/controller.log" || die "未见 h2 被 SIGKILL 的回收记录"
NEW_H2=$(hb 2 | head -1) || true
[[ -n "$NEW_H2" && "$NEW_H2" != "$H2" ]] || die "h2 未被自动重启"
[[ "$(grep -c "h1 started pid" "$TMP/controller.log")" == "1" ]] || die "h1 不应被重启"

# —— 2) 热切换 normal→tsan（仅当 build-tsan 存在）——
if [[ "$TSAN" == "1" ]]; then
  write_config tsan
  sleep 3
  grep -q "h2 exited code=0" "$TMP/controller.log" || die "h2 应优雅退出（SIGTERM 处理）后才被切换"
  grep -q "h2 started pid=.*variant=tsan" "$TMP/controller.log" || die "h2 未切换到 tsan 变体"
  grep -q "core_version=core-v1-tsan" "$TMP/controller.log" || die "tsan 变体未真正加载 tsan 动态库（libcore）"
  hb 2 >/dev/null || die "h2 切 tsan 后进程不在"

  # —— 3) 热切换 tsan→normal（切回）——
  write_config normal
  sleep 3
  grep -q "h2 started pid=.*variant=normal" "$TMP/controller.log" || die "h2 未切回 normal 变体"
  hb 2 >/dev/null || die "h2 切回 normal 后进程不在"
fi

# —— 4) 优雅退出：TERM 后总控应级联停掉所有组件并清理 ——
kill -TERM "$CTRL"
wait "$CTRL" || true
grep -q "all components stopped" "$TMP/controller.log" || die "无优雅退出日志"
pgrep -f "heartbeat" >/dev/null 2>&1 && die "退出后仍有残留 heartbeat 进程" || true

printf 'PASS: stress-controller\n'