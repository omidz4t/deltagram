#!/usr/bin/env bash
# Run the built Telegram binary on a virtual X display inside the dev shell
# and save screenshots. Usage: ./nix/develop.sh --command bash nix/ui-test.sh
# Output: $DELTA_TEL_DATA/ui-test/shots/*.png
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
OUT="$DELTA_TEL_DATA/ui-test"
BIN="${CMAKE_BUILD_DIR:-$DELTA_TEL_DATA/tdesktop-out}/bin"
DISPLAY_NUM=77

rm -rf "$OUT"
mkdir -p "$OUT/shots" "$OUT/acc" "$OUT/home"
rm -f "/tmp/.X${DISPLAY_NUM}-lock" "/tmp/.X11-unix/X${DISPLAY_NUM}"

Xvfb ":$DISPLAY_NUM" -screen 0 1280x800x24 >"$OUT/xvfb.log" 2>&1 &
sleep 2
export DISPLAY=":$DISPLAY_NUM" QT_QPA_PLATFORM=xcb
export DC_ACCOUNTS_PATH="$OUT/acc"
export DELTA_TEL_RPC="$CARGO_TARGET_DIR/debug/deltachat-rpc-server"
export FONTCONFIG_FILE="$ROOT/dist/delta-tel/telegram-fonts.conf"
export HOME="$OUT/home"

openbox >"$OUT/openbox.log" 2>&1 &
sleep 2
cp "$ROOT/dist/delta-tel/intro1.png" "$BIN/" 2>/dev/null || true

timeout "${UI_TEST_SECONDS:-60}" "$BIN/Telegram" >"$OUT/app.log" 2>&1 &
APP=$!
sleep 15
import -window root "$OUT/shots/welcome.png"
wait "$APP" 2>/dev/null
kill %1 %2 2>/dev/null
echo "Screenshots in $OUT/shots"
