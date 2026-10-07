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
# Exercise WebKit's sandboxed session-bus proxy without host executables.
cat > /tmp/session.conf <<'DBUS'
<busconfig><type>session</type><listen>unix:path=/tmp/session-bus</listen>
<policy context="default"><allow send_destination="*"/><allow own="*"/><allow receive_sender="*"/></policy>
</busconfig>
DBUS
"$DEPS/ld-linux-x86-64.so.2" --library-path "$DEPS" \
 /test-deps/usr/bin/dbus-daemon --config-file=/tmp/session.conf --nofork \
 > /tmp/dbus.log 2>&1 &
DBUS_PID=$!
export DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/session-bus
sleep 1
kill -0 "$DBUS_PID" || { cat /tmp/dbus.log; exit 1; }
HOME=/tmp/home XDG_RUNTIME_DIR=/tmp/run DISPLAY=:99 LIBGL_DRIVERS_PATH=/missing-drivers GBM_BACKENDS_PATH=/missing-gbm \
 "$BUNDLE" --bundle-webview-test 'data:text/html,<canvas id=c width=100 height=100 style="transform:rotate(10deg)"></canvas><script>let x=c.getContext("2d");let n=0;function draw(){x.fillStyle="red";x.fillRect(0,0,100,100);if(++n<30){requestAnimationFrame(draw)}else if(x.getImageData(5,5,1,1).data[0]===255){document.title="Deltagram WebView JavaScript passed"}}requestAnimationFrame(draw)</script>' > /tmp/webview.log 2>&1 &
WEBVIEW_PID=$!
if ! DISPLAY=:99 python /verify-webview.py; then
  cat /tmp/webview.log
  exit 1
fi
if grep -Eq "EGL_BAD_PARAMETER|Failed to create GBM|Web process became unresponsive|GLib-GIO-CRITICAL" /tmp/webview.log; then
  cat /tmp/webview.log
  exit 1
fi
kill "$WEBVIEW_PID"
wait "$WEBVIEW_PID" || test "$?" -eq 143
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
if grep -Eq 'Fontconfig warning|error: empty dic file|Hash Manager Error|App info not found|Failed to execute child process.*update-desktop-database' /tmp/gui.log; then exit 1; fi
HOME=/tmp/home python - <<'DESKTOP'
import ctypes
from pathlib import Path
assert (Path.home() / '.local/share/applications/org.deltagram.desktop.desktop').is_file()
gio = ctypes.CDLL('libgio-2.0.so.0')
gio.g_desktop_app_info_new.argtypes = [ctypes.c_char_p]
gio.g_desktop_app_info_new.restype = ctypes.c_void_p
info = gio.g_desktop_app_info_new(b'org.deltagram.desktop.desktop')
assert info, 'Deltagram desktop entry cannot be resolved by GIO'
gio.g_object_unref.argtypes = [ctypes.c_void_p]
gio.g_object_unref(info)
DESKTOP
kill "$APP_PID"
wait "$APP_PID" || test "$?" -eq 143
printf 'WebKit JavaScript, GUI startup and keyboard input checks passed.\n'
