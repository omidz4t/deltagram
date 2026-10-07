#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/nix/paths.sh"
OUTPUT=${1:-$ROOT/dist}
mkdir -p "$OUTPUT"
OUTPUT=$(cd "$OUTPUT" && pwd)
WORK=${DELTA_CONTAINER_BUILD_DIR:-$DELTA_TEL_DATA/container-build}
mkdir -p "$WORK"
WORK=$(cd "$WORK" && pwd)
RUNTIME=$(mktemp -d "$TMPDIR/deltagram-runtime.XXXXXX")
STORE="$DELTA_TEL_NIX_ROOT/store"
if [[ "${DELTA_TEL_USE_SYSTEM_NIX:-0}" == 1 ]]; then STORE=/nix/store; fi
trap 'rm -f "$RUNTIME/runtime-x86_64"; rmdir "$RUNTIME"' EXIT
curl -fsSL https://github.com/AppImage/AppImageKit/releases/download/13/obsolete-runtime-x86_64 \
  -o "$RUNTIME/runtime-x86_64"
echo "328e0d745c5c6817048c27bc3e8314871703f8f47ffa81a37cb06cd95a94b323  $RUNTIME/runtime-x86_64" | sha256sum -c -
docker build --tag deltagram-debian13-builder --file "$ROOT/docker/Dockerfile" "$ROOT/docker"
docker run --rm \
  --mount "type=bind,source=$WORK,target=/build" \
  --mount "type=bind,source=$DELTA_TEL_DATA/release-stage,target=/input,readonly" \
  --mount "type=bind,source=$STORE,target=/nix/store,readonly" \
  --mount "type=bind,source=$RUNTIME,target=/runtime,readonly" \
  --mount "type=bind,source=$OUTPUT,target=/output" \
  --env "CC=$DELTA_CONTAINER_CC" --env "PATH=$DELTA_CONTAINER_PATH" \
  --env "DELTA_QT_PLUGIN_ROOT=$DELTA_QT_PLUGIN_ROOT" \
  deltagram-debian13-builder
