#!/usr/bin/env bash
# 构建普通变体（build/，RelWithDebInfo）
set -euo pipefail
cd "$(dirname "$0")/.."
cmake -S . -B build
cmake --build build -j "$@"