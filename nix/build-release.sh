#!/usr/bin/env bash
# Size-optimised, stripped build of the Delta Chat client.
# Usage: ./nix/develop.sh --command bash nix/build-release.sh
# Output: $RELEASE_DIR/bin/Telegram and the stripped copy $RELEASE_DIR/bin/Telegram.stripped
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
RELEASE_DIR="${RELEASE_DIR:-$DELTA_TEL_DATA/tdesktop-release}"
JOBS="${DELTA_TEL_JOBS:-4}"
mkdir -p "$RELEASE_DIR" "${CCACHE_DIR:-$DELTA_TEL_DATA/ccache}"
exec 9>"$RELEASE_DIR/.build.lock"
flock 9

if [[ ! -f "$RELEASE_DIR/build.ninja" ]]; then
  cmake \
    -S "$ROOT/tdesktop" \
    -B "$RELEASE_DIR" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
    -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -Wl,-O1" \
    -DDESKTOP_APP_USE_PACKAGED=ON \
    -DTDESKTOP_API_TEST=ON \
    -DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
    -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON
fi

VERSION=$(python3 "$ROOT/nix/bump-version.py" \
  "$ROOT/tdesktop/Telegram/build/version" \
  "$ROOT/tdesktop/Telegram/SourceFiles/core/version.h")
echo "Building Delta Tel $VERSION"
cmake --build "$RELEASE_DIR" --target Telegram -j "$JOBS"
strip --strip-all -o "$RELEASE_DIR/bin/Telegram.stripped" "$RELEASE_DIR/bin/Telegram"
printf '%s\n' "$VERSION" > "$RELEASE_DIR/bin/Telegram.version"
ls -la "$RELEASE_DIR/bin/Telegram" "$RELEASE_DIR/bin/Telegram.stripped"

if [[ "${DELTA_TEL_PACK_UPX:-0}" == 1 ]] && command -v upx >/dev/null; then
  cp "$RELEASE_DIR/bin/Telegram.stripped" "$RELEASE_DIR/bin/Telegram.packed"
  upx --best --lzma -q "$RELEASE_DIR/bin/Telegram.packed"
  ls -la "$RELEASE_DIR/bin/Telegram.packed"
fi
