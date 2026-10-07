# Plan

The product is one `Telegram` binary: the Telegram Desktop window, with
Delta Chat behind it. `./delta-shell` is a separate tool and is not this plan.

## 1. Nix packages for the packaged build

`tdesktop` has no `Libraries/` directory, so CMake forces
`DESKTOP_APP_USE_PACKAGED` and `find_package` must succeed from the dev shell.

Already in Nixpkgs 25.05 and added to `flake.nix` when this step runs:
`ada`, `xxHash`, `minizip`, `tl-expected`, `rnnoise`, `openh264`,
`range-v3`, `openal`, `libopus`, `boost`, `microsoft-gsl`, `glibmm`,
`pango`, `libjpeg`, `xz`, `libheif`, `libavif`, `kdePackages.kcoreaddons`,
plus the Qt and media packages that were already listed.

Not in this Nixpkgs pin: `tde2e`, `tg_owt`. On Linux both are
`find_package(... REQUIRED)`. `tde2e` is `tdlib/td` at commit `51743df`
with `-DTD_E2E_ONLY=ON` (`Telegram/build/prepare/prepare.py`). `tg_owt` is
linked from `td_ui`, so the window does not configure without it. WebRTC is
built from the `lib_webrtc` submodule, not faked.

Status: CMake for the `Telegram` target finished inside `./nix/develop.sh`.
`tde2e` and `tg_owt` are built with that shell and installed under
`~/.cache/delta-tel/`. Patched `tlottie` is a static library from Nix
unstable Rust. The Ninja build of `Telegram` is in progress. Twelve and
then eight parallel compilers were killed by the OOM killer on 15G RAM,
so the resume uses 4 jobs. Qt stays the Nixpkgs 6.9 build. One
`QAccessible::Attribute::Orientation` use in `lib_ui` is compiled only
when Qt is 6.10 or newer.

## 2. Configure

```bash
./nix/develop.sh --command cmake -S tdesktop \
  -B "$HOME/.cache/delta-tel/tdesktop-out" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DTDESKTOP_API_TEST=ON \
  -DDESKTOP_APP_DISABLE_CRASH_REPORTS=ON \
  -DDESKTOP_APP_DISABLE_AUTOUPDATE=ON
```

Repeat until Ninja files exist. Each missing package is added to `flake.nix`
or satisfied from a submodule. Qt stays in the Nix store.

## 3. One Telegram binary

```bash
cmake --build "$HOME/.cache/delta-tel/tdesktop-out" --target Telegram -j"$(nproc)"
```

Every compile uses all cores (`AGENTS.md`).

Output: `$HOME/.cache/delta-tel/tdesktop-out/Telegram`. ccache keeps later
edits incremental. Do not rebuild Qt.

## 4. Run it once

Launch that ELF. The test `api_id` is enough for the window. At this point
it is still a Telegram client. That launch is the gate for the next step.

## 5. Delta Chat inside that window

Keep `DeltaRpc` and `deltachat-rpc-server` (stdio JSON Lines). Drive the
existing chat list and composer from it. No second window. No fake `MTP*`
objects. First slice: account, chat list, one chat, plain text, send,
incoming event, unread.

A single movable file of that `Telegram` ELF comes after the window runs.
