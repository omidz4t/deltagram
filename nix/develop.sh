#!/usr/bin/env bash
# Enter the dev shell. Toolchain and libraries come from the Nix store.
# CMake, Ninja, ccache, and Cargo keep their outputs under data,
# so an edit rebuilds only the objects that changed.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
# The shell depends only on these two files. Passing the project root to Nix
# snapshots the application sources and outputs into the store on every edit.
TOOLCHAIN=$(mktemp -d "${TMPDIR:-/tmp}/delta-tel-toolchain.XXXXXX")
trap 'rm -rf -- "$TOOLCHAIN"' EXIT
cp "$ROOT/flake.nix" "$ROOT/flake.lock" "$TOOLCHAIN/"
"$ROOT/nix/enter.sh" nix develop "path:$TOOLCHAIN#${DELTA_TEL_SHELL:-default}" --option sandbox false --extra-experimental-features 'nix-command flakes' "$@"
