#!/usr/bin/env bash
# Serialize the complete build/stage/package sequence on the host.
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
source "$ROOT/nix/paths.sh"
ALL=0
ALLOW_TEST=0
for arg in "$@"; do
  case "$arg" in
    all) ALL=1 ;;
    --allow-test-api) ALLOW_TEST=1 ;;
    *)
      echo 'Usage: nix/release.sh [all] [--allow-test-api]' >&2
      exit 2
      ;;
  esac
done
_release_xtrace=0
case $- in *x*) _release_xtrace=1; set +x ;; esac
if [[ "$ALLOW_TEST" -eq 1 ]]; then
  if [[ "${DELTA_TEL_API_MODE:-test}" == production ]]; then
    echo '--allow-test-api cannot be combined with DELTA_TEL_API_MODE=production.' >&2
    if [[ "$_release_xtrace" -eq 1 ]]; then set -x; fi
    exit 2
  fi
  export DELTA_TEL_API_MODE=test
  export DELTA_TEL_STAGE_ALLOW_TEST_API=1
else
  if [[ "${DELTA_TEL_API_MODE:-}" != production ]]; then
    echo 'nix/release.sh requires DELTA_TEL_API_MODE=production and nonempty TDESKTOP_API_ID/TDESKTOP_API_HASH, or --allow-test-api for validation-only packaging.' >&2
    if [[ "$_release_xtrace" -eq 1 ]]; then set -x; fi
    exit 2
  fi
  if [[ -z "${TDESKTOP_API_ID:-}" || -z "${TDESKTOP_API_HASH:-}" ]]; then
    echo 'production release requires nonempty TDESKTOP_API_ID and TDESKTOP_API_HASH.' >&2
    if [[ "$_release_xtrace" -eq 1 ]]; then set -x; fi
    exit 2
  fi
  export DELTA_TEL_STAGE_ALLOW_TEST_API=0
fi
if [[ "$_release_xtrace" -eq 1 ]]; then set -x; fi
docker info > /dev/null
exec 8>"$DELTA_TEL_DATA/.release-package.lock"
flock 8
DELTA_TEL_JOBS=4 "$ROOT/nix/develop.sh" --command bash -c \
  'bash nix/build-rpc.sh && bash nix/build-release.sh &&
   if [[ "${DELTA_TEL_STAGE_ALLOW_TEST_API}" == 1 ]]; then
     python3 nix/stage-release.py --allow-test-api
   else
     python3 nix/stage-release.py
   fi'
bash "$ROOT/docker/build.sh"
bash "$ROOT/docker/verify.sh"
if [[ "$ALL" -eq 1 ]]; then bash "$ROOT/docker/package-formats.sh"; fi
echo "Single-file executable: $ROOT/dist/deltagram"
