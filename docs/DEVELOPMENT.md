# Development guide

This guide covers the supported development workflow, contribution conventions,
build storage and continuous integration for Deltagram. The project is
experimental; consult the [compatibility tracker](COMPATIBILITY.md) before
assuming a feature is implemented or verified.

## Architecture and project layout

Deltagram uses Telegram Desktop as its Qt UI donor and Chatmail Core as its
messaging backend. Core owns persistence, encryption and networking. New
messaging paths must use Core JSON-RPC; they must not introduce MTProto requests
or reimplement IMAP, SMTP or OpenPGP.

Read [ONBOARDING.md](ONBOARDING.md),
[ARCHITECTURE-RECON.md](ARCHITECTURE-RECON.md) and [MVP-PLAN.md](MVP-PLAN.md)
before changing messaging behavior. Follow the repository's `AGENTS.md`
instructions and the donor's applicable code conventions.

| Location | Purpose | Git status |
| --- | --- | --- |
| `tdesktop/` | Edited Qt client and donor sources | Tracked |
| `context/core/` | Pinned Chatmail Core checkout | Ignored |
| `context/deltachat-desktop/` | Pinned Desktop reference checkout | Ignored |
| `nix/` | Toolchain, build and validation scripts | Tracked |
| `scripts/` | Project automation, including version preparation | Tracked |
| `data/` | Local toolchain, caches and incremental build state | Ignored |
| `dist/` | Generated executables and distribution packages | Ignored |
| `.version` and `CHANGELOG.md` | Prepared project version and release notes | Tracked |

Delta Chat Desktop is a behavior reference, not a build dependency. Upstream
source URLs and revisions are recorded in `nix/context-sources.sh`.

## Set up the development environment

The current build workflow targets x86-64 Linux. Local Nix installation requires
unprivileged user namespaces. Git, Bash, Make, curl and standard Linux utilities
must be available; initialization also requires network access. Portable release
packaging requires Docker and a running daemon.

From the project root:

```sh
make init
make build
make test
```

`make init` fetches missing context checkouts, installs the project-local Nix
toolchain, builds the locked dependency closure and verifies shell entry. It
preserves existing context directories and local edits, including source
snapshots without Git metadata. Existing directories are not reset to their
pinned revisions; inspect them when validating reproducibility.

Enter the dependency shell for manual commands:

```sh
./nix/develop.sh
```

For GUI automation and debugging tools, including Xvfb, xdotool, image tools,
GDB and UPX, select the optional UI shell:

```sh
DELTA_TEL_SHELL=ui ./nix/develop.sh
```

The default shell omits those tools to reduce its dependency closure. See
[NIX.md](NIX.md) for toolchain setup and storage details.

## Build and validation commands

| Command | Result |
| --- | --- |
| `make init` | Initialize context sources and the locked toolchain |
| `make build` | Build Core RPC and the Qt client for development |
| `make test` | Run offline Python regressions, shell syntax checks and the source audit |
| `make version` | Prepare the version and changelogs from committed Conventional Commits |
| `make release` | Build and verify the portable `dist/deltagram` executable |
| `make release-all` | Also produce Debian, RPM, Arch and portable archive packages |
| `make clean` | Remove `dist/` and `publish-output/` |

A successful dependency-shell initialization does not verify the application.
Run the relevant build target after source changes, and record the checks that
support the change.

After building Core, exercise account creation and chat-list loading through
the real Qt/Core RPC transport:

```sh
./nix/develop.sh --command bash nix/test-rpc.sh
```

The smoke test uses disposable account data, performs no mail IO and limits the
application process to 30 seconds. Review IPv4 literals separately when preparing
source for publication:

```sh
./nix/develop.sh --command python3 nix/audit-source.py --ips
```

Public endpoints, loopback addresses and test fixtures can legitimately appear
in donor sources. Automated pattern checks complement manual review; they do
not establish that a checkout contains no secrets. See
[VALIDATION.md](VALIDATION.md) for recorded results and
[PUBLISHING.md](PUBLISHING.md) for source publication requirements.

## Incremental builds and storage

The toolchain is pinned by `flake.nix` and `flake.lock`. Only those definitions
are copied into the temporary Nix input; application and Core sources build
outside the store using CMake, Ninja, Cargo and ccache. Source edits therefore
reuse the dependency closure and existing compilation state.

| Default path | Contents | Override |
| --- | --- | --- |
| `data/nix` | Project-local Nix installation and store | `DELTA_TEL_NIX_ROOT` |
| `data/cache` | Application/tool cache files | `XDG_CACHE_HOME` |
| `data/ccache` | Compressed C/C++ compiler cache | `CCACHE_DIR` |
| `data/cargo-home` | Cargo registry and Git dependencies | `CARGO_HOME` |
| `data/cargo-target` | Rust build outputs | `CARGO_TARGET_DIR` |
| `data/tdesktop-out` | Development GUI build tree | `CMAKE_BUILD_DIR` |
| `data/tdesktop-release` | Release GUI build tree | `RELEASE_DIR` |
| `data/tmp` | Temporary build files | `TMPDIR` |

Set `DELTA_TEL_DATA` before initialization to choose another base directory.
Individual overrides take precedence. Keep existing CMake trees at their
original paths: their configuration and incremental records contain absolute
paths.

ccache compresses entries and defaults to a 5 GB limit; use `CCACHE_MAXSIZE` to
adjust it. The Nix store and Cargo outputs have no automatic size cap. Installer
downloads and packaging stages also use `data/`, while Docker images use Docker's
own storage. Avoid automatic Nix store garbage collection because staged runtimes
can reference store libraries.

**Preserve incremental build state.** Do not delete, rename or replace the
release build directory, ccache, `build.ninja`, `.ninja_deps` or `.ninja_log`
when recovering from a Ninja warning or crash. A premature-end-of-file warning
requires retrying the same build command. `make clean` preserves these records,
compiler caches, build trees and account data.

Run one heavy build at a time. Release GUI compilation must use
`DELTA_TEL_JOBS=4`; using every CPU can exhaust memory. Release scripts hold locks
to serialize compilation and packaging. Other CMake, Ninja and Cargo builds use
`nproc` as specified by the project instructions.

## Contribution and commit conventions

All contributors, including coding agents, must use Conventional Commits and
sign every commit with their own configured Git identity and signing key.

```text
type(scope): imperative summary
```

The scope is optional. Keep the summary concise and use the body to explain the
reason for the change and relevant validation when needed.

| Type | Use |
| --- | --- |
| `feat` | Introduce a feature |
| `fix` | Correct a bug |
| `perf` | Improve performance |
| `refactor` | Restructure code without changing behavior |
| `docs` | Update documentation |
| `test` | Add or update tests |
| `build` | Change build tools, dependencies or packaging |
| `ci` | Change automation workflows |
| `style` | Change formatting without changing behavior |
| `chore` | Perform maintenance |
| `revert` | Revert an earlier change |

Examples:

```text
feat(chat): add account switching
fix(rpc): handle disconnects
build: reduce release packaging time
docs: clarify development prerequisites
```

Mark a breaking change with `!` after the type or scope, and explain the impact
in a `BREAKING CHANGE:` footer:

```text
feat(rpc)!: replace the account selection API

BREAKING CHANGE: callers must provide an explicit account identifier.
```

Configure `user.name`, `user.email`, `user.signingkey` and
`commit.gpgsign=true` before contributing. Create and verify signed commits with:

```sh
git commit -S -m 'docs: clarify development prerequisites'
git verify-commit HEAD
```

Keep credentials, local context sources and generated binaries out of commits.
When squashing a branch, give the resulting commit a conventional message and
sign it again. Version bump rules are documented in [RELEASING.md](RELEASING.md).

## Version preparation and release packaging

`.version` is the source of truth. `scripts/semantic-release.py` reads committed
Conventional Commits and synchronizes the project changelog, app changelog,
build metadata and compiled version header. Builds use the prepared version and
never increment it automatically.

Commit the source changes before preparing a release, then preview the result:

```sh
./nix/develop.sh --command python3 scripts/semantic-release.py --dry-run
make version
```

Review the generated files and commit them together with a signed conventional
message before distributing a release. See [RELEASING.md](RELEASING.md) for
history boundaries, bump rules and repeat-run behavior.

`make release` builds Core and the GUI, stages their runtime and uses Docker to
produce one executable. `make release-all` also writes distribution packages to
`dist/packages/`. The portable executable targets Linux x86-64; platform
requirements and installation instructions are in the
[packaging guide](../docker/README.md).

Normal releases retain the ordinary and stripped intermediate GUI executables.
`DELTA_TEL_PACK_UPX=1` optionally compresses an intermediate copy when UPX is
available; it is not required for portable packaging.

## Continuous integration

The workflow in [`.github/workflows/build.yml`](../.github/workflows/build.yml)
runs on pushes to `main`, pull requests and manual dispatch. It:

1. Runs offline regressions, shell syntax checks and the source audit before downloading the toolchain.
2. Restores dependency and compiler caches, then initializes Core and the dependency shell.
3. Builds Core RPC and the Qt release sequentially, with four GUI compiler jobs.
4. Runs the Qt/Core RPC smoke test and reports compiler cache statistics.
5. Packages and verifies release assets when Conventional Commits require a release.
6. Publishes an experimental GitHub release after all build and packaging checks pass.

Pull requests compile and validate RPC. Pushes to `main` with release-worthy
commits also publish packages; documentation-only pushes run offline checks.
Manual dispatch with `publish` disabled runs a build without publication. CI omits the Desktop reference clone and separate dependency
`buildEnv` construction. It uses the runner's existing `/nix` installation through
`DELTA_TEL_USE_SYSTEM_NIX=1`; local builds retain the project-local store.

The Nix cache is keyed by toolchain definitions and populated from `main`.
The Qt compiler cache uses a toolchain key and commit-specific snapshot, with a
compatible prefix for reuse. Rust dependencies and the compiled Core use a
separate immutable key tied to the toolchain, pinned Core revision and build
script; ordinary application commits reuse it without uploading another copy.
Both are saved after successful compilation and smoke tests, before packaging,
so a packaging failure does not discard the compiled work. The first build has
no compiler cache and remains expensive. ccache is capped at 5 GB. Cache eviction
and transfer costs can affect performance; use per-run statistics and timings.
Cache misses still perform normal builds, and no automatic store garbage
collection runs.

CI enables ccache's PCH settings, `pch_defines,time_macros`, for the donor's
precompiled headers. These settings relax macro checks within PCH and can retain
the original date in cached build-date strings. Compiler flags, header content
and the toolchain remain part of cache validation.

Build jobs have read-only repository permissions; only the publishing job can
write release assets. Actions are pinned to commit IDs. CI prepares version files
in its workspace without creating commits. Source commits must be signed by
contributors. See [release preparation](RELEASING.md).

## Messaging and calling boundaries

Chatmail Core provides messaging encryption. The client excludes the Telegram
conference encryption dependency and adapter; Telegram conference starts and
joins are refused. Delta Chat calling support remains unimplemented, while the
donor's remaining call code still requires WebRTC. Consult the
[compatibility tracker](COMPATIBILITY.md) for the current scope and evidence.
