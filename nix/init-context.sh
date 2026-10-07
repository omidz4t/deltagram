#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$ROOT/nix/context-sources.sh"
mkdir -p "$ROOT/context"

# Preserve existing checkouts and materialized snapshots, including local edits.
# Publish a clone only after its pinned checkout has completed successfully.
clone_source() (
    name="$1" url="$2" revision="$3"
    destination="$ROOT/context/$name"
    if [[ -d "$destination" ]]; then
        echo "Keeping existing context/$name"
        exit 0
    fi
    temporary="$(mktemp -d "$ROOT/context/.clone-$name.XXXXXX")"
    trap 'rm -rf -- "$temporary"' EXIT
    git init --quiet "$temporary"
    git -C "$temporary" remote add origin "$url"
    git -C "$temporary" fetch --depth=1 --filter=blob:none origin "$revision"
    git -C "$temporary" -c advice.detachedHead=false checkout --quiet --detach FETCH_HEAD
    mv -T --no-clobber "$temporary" "$destination"
    echo "Initialized context/$name at $revision"
)

clone_source core "$CORE_URL" "$CORE_REV"
clone_source deltachat-desktop "$DESKTOP_URL" "$DESKTOP_REV"
