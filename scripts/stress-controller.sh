#!/usr/bin/env bash
# 总控集成压力测试：反复冲击 崩溃恢复 / 变体热切换 / 优雅退出，验证无残留进程。
# 兼容 Linux（一级验证基准）与 macOS（开发冒烟）。不依赖 /proc。
#
# 前置：./scripts/build.sh 与 ./scripts/build-tsan.sh 均已执行。
# 用法：./scripts/stress-controller.sh
# 通过输出 "PASS: stress-controller"。

set -euo pipefail
cd "$(dirname "$0")/.."

die() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

[[ -x build/bin/controller ]] || die "build 缺失：先执行 ./scripts/build.sh"
[[ -x build/bin/heartbeat ]] || die "build 缺失：先执行 ./scripts/build.sh"
[[ -x build-tsan/bin/heartbeat ]] || die "build-tsan 缺失：先执行 ./scripts/build-tsan.sh"

TMP="$(mktemp -d)"
CTRL=""
cleanup() {
  [[ -n "$CTRL" ]] && kill -9 "$CTRL" 2>/dev/null || true
  wait "$CTRL" 2>/dev/null || true
  rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

write_config() { # $1 = variant_normal | variant_tsan
  local variant="$1"
  local h2
  if [[ "$variant" == "variant_normal" ]]; then h2="normal"; else h2="tsan"; fi
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
  sleep 1   # 保证两次写配置的 mtime 秒级不同，控制器必然感知到重载
}

: > "$TMP/controller.log"
write_config variant_normal
./build/bin/controller --config "$TMP/config.json" >>"$TMP/controller.log" 2>&1 &
CTRL=$!

# —— 0) 初始调度：两个组件都应被拉起 ——
sleep 3
grep -q "h1 started pid" "$TMP/controller.log" || die "h1 未被拉起"
grep -q "h2 started pid" "$TMP/controller.log" || die "h2 未被拉起"

# —— 1) 崩溃恢复：SIGKILL h2，应被自动重启为新进程 ——
H2=$(pgrep -f "heartbeat --instance 2" | head -1) || true
[[ -n "$H2" ]] || die "找不到 h2 进程"
kill -9 "$H2"
sleep 2
grep -q "h2 killed signal=9" "$TMP/controller.log" || die "未见 h2 被 SIGKILL 的回收记录"
NEW_H2=$(pgrep -f "heartbeat --instance 2" | head -1) || true
[[ -n "$NEW_H2" && "$NEW_H2" != "$H2" ]] || die "h2 未被自动重启"

# —— 2) 热切换 normal→tsan：编辑配置改 h2 变体，只重启 h2，h1 不受影响 ——
write_config variant_tsan
sleep 3
grep -q "h2 started pid=.*variant=tsan" "$TMP/controller.log" || die "h2 未切换到 tsan 变体"
grep -c "h1 started pid" "$TMP/controller.log" | grep -qE "^1$" || die "h1 不应被重启"

# —— 3) 热切换 tsan→normal（切回）——
write_config variant_normal
sleep 3
grep -q "h2 started pid=.*variant=normal" "$TMP/controller.log" || die "h2 未切回 normal 变体"

# —— 4) 优雅退出：TERM 后总控应级联停掉所有组件并清理 ——
kill -TERM "$CTRL"
wait "$CTRL" || true
grep -q "all components stopped" "$TMP/controller.log" || die "无优雅退出日志"
pgrep -f "heartbeat --instance" >/dev/null 2>&1 && die "退出后仍有残留 heartbeat 进程" || true

printf 'PASS: stress-controller\n'