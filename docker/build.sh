#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/nix/paths.sh"
TASK_ENV=$(mktemp "$TMPDIR/deltagram-toolchain.XXXXXX")
trap 'rm -f "$TASK_ENV"' EXIT
"$ROOT/nix/develop.sh" --command bash -c 'printf "%s\n%s\n%s\n" "$(command -v cc)" "$PATH" "$(pkg-config --variable=libdir Qt6Core)/qt-6/plugins" > "$1"' bash "$TASK_ENV"
mapfile -t TOOLCHAIN < "$TASK_ENV"
export DELTA_CONTAINER_CC="${TOOLCHAIN[0]}"
export DELTA_CONTAINER_PATH="${TOOLCHAIN[1]}"
export DELTA_QT_PLUGIN_ROOT="${TOOLCHAIN[2]}"
# Docker's socket is on the host /run, outside the Nix mount namespace.
bash "$ROOT/docker/run-build.sh" "$@"
