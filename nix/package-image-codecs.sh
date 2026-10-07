#!/usr/bin/env bash
# Run in the development shell. Preserve the existing portable runtime.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/dist/delta-tel"
QT_PLUGINS="$(pkg-config --variable=libdir Qt6Core)/qt-6/plugins"
mkdir -p "$DEST/telegram-plugins/imageformats" "$DEST/telegram-lib"
for name in jpeg gif ico; do
  source="$QT_PLUGINS/imageformats/libq${name}.so"
  target="$DEST/telegram-plugins/imageformats/libq${name}.so"
  cp -L "$source" "$target"
  # Existing libraries belong to the bundled Qt runtime. Only add missing deps.
  while read -r dependency; do
    [[ -f "$dependency" ]] || continue
    # The launcher deliberately uses host graphics libraries.
    case "$(basename "$dependency")" in libGL*|libEGL*|libOpenGL*) continue ;; esac
    output="$DEST/telegram-lib/$(basename "$dependency")"
    if [[ ! -e "$output" ]]; then
      cp -L "$dependency" "$output"
      patchelf --set-rpath '$ORIGIN' "$output"
    fi
  done < <(ldd "$source" | awk '/=> \// { print $3 }')
  patchelf --set-rpath '$ORIGIN/../../telegram-lib' "$target"
done
