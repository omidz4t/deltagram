#!/usr/bin/env bash
# Install single-user Nix into data/nix (mounted as /nix).
set -euo pipefail
export NIX_CONFIG="${NIX_CONFIG:-}
build-users-group =
require-drop-supplementary-groups = false
sandbox = false"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
if [[ "${DELTA_TEL_USE_SYSTEM_NIX:-0}" == 1 ]]; then
  nix --version
  exit 0
fi
NIX_ROOT="${DELTA_TEL_NIX_ROOT:-$DELTA_TEL_DATA/nix}"
mkdir -p "$NIX_ROOT"

if "$ROOT/nix/enter.sh" test -x /nix/var/nix/profiles/default/bin/nix; then
  echo "Nix is already installed in $NIX_ROOT"
  exit 0
fi

VER=2.31.2
mkdir -p "$DELTA_TEL_DATA/downloads"
TAR="$DELTA_TEL_DATA/downloads/nix-${VER}-x86_64-linux.tar.xz"
URL="https://releases.nixos.org/nix/nix-${VER}/nix-${VER}-x86_64-linux.tar.xz"
if [[ ! -f "$TAR" ]]; then
  curl -fL --retry 3 -o "$TAR" "$URL"
fi

WORKDIR=$(mktemp -d "$DELTA_TEL_DATA/downloads/nix-unpack.XXXXXX")
trap 'rm -rf -- "$WORKDIR"; rm -f -- "$TAR"' EXIT
tar -C "$WORKDIR" -xf "$TAR"
# The upstream installer expects to be root and writes /nix. Our enter.sh
# provides that view.
"$ROOT/nix/enter.sh" bash -lc "cd $(printf %q "$WORKDIR/nix-${VER}-x86_64-linux") && ./install --no-daemon"
echo "Installed Nix into $NIX_ROOT"
