# Deltagram

**Experimental project.** Features are incomplete and compatibility is still
being tested. See the [compatibility tracker](docs/COMPATIBILITY.md) for feature
status, evidence and known limitations.

A native Qt chat client using Telegram Desktop as the UI donor and Chatmail Core
as its backend. Core owns messaging, persistence, encryption and networking.
New client messaging paths use Core JSON-RPC rather than MTProto.

Start with [onboarding](docs/ONBOARDING.md), the [architecture](docs/ARCHITECTURE-RECON.md)
and the [implementation plan](docs/MVP-PLAN.md). Build instructions are in
[docs/NIX.md](docs/NIX.md). The incremental release build uses
`DELTA_TEL_JOBS=4`; its build records and compiler cache live in ignored `data/`.

The [Debian 13 container packaging guide](docker/README.md) describes the
single-file desktop package. This packages an already-built release; it does
not compile the entire GUI inside Docker.

Before publishing a working development checkout, follow
[docs/PUBLISHING.md](docs/PUBLISHING.md). That workflow exports edited donor
sources without nested Git history, accounts, binaries or local diagnostics.

For sharing and scanning invites, see [Invite QR codes](docs/QR-CODES.md).

To put profiles in the folder sidebar, enable **Show profiles instead of folders**
in **Settings → Advanced → Experimental settings → Interface**. Click a profile
to switch; the setting is off by default and persists across restarts.

## License

Deltagram is free software licensed under the **GNU General Public License,
version 3 or later (GPL-3.0-or-later)**. See the root [LICENSE](LICENSE) for the
full license text.

This project derives from Telegram Desktop. Its copyright notices and licensing
terms remain in [tdesktop/LEGAL](tdesktop/LEGAL) and
[tdesktop/LICENSE](tdesktop/LICENSE), including its OpenSSL linking exception.
Third-party components retain their own licenses and notices, including
[Chatmail Core](https://github.com/chatmail/core/blob/main/LICENSE) and the
[ZXing QR decoder](licenses/ZXing-LICENSE) (Apache-2.0).

## Building

On x86_64 Linux with unprivileged user namespaces enabled (release packaging
also requires a running Docker daemon):

```sh
make init     # fetch context sources and install the local Nix toolchain
make build    # debug Core RPC server and Qt client
make test     # offline regression tests and source publication audit
make version  # prepare version and changelogs from Conventional Commits
make release  # one portable dist/deltagram executable, four GUI jobs
make release-all # executable plus Debian, RPM, Arch and portable tar packages
make clean    # remove disposable distribution/export output
```

The Nix store and build caches live under ignored `data/`. ccache defaults to
5 GB. `clean` preserves compiler caches and Ninja's incremental records.
See [development details](docs/DEVELOPMENT.md) for limits and overrides.
Version preparation is documented in [the release guide](docs/RELEASING.md);
release notes are in [CHANGELOG.md](CHANGELOG.md).

GitHub Actions automatically builds and publishes experimental releases from
Conventional Commits on `main`, starting with `0.1.0`. See the
[release guide](docs/RELEASING.md) for artifacts and version rules. Builds use
`flake.lock`. Telegram conference encryption is
excluded; messaging encryption remains in Chatmail Core. See
[development details](docs/DEVELOPMENT.md) for CI and optional GUI test tools.

Working in local checks:

- Core account creation and chat-list loading through Qt JSON-RPC.
- Contacts, contact cards, group creation and invitation previews/navigation.
- Broadcast channel presentation, subscriber write restrictions and owner read counts.
- Message information and reaction UI.

Incomplete or not yet verified:

- Voice playback UI, service events and complete profile-switch refresh.
- Delta Chat calls; Telegram conferences are disabled.
- Live SecureJoin completion, second-device transfer and broad end-to-end compatibility.
- Accessibility, RTL, advanced media/search and full webxdc behavior need validation.

The [compatibility tracker](docs/COMPATIBILITY.md) distinguishes implemented
behavior from locally verified behavior. Core and the Desktop behavior reference
live in ignored `context/`; `make init` fetches pinned sources when absent and
preserves existing local copies.

`dist/deltagram` embeds the GUI, Core and their runtime; it needs no adjacent
libraries or Nix installation. It targets Linux x86-64 with X11/XWayland,
glibc/zlib and executable temporary storage. Windows, macOS and ARM require
separate builds. See the [release and package guide](docker/README.md) for
outputs, installation and verification.
