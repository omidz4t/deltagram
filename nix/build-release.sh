#!/usr/bin/env bash
# Size-optimised, stripped build of the Delta Chat client.
# Usage: ./nix/develop.sh --command bash nix/build-release.sh
# Output: $RELEASE_DIR/bin/Telegram and the stripped copy $RELEASE_DIR/bin/deltagram.stripped
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
source "$ROOT/nix/telegram-api-config.sh"
RELEASE_DIR="${RELEASE_DIR:-$DELTA_TEL_DATA/tdesktop-release}"
JOBS="${DELTA_TEL_JOBS:-4}"
mkdir -p "$RELEASE_DIR" "${CCACHE_DIR:-$DELTA_TEL_DATA/ccache}"
exec 9>"$RELEASE_DIR/.build.lock"
flock 9

delta_tel_init_telegram_api "$RELEASE_DIR"
delta_tel_cmake_configure "$RELEASE_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
  -DCMAKE_C_FLAGS_RELEASE="-Os -DNDEBUG -ffunction-sections -fdata-sections" \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -Wl,-O1" \
  -DDESKTOP_APP_USE_PACKAGED=ON \
  -DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON

# Release preparation owns version changes; compilation is repeatable.
VERSION=$(awk '$1 == "AppVersionStr" { print $2 }' "$ROOT/tdesktop/Telegram/build/version")
echo "Building Delta Tel $VERSION"
# Compile the backend boundary first so integration errors surface before the
# donor's large source set. The full build reuses this object and its prerequisites.
cmake --build "$RELEASE_DIR" \
  --target Telegram/CMakeFiles/Telegram.dir/SourceFiles/delta/delta_bridge.cpp.o \
  -j "$JOBS"
cmake --build "$RELEASE_DIR" --target Telegram -j "$JOBS"
strip --strip-all -o "$RELEASE_DIR/bin/deltagram.stripped" "$RELEASE_DIR/bin/Telegram"
printf '%s\n' "$VERSION" > "$RELEASE_DIR/bin/deltagram.version"
python3 "$ROOT/nix/build-provenance.py" write \
  --release-dir "$RELEASE_DIR" \
  --mode "$DELTA_TEL_TELEGRAM_API_MODE"
ls -la "$RELEASE_DIR/bin/Telegram" "$RELEASE_DIR/bin/deltagram.stripped"

if [[ "${DELTA_TEL_PACK_UPX:-0}" == 1 ]] && command -v upx >/dev/null; then
  cp "$RELEASE_DIR/bin/deltagram.stripped" "$RELEASE_DIR/bin/deltagram.packed"
  upx --best --lzma -q "$RELEASE_DIR/bin/deltagram.packed"
  ls -la "$RELEASE_DIR/bin/deltagram.packed"
fi
