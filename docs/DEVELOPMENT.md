# Development and storage

## Commit conventions

All contributors, including coding agents, must use Conventional Commits and
sign every commit with their own configured Git identity and signing key.
Use `type(scope): summary`, with an optional scope and a short imperative
summary. Choose the type that describes the change:

- `feat`: add a feature.
- `fix`: correct a bug.
- `docs`: update documentation.
- `build`: change build tools, dependencies or packaging.
- `ci`: change automation workflows.
- `test`: add or update tests.
- `refactor`: restructure code without changing behavior.
- `perf`: improve performance.
- `style`: change formatting without changing behavior.
- `chore`: perform maintenance.
- `revert`: revert an earlier change.

Examples: `feat(chat): add account switching`, `fix(rpc): handle disconnects`,
and `docs: clarify release prerequisites`. Mark breaking changes with `!`
after the type or scope and explain them in a `BREAKING CHANGE:` footer.
Use the commit body to explain the reason and relevant validation when needed.

Configure `user.name`, `user.email`, `user.signingkey` and `commit.gpgsign=true`
before contributing, then commit with `git commit -S -m 'docs: clarify setup'`.
Check the signature with `git verify-commit HEAD`. Keep generated binaries,
credentials and local context sources out of commits. When squashing a branch,
give the resulting commit a conventional message and sign it again.

## Local builds

Run `make init` once, then `make build` or `make release`. The shell copies only
flake.nix and flake.lock into a temporary directory; application sources never
become a Nix store snapshot. The edited Qt donor lives in `tdesktop/`.
`make init` fetches pinned Chatmail Core and Delta Chat Desktop sources into
ignored `context/core/` and `context/deltachat-desktop/`. Desktop is a behavior
reference, not a build input. Source URLs and revisions are recorded in
`nix/context-sources.sh`. Initialization preserves existing directories and local
edits, including materialized snapshots without Git metadata; it never resets
them to the pinned revisions. Fresh checkouts need Git and network access.

Defaults are project-local: `data/nix`, `data/cache`, `data/ccache`,
`data/cargo-home`, `data/cargo-target`, `data/tdesktop-out` and
`data/tdesktop-release`. Set `DELTA_TEL_DATA` to choose another data directory.
Individual `DELTA_TEL_NIX_ROOT`, `CCACHE_DIR`, `CMAKE_BUILD_DIR`, `RELEASE_DIR`,
`CARGO_HOME` and `CARGO_TARGET_DIR` overrides remain supported. Do not relocate
an existing CMake tree: its absolute paths and incremental records matter.

ccache compresses entries and defaults to 5 GB (`CCACHE_MAXSIZE` overrides it).
The store and Cargo output have no automatic size cap. Do not run automatic
store garbage collection: portable bundles can reference store libraries.
`make clean` deletes only `dist/` and `publish-output/`; it never deletes build
trees, Ninja records, accounts or ccache. Release builds use four compiler jobs
and a lock. Run one heavy build at a time.

`make test` runs offline Python regressions, shell syntax checks and a source
audit. Run `python3 nix/audit-source.py --ips` inside the development shell to
review IPv4 literals. Public endpoints, loopback and fixture addresses are
expected in donor sources. Pattern matching cannot prove absence of secrets.

Both Core and the Qt client use the Rust/compiler dependencies pinned in
flake.lock. WebRTC and animation packages are supplied by the dependency
closure. A successful dependency shell alone is not proof of an application
build; run the build target to verify the current source.

Installer downloads and default temporary build files also live under `data/`,
which avoids filling a small tmpfs at /tmp. Set TMPDIR to override this explicitly.
UPX output is optional (`DELTA_TEL_PACK_UPX=1`); ordinary releases keep only the
normal and stripped intermediate executables. `make release` then stages their
runtime and uses Docker to produce the single-file `dist/deltagram` executable.
`make release-all` also produces Debian, RPM, Arch and portable archive packages
under `dist/packages/`. A running Docker daemon is required. Packaging stages
stay under `data/`; Docker images use Docker's own storage. See
[the release guide](../docker/README.md). Validation results are in VALIDATION.md.

## CI and Telegram conference removal

The root `.github/workflows/build.yml` is retained but currently disabled on
GitHub. When enabled, it installs Nix, initializes the locked dependency toolchain,
runs offline tests and the source audit, and builds Core RPC followed by the Qt
release. GUI compilation uses four jobs. It does not commit or upload binaries.
Release packaging also requires the runner's Docker daemon.
The workflow has read-only repository permissions and pins its actions to commit
IDs. `DELTA_TEL_USE_SYSTEM_NIX=1` uses the runner's existing /nix installation;
local builds retain the project-local single-user store.

The toolchain now pins nixos-unstable in flake.lock, providing a sufficiently new
Rust compiler, tg_owt and tlottie. Core no longer fetches a separate floating
Rust toolchain. These are dependency packages only: the application and Core
sources still build incrementally outside the Nix store.

The application no longer links tde2e or compiles its Telegram adapter. Telegram
conference starts and joins are refused; no substitute encryption is provided.
Chatmail Core remains responsible for messaging encryption. Delta Chat calling
support is not implemented by this change. The donor's remaining call code still
needs WebRTC; complete removal of that donor subsystem is separate work.

Normal builds omit the optional GUI automation and debugging tools. Use
`DELTA_TEL_SHELL=ui ./nix/develop.sh` for Xvfb, xdotool, image tools, GDB and
UPX. This keeps the default local/CI dependency closure smaller.

After building Core, run `./nix/develop.sh --command bash nix/test-rpc.sh` for the
existing Qt/Core RPC account-and-chat-list self-test. It uses disposable account
data, no mail IO, and a 30-second process timeout. CI runs it after the build.
