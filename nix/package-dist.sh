#!/usr/bin/env bash
# Lay down dist/delta-tel so it runs without /nix mounted.
# The Qt app and the RPC server use different glibc builds, so each gets
# its own lib directory and its own dynamic loader.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
exec "$ROOT/nix/enter.sh" bash -s -- "$ROOT" <<'EOS'
set -euo pipefail
ROOT=$1
SHELL_BIN="$DELTA_TEL_DATA/delta-shell/delta_shell"
RPC_BIN="$CARGO_TARGET_DIR/debug/deltachat-rpc-server"
QT_PLUGINS=/nix/store/kcvgd6391r93919gb7npj83vycy4v0ni-qtbase-6.9.3/lib/qt-6/plugins
DEST="$ROOT/dist/delta-tel"
rm -rf "$DEST"
mkdir -p "$DEST/shell/bin" "$DEST/shell/lib" "$DEST/shell/plugins/platforms" \
  "$DEST/rpc/bin" "$DEST/rpc/lib"

copy_deps() {
  local bin=$1 dest=$2
  ldd "$bin" | awk '/=> \// {print $3} /^\// {print $1}' | while read -r lib; do
    [[ -f "$lib" ]] || continue
    cp -L --remove-destination "$lib" "$dest/" 2>/dev/null || cp -L "$lib" "$dest/"
  done
}

cp -L "$SHELL_BIN" "$DEST/shell/bin/delta_shell"
cp -L "$RPC_BIN" "$DEST/rpc/bin/deltachat-rpc-server"
copy_deps "$SHELL_BIN" "$DEST/shell/lib"
copy_deps "$RPC_BIN" "$DEST/rpc/lib"

# X11 platform plugin and anything it loads.
cp -L "$QT_PLUGINS/platforms/libqxcb.so" "$DEST/shell/plugins/platforms/"
cp -L "$QT_PLUGINS/platforms/libqoffscreen.so" "$DEST/shell/plugins/platforms/" || true
copy_deps "$QT_PLUGINS/platforms/libqxcb.so" "$DEST/shell/lib"
if [[ -d "$QT_PLUGINS/platformthemes" ]]; then
  mkdir -p "$DEST/shell/plugins/platformthemes"
  cp -L "$QT_PLUGINS/platformthemes/"*.so "$DEST/shell/plugins/platformthemes/" 2>/dev/null || true
fi
if [[ -d "$QT_PLUGINS/xcbglintegrations" ]]; then
  mkdir -p "$DEST/shell/plugins/xcbglintegrations"
  cp -L "$QT_PLUGINS/xcbglintegrations/"*.so "$DEST/shell/plugins/xcbglintegrations/"
  for so in "$DEST/shell/plugins/xcbglintegrations/"*.so; do
    copy_deps "$so" "$DEST/shell/lib"
  done
fi

PATCHELF=/nix/store/5wf9wpdkxs30811kfgkicn9i3nz9jhsh-patchelf-0.15.0/bin/patchelf
SHELL_LD=$(ldd "$SHELL_BIN" | awk '/ld-linux/ {print $1; exit}')
RPC_LD=$(ldd "$RPC_BIN" | awk '/ld-linux/ {print $1; exit}')
cp -L "$SHELL_LD" "$DEST/shell/lib/ld-linux-x86-64.so.2"
cp -L "$RPC_LD" "$DEST/rpc/lib/ld-linux-x86-64.so.2"

# Point both programs at the loader and libs that sit next to them.
"$PATCHELF" --set-interpreter "$DEST/shell/lib/ld-linux-x86-64.so.2" \
  --force-rpath --set-rpath '$ORIGIN/../lib' "$DEST/shell/bin/delta_shell"
"$PATCHELF" --set-interpreter "$DEST/rpc/lib/ld-linux-x86-64.so.2" \
  --force-rpath --set-rpath '$ORIGIN/../lib' "$DEST/rpc/bin/deltachat-rpc-server"
for so in "$DEST/shell/plugins/"*/*.so; do
  [[ -f "$so" ]] || continue
  "$PATCHELF" --force-rpath --set-rpath '$ORIGIN/../../lib:$ORIGIN' "$so" || true
done
for so in "$DEST/shell/lib/"*.so* "$DEST/rpc/lib/"*.so*; do
  [[ -f "$so" ]] || continue
  case "$so" in *ld-linux*) continue ;; esac
  "$PATCHELF" --force-rpath --set-rpath '$ORIGIN' "$so" || true
done

# Host fonts. The Nix fontconfig has no config file outside the store.
cat > "$DEST/shell/fonts.conf" <<'XML'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <dir>/usr/local/share/fonts</dir>
  <cachedir>/tmp/delta-tel-fontconfig</cachedir>
</fontconfig>
XML

cat > "$DEST/delta-shell" <<'LAUNCH'
#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export FONTCONFIG_FILE="$HERE/shell/fonts.conf"
export QT_PLUGIN_PATH="$HERE/shell/plugins"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
ACCOUNTS="${DELTA_ACCOUNTS:-$HOME/.local/share/delta-tel/accounts}"
mkdir -p "$ACCOUNTS"
export LD_LIBRARY_PATH="$HERE/shell/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$HERE/shell/bin/delta_shell" \
  --rpc "$HERE/rpc/bin/deltachat-rpc-server" \
  --accounts "$ACCOUNTS" \
  "$@"
LAUNCH
chmod +x "$DEST/delta-shell"
echo "PACKED $DEST"
EOS
