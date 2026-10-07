#!/usr/bin/env bash
# Build Core with the same locked Nix toolchain as the Qt client.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
if [[ ! -f "$ROOT/context/core/Cargo.toml" ]]; then
  echo "Core sources are missing. Run make init first." >&2
  exit 1
fi
cd "$ROOT/context/core"
export OPENSSL_NO_VENDOR=1
export CMAKE_BUILD_PARALLEL_LEVEL="$(nproc)"
cargo build --locked -j "$(nproc)" -p deltachat-rpc-server
