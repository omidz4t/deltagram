#!/usr/bin/env bash
set -euo pipefail
test ! -e /nix
test ! -e /usr/lib/x86_64-linux-gnu/libQt6Core.so.6
test ! -e /usr/lib/x86_64-linux-gnu/libgtk-4.so.1
DEPS=/test-deps/usr/lib/x86_64-linux-gnu
python() {
  PYTHONHOME=/test-deps/usr "$DEPS/ld-linux-x86-64.so.2" --library-path "$DEPS" \
    /test-deps/usr/bin/python3 "$@"
}
if [[ "$GUI_ONLY" != 1 ]]; then
  VERSION=$("$BUNDLE" --bundle-version)
  test "$VERSION" = "$(cat /output/deltagram.version)"
  echo "$VERSION"
  python /verify-rpc.py
fi
cat > /usr/bin/xkbcomp <<'KEYBOARD'
#!/bin/sh
exec /test-deps/usr/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 \
 --library-path /test-deps/usr/lib/x86_64-linux-gnu /test-deps/usr/bin/xkbcomp "$@"
KEYBOARD
chmod +x /usr/bin/xkbcomp
"$DEPS/ld-linux-x86-64.so.2" --library-path "$DEPS" /test-deps/usr/bin/Xvfb \
  :99 -xkbdir /test-deps/usr/share/X11/xkb -screen 0 1100x850x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &
DISPLAY_PID=$!
trap 'kill "$DISPLAY_PID" 2>/dev/null || true' EXIT
sleep 2
kill -0 "$DISPLAY_PID" || { cat /tmp/xvfb.log; exit 1; }
mkdir -p /tmp/home /tmp/run
chmod 700 /tmp/run
FONTCONFIG_PATH=/test-deps/etc/fonts FONTCONFIG_FILE=/missing-host-fonts.conf \
 XKB_CONFIG_ROOT=/missing-host-keyboard-root XLOCALEDIR=/missing-host-locale \
 HOME=/tmp/home XDG_RUNTIME_DIR=/tmp/run DISPLAY=:99 \
 "$BUNDLE" -workdir /tmp/work > /tmp/gui.log 2>&1 &
APP_PID=$!
sleep 25
if ! kill -0 "$APP_PID"; then
  wait "$APP_PID" || echo "Application exit status: $?"
  cat /tmp/gui.log
  find /tmp/home /tmp/work -name 'log*.txt' -exec tail -60 {} \; 2>/dev/null || true
  exit 1
fi
cat /tmp/gui.log
if ! DISPLAY=:99 python /verify-window.py; then
  cat /tmp/gui.log
  exit 1
fi
if grep -q 'Fontconfig warning' /tmp/gui.log; then exit 1; fi
kill "$APP_PID"
wait "$APP_PID" || test "$?" -eq 143
printf 'GUI startup, keyboard input and Core RPC checks passed.\n'
