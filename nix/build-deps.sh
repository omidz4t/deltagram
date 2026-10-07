#!/usr/bin/env bash
# Build only the dependency closure; keep the application out of Nix snapshots.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
TOOLCHAIN=$(mktemp -d "${TMPDIR:-/tmp}/delta-tel-deps.XXXXXX")
trap 'rm -rf -- "$TOOLCHAIN"' EXIT
cp "$ROOT/flake.nix" "$ROOT/flake.lock" "$TOOLCHAIN/"
"$ROOT/nix/enter.sh" nix build "path:$TOOLCHAIN#deps" --no-link \
  --extra-experimental-features 'nix-command flakes'
