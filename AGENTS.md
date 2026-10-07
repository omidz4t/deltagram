# Delta Tel

Telegram Desktop (`tdesktop/`) is the Qt donor. Chatmail Core (`context/core/`) is the
backend. The client must not speak MTProto and must not reimplement IMAP,
SMTP, or OpenPGP.

Read these before changing messaging behavior:

- `docs/ONBOARDING.md`
- `docs/ARCHITECTURE-RECON.md`
- `docs/MVP-PLAN.md`
- `docs/NIX.md`

`tdesktop/AGENTS.md` still holds the donor repo's style, rpl, and platform
rules. Where it tells you how to send an `MTP*` request, that applies only to
code that has not been cut over. New send and list paths use the generic
backend in the MVP plan.

Build the toolchain with `./nix/develop.sh`. Do not put the application
sources inside a Nix derivation; Ninja and ccache under
`data/` are the incremental build.

The release tree is `data/tdesktop-release`. ccache is
`data/ccache`, and `nix/build-release.sh` already launches
the compiler through ccache. Never delete, rename, or replace
`.ninja_deps`, `.ninja_log`, `build.ninja`, the ccache directory, or the
release build directory to recover from a Ninja warning or crash. Doing
that throws away the incremental record and forces a full rebuild of
about 1,700 files. A Ninja "premature end of file" warning means retry
the same build command; it does not mean the cache is corrupt.

`nix/build-release.sh` must run with `DELTA_TEL_JOBS=4`. A full
`nproc` run kills `cc1plus` (the compiler is "Killed" / out of memory)
and then Ninja reports a segmentation fault. Other CMake, Ninja, and
Cargo builds still use `-j$(nproc)` and `CMAKE_BUILD_PARALLEL_LEVEL=$(nproc)`.
One heavy build at a time. Do not start a second `build-release.sh` or
`ninja` in `tdesktop-release` while one is running.
