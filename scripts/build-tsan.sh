#!/usr/bin/env bash
# 构建 TSan 变体（build-tsan/，全局插桩，-O1 -g）
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build-tsan -DENABLE_TSAN=ON
cmake --build build-tsan -j "$@"