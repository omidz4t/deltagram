# Nix and incremental builds

`make init` installs the project-local Nix toolchain under ignored `data/nix`,
fetches pinned sources into ignored `context/`, builds the dependency closure,
and verifies shell entry. `./nix/develop.sh` opens the shell; `nix/enter.sh`
mounts the store through an unprivileged user namespace. Changing application
sources does not rebuild the dependency closure.

Only flake.nix and flake.lock enter the temporary Nix definition. Application
sources and build outputs never become a Nix source snapshot. The Qt donor is
materialized in `tdesktop/`; Core lives in `context/core/`. No Qt donor submodule
checkout is required for this source export.

Incremental state lives under ignored `data/`:

- `data/ccache` (compressed, capped at 5 GB by default)
- `data/tdesktop-out` (development GUI)
- `data/tdesktop-release` (release GUI)
- `data/cargo-target` and `data/cargo-home`

See [DEVELOPMENT.md](DEVELOPMENT.md) for overrides. Do not relocate an existing
CMake tree: its absolute paths and incremental records matter.

## Release executable

```sh
make release      # dist/deltagram, plus checksum and version sidecars
make release-all  # also Debian, RPM, Arch and portable archive packages
```

Docker must be running. Core and the GUI compile sequentially in the locked Nix
shell; packaging runs on the host so it can reach Docker. GUI compilation uses
`DELTA_TEL_JOBS=4`. The result embeds the GUI, Core and their runtime in one
executable and requires no Nix installation on the receiving machine. See the
[release guide](../docker/README.md) for platform requirements and verification.

Intermediate GUI outputs remain in `data/tdesktop-release/bin/Telegram` and
`Telegram.stripped`; these are not the portable user-facing release. Runtime
staging and container downloads stay in `data/release-stage` and
`data/container-build`. No manual copy into an older dist bundle is needed.

GUI builds use the prepared version without incrementing it, so retries retain
the same version and compiler caches. Before preparing a new release, run
`python3 scripts/semantic-release.py --dry-run`, then `make version` to update
`.version`, the app metadata/header and changelogs from committed Conventional
Commits. See [RELEASING.md](RELEASING.md). No cache cleaning is required.
The pipeline's caching and optional packaging are described in
[DEVELOPMENT.md](DEVELOPMENT.md).

`nix/release.sh` locks the complete build/stage/package sequence.
`nix/build-release.sh` also holds `.build.lock` in the release tree. Run one heavy
build at a time. Never delete, rename or replace `.ninja_deps`, `.ninja_log`,
`build.ninja`, the ccache directory or the release directory to recover from a
Ninja warning. A premature-end-of-file warning means retry the same command.
A full-nproc GUI release build can run out of memory; keep four GUI jobs.
`make clean` removes distribution/export output and preserves these caches.

## Validation

`make test` runs offline version regressions, shell syntax checks and a source
audit. After Core builds, `./nix/develop.sh --command bash nix/test-rpc.sh` tests
Qt/Core account creation and chat-list loading without mail IO.
`bash docker/verify.sh` checks the final single-file artifact in a container
without Nix or installed Qt/GTK. These checks do not prove network messaging or
compatibility with every graphics driver and Linux distribution.

The dependency closure supplies tg_owt and tlottie from pinned nixpkgs.
Telegram tde2e is excluded and conference attempts are refused; Chatmail Core
handles messaging encryption. GitHub Actions builds and publishes experimental releases on `main`; see
[the release guide](RELEASING.md). Local release commands only create artifacts.
