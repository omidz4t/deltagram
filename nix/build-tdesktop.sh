#!/usr/bin/env bash
# Configure once, then incrementally build Telegram with Ninja + ccache.
# Dependencies come from `nix develop` / the Nix store and are not rebuilt
# when application sources change.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
BUILD_DIR="${CMAKE_BUILD_DIR:-$DELTA_TEL_DATA/tdesktop-out}"
mkdir -p "$BUILD_DIR" "${CCACHE_DIR:-$DELTA_TEL_DATA/ccache}"

if [[ ! -f "$ROOT/tdesktop/cmake/CMakeLists.txt" ]]; then
  echo "tdesktop cmake submodule is empty. Run:"
  echo "  git -C \"$ROOT/tdesktop\" submodule update --init --depth 1"
  exit 1
fi

cmake_args=(
  -S "$ROOT/tdesktop"
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Debug
  -DCMAKE_C_COMPILER_LAUNCHER=ccache
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
  -DDESKTOP_APP_USE_PACKAGED=ON
  -DTDESKTOP_API_TEST=ON
  -DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON
)

if [[ ! -f "$BUILD_DIR/build.ninja" ]]; then
  cmake "${cmake_args[@]}"
fi

python3 "$ROOT/nix/bump-version.py" \
  "$ROOT/tdesktop/Telegram/build/version" \
  "$ROOT/tdesktop/Telegram/SourceFiles/core/version.h"
cmake --build "$BUILD_DIR" --target Telegram -j "${NIX_BUILD_CORES:-$(nproc)}"
echo "Binary: $BUILD_DIR/bin/Telegram"
