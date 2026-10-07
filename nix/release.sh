#!/usr/bin/env bash
# Serialize the complete build/stage/package sequence on the host.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/nix/paths.sh"
if [[ $# -gt 1 || ( $# -eq 1 && "$1" != all ) ]]; then
  echo 'Usage: nix/release.sh [all]' >&2
  exit 2
fi
docker info > /dev/null
exec 8>"$DELTA_TEL_DATA/.release-package.lock"
flock 8
DELTA_TEL_JOBS=4 "$ROOT/nix/develop.sh" --command bash -c \
  'bash nix/build-rpc.sh && bash nix/build-release.sh && python3 nix/stage-release.py'
bash "$ROOT/docker/build.sh"
bash "$ROOT/docker/verify.sh"
if [[ "${1:-}" == all ]]; then bash "$ROOT/docker/package-formats.sh"; fi
echo "Single-file executable: $ROOT/dist/deltagram"
