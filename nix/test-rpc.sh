#!/usr/bin/env bash
# Exercise the real Core server through the Qt JSON-RPC transport, without mail IO.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"
cmake -S "$ROOT/tdesktop/Telegram/SourceFiles/delta" \
  -B "$DELTA_TEL_DATA/delta-shell" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$DELTA_TEL_DATA/delta-shell" -j "$(nproc)"
TEST_DIR=$(mktemp -d "$TMPDIR/rpc-smoke.XXXXXX")
trap 'rm -rf -- "$TEST_DIR"' EXIT
mkdir -m 700 "$TEST_DIR/runtime"
QT_QPA_PLATFORM=offscreen XDG_RUNTIME_DIR="$TEST_DIR/runtime" \
  timeout 30 "$DELTA_TEL_DATA/delta-shell/delta_shell" \
  --rpc "$CARGO_TARGET_DIR/debug/deltachat-rpc-server" \
  --accounts "$TEST_DIR/accounts" --self-test
