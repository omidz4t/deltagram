#!/usr/bin/env bash
# Run on the host, where Docker's socket is available.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/nix/paths.sh"
test -x "$ROOT/dist/deltagram" || { echo 'Run make release first.' >&2; exit 1; }
mkdir -p "$ROOT/dist/packages"
docker build --tag deltagram-package-builder --file "$ROOT/docker/Packaging.Dockerfile" "$ROOT/docker"
docker run --rm \
  --mount "type=bind,source=$ROOT/dist,target=/input,readonly" \
  --mount "type=bind,source=$ROOT/dist/packages,target=/output" \
  --mount "type=bind,source=$ROOT/tdesktop/LICENSE,target=/licenses/Telegram-LICENSE,readonly" \
  --mount "type=bind,source=$ROOT/tdesktop/LEGAL,target=/licenses/Telegram-LEGAL,readonly" \
  --mount "type=bind,source=$ROOT/context/core/LICENSE,target=/licenses/Core-LICENSE,readonly" \
  --env "SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git -C "$ROOT" log -1 --format=%ct)}" \
  deltagram-package-builder
