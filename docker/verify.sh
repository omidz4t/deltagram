#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
source "$ROOT/nix/paths.sh"
OUTPUT=${1:-$ROOT/dist}
DEPS=${DELTA_CONTAINER_BUILD_DIR:-$DELTA_TEL_DATA/container-build}/debian
SCREENS=$(mktemp -d "$DELTA_TEL_DATA/verification.XXXXXX")
# Test dependencies provide a virtual display and test interpreter only.
# The application itself must use the runtime embedded in its single file.
# Allow WebKit to create its own sandbox namespaces and install its seccomp
# policy. The root test user also needs SYS_ADMIN and NET_ADMIN for bubblewrap's mount/network
# namespaces. Ubuntu's Docker AppArmor profile also blocks their mounts.
# These permissions apply only to this offline test container.
timeout 300 docker run --rm --security-opt seccomp=unconfined --security-opt apparmor=unconfined --cap-add SYS_ADMIN --cap-add NET_ADMIN --ulimit core=0 --network none --tmpfs /tmp:rw,exec,size=1900m \
  --mount "type=bind,source=$OUTPUT,target=/output,readonly" \
  --mount "type=bind,source=$SCREENS,target=/screens" \
  --mount "type=bind,source=$DEPS,target=/test-deps,readonly" \
  --mount "type=bind,source=$ROOT/docker/verify-webview.py,target=/verify-webview.py,readonly" \
  --mount "type=bind,source=$ROOT/docker/verify-window.py,target=/verify-window.py,readonly" \
  --mount "type=bind,source=$ROOT/docker/verify-rpc.py,target=/verify-rpc.py,readonly" \
  --mount "type=bind,source=$ROOT/docker/verify-container.sh,target=/verify-container.sh,readonly" \
  --env "GUI_ONLY=${DELTA_TEST_GUI_ONLY:-0}" \
  --env "BUNDLE=/output/deltagram" \
  "${DELTA_TEST_IMAGE:-debian:13-slim}" bash /verify-container.sh
