#!/usr/bin/env bash
# Shared local build state. Never move or erase an existing incremental tree.
DELTA_TEL_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export DELTA_TEL_DATA="${DELTA_TEL_DATA:-$DELTA_TEL_ROOT/data}"
export DELTA_TEL_NIX_ROOT="${DELTA_TEL_NIX_ROOT:-$DELTA_TEL_DATA/nix}"
export CCACHE_DIR="${CCACHE_DIR:-$DELTA_TEL_DATA/ccache}"
export CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-5G}"
export CMAKE_BUILD_DIR="${CMAKE_BUILD_DIR:-$DELTA_TEL_DATA/tdesktop-out}"
export RELEASE_DIR="${RELEASE_DIR:-$DELTA_TEL_DATA/tdesktop-release}"
export CARGO_TARGET_DIR="${CARGO_TARGET_DIR:-$DELTA_TEL_DATA/cargo-target}"
export CARGO_HOME="${CARGO_HOME:-$DELTA_TEL_DATA/cargo-home}"
export XDG_CACHE_HOME="${XDG_CACHE_HOME:-$DELTA_TEL_DATA/cache}"
export TMPDIR="${TMPDIR:-$DELTA_TEL_DATA/tmp}"
mkdir -p "$TMPDIR"
