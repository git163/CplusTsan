#!/usr/bin/env bash
# tsan-run.sh — TSan 变体统一启动器
# 唯一职责：设置 TSAN_OPTIONS 后，把后续参数（二进制 + args）原样转发执行。
# 总控的 tsan 变体（以及手动执行）都经由它，保证 TSan 运行参数单一来源。
#
# 用法：
#   ./tsan/tsan-run.sh <binary> [args...]
#   TSAN_LOG_DIR=/var/log/tsan ./tsan/tsan-run.sh <binary> [args...]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 报告落盘目录：默认 /tmp/tsan，可用 TSAN_LOG_DIR 覆盖
TSAN_LOG_DIR="${TSAN_LOG_DIR:-/tmp/tsan}"
mkdir -p "$TSAN_LOG_DIR"

# halt_on_error=0: 服务进程遇到首个竞态不要退出（TSan 默认是直接退出），记录后继续跑
# log_path=...:     报告写到 <log_path>.<pid>，避免与业务日志混杂
# suppressions=...: 使用本仓库抑制模板
# second_deadlock_stack=1: 死锁报告带上第二帧栈，便于定位
export TSAN_OPTIONS="halt_on_error=0:log_path=${TSAN_LOG_DIR}/tsan:suppressions=${SCRIPT_DIR}/tsan.supp:second_deadlock_stack=1${TSAN_OPTIONS:+:$TSAN_OPTIONS}"

exec "$@"