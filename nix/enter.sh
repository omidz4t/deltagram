#!/usr/bin/env bash
# Run a command with this repo's Nix store mounted at /nix.
# The store lives under data/nix because creating /nix
# needs root, which this machine does not grant to the user.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/paths.sh"
NIX_ROOT="${DELTA_TEL_NIX_ROOT:-$DELTA_TEL_DATA/nix}"
if [[ "${DELTA_TEL_USE_SYSTEM_NIX:-0}" == 1 ]]; then
  cd "$ROOT"
  exec "$@"
fi
export NIX_CONFIG="${NIX_CONFIG:-}
build-users-group =
require-drop-supplementary-groups = false
sandbox = false"
mkdir -p "$NIX_ROOT"

if [[ $# -eq 0 ]]; then
  set -- bash -l
fi

exec unshare --user --map-root-user --mount --pid --fork --mount-proc bash -s -- "$NIX_ROOT" "$ROOT" "$@" <<'EOS'
set -euo pipefail
NIX_ROOT=$1
ROOT=$2
shift 2
NEW=$(mktemp -d /tmp/delta-tel-nixroot.XXXXXX)
mount -t tmpfs tmpfs "$NEW"
for d in bin boot etc home lib lib64 opt root sbin srv usr; do
  if [[ -d "/$d" ]]; then
    mkdir -p "$NEW/$d"
    mount --bind "/$d" "$NEW/$d"
  fi
done
mkdir -p "$NEW/var" "$NEW/nix" "$NEW/tmp" "$NEW/dev" "$NEW/run" "$NEW/proc"
mount --bind "$NIX_ROOT" "$NEW/nix"
mount --bind /tmp "$NEW/tmp"
mount --bind /proc "$NEW/proc"
mount -t tmpfs tmpfs "$NEW/run"
for n in null zero random urandom tty; do
  touch "$NEW/dev/$n"
  mount --bind "/dev/$n" "$NEW/dev/$n"
done
mkdir -p "$NEW/dev/pts" "$NEW/dev/shm"
# A fresh devpts instance is required. Binding the host /dev/pts fails
# posix_openpt inside this user namespace ("opening pseudoterminal master").
mount -t devpts devpts "$NEW/dev/pts" -o newinstance,ptmxmode=0666,mode=0620
ln -s pts/ptmx "$NEW/dev/ptmx"
ln -s /proc/self/fd "$NEW/dev/fd"
ln -s /proc/self/fd/0 "$NEW/dev/stdin"
ln -s /proc/self/fd/1 "$NEW/dev/stdout"
ln -s /proc/self/fd/2 "$NEW/dev/stderr"
mount --bind /dev/shm "$NEW/dev/shm" || true
cmd=$(printf "%q " "$@")
exec chroot "$NEW" /usr/bin/env \
  HOME="$HOME" \
  USER="${USER:-user}" \
  LOGNAME="${USER:-user}" \
  PATH="$HOME/.nix-profile/bin:/nix/var/nix/profiles/default/bin:/usr/bin:/bin" \
  TERM="${TERM:-xterm}" \
  DELTA_TEL_ROOT="$ROOT" \
  /usr/bin/bash -lc "cd $(printf %q "$ROOT") && $cmd"
EOS
