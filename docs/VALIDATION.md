# Publication validation

Checks run on 2026-10-05, x86_64 Linux:

- Fresh single-user Nix installation, dependency closure build and shell entry.
- Two version regression tests and syntax checks for every project shell script.
- Publication audit across about 14,700 files, with no blocking findings.
- IPv4 locations reviewed: donor endpoints, test fixtures, examples and version
  strings. No project-specific address was identified. No private marker was
  found beyond the audit rule itself; Spectre occurs as a compiler option.
- SVG editor export paths removed; donor example home paths anonymized.
- No nested Git directories, gitlinks, generated ELF files or files at the
  GitHub 100 MB limit in the published source set.
- Signed Git commit verified using the globally configured signing key.

The initial GUI build was attempted through the Nix shell. CMake fails at the missing
`tde2e` package; tg_owt and tlottie also require external package artifacts.
Neither a successful application build nor RPC integration tests are claimed.
At that point the Core builder used an unpinned nixos-unstable Rust toolchain.

The initial import has upstream whitespace findings under `git diff --check`.
These were left intact to preserve donor sources and fixtures. The audit is a
pattern-based check, not proof that all possible private information is absent.
At the initial import no GitHub repository or remote was configured, and nothing
had been pushed.

Before the first GitHub push, eight upstream Windows runtime DLLs were excluded
from a fresh signed initial commit. Earlier local commits containing those DLLs
are not part of the published branch history. Images and other source resources
remain included. The private destination is themadorg/deltagram.

## Conference removal and CI follow-up

The updated locked dependency shell configures the full GUI without tde2e and
supplies tg_owt and tlottie. The three affected GUI translation units compile.
Core RPC builds with the same locked toolchain. The existing Qt RPC self-test
passes against that server: a temporary unconfigured account is created and
returns an empty chat list, without mail IO. The offline tests, source audit,
workflow actionlint check and system-Nix shell mode also pass.

Before the packaging work below, the full local GUI build was intentionally
stopped after compiling 1,320 steps without compiler errors; affected source
compilation was completed separately. At that stage the full link was unverified.
The retained CI workflow runs the RPC smoke test after release. An upstream
public update-verification key was restored from Telegram Desktop v7.2.10; only
that specific public PEM is allowed by the ignore/export rules.

## Single-file release and native packages

On 2026-10-05 the full GUI release compiled and linked with the locked toolchain,
using four compiler jobs and preserving the existing Ninja and ccache records.
Core RPC also built. A remaining conference-message consumer of the removed
tde2e API was removed; conference messages remain disabled, without a plaintext
fallback.

Release 7.2.42 is packaged as `dist/deltagram` (212,707,624 bytes), with version
and SHA-256 sidecars. It embeds the GUI, Core, matching loaders/libraries,
Qt JPEG/WebP/SVG plugins, PipeWire runtime, WebKit helpers, fonts and certificates.
Absolute ELF dependency names are normalized; the packaged GUI does not load
tlottie from `/nix/store`. WebKit helpers use the bundled loader. The WebP plugin
is required for the embedded emoji sheets; omitting it caused a startup assertion
and is now rejected during staging.

The same executable passed these checks in disposable containers with network
access disabled and no Nix store or installed Qt/GTK:

| Environment | Version | Core RPC response | GUI startup and profile-name keyboard input |
| --- | --- | --- | --- |
| Debian 13 slim | Passed | Passed | Passed |
| Ubuntu 22.04 | Passed | Passed | Passed |
| Fedora 43 | Passed | Passed | Passed |
| Arch Linux base image | Passed | Passed | Passed |

The test display and Python interpreter use separate Debian test dependencies;
the application uses only its embedded runtime. Tests confirmed an actual mapped
818 × 642 GUI window and the exact typed/clipboard text `portablekeyboard123`.
Profiles were disposable and no mail IO or real account was used.

`make release-all` completed the build and package pipeline. Debian, RPM, Arch
and portable tar packages use the same executable. Packaging validates metadata
and compares the extracted executable from each native package byte-for-byte
against `dist/deltagram`. SHA-256 checksums are provided. `make release` now runs
the isolated Debian GUI/RPC/input check before optional package generation.
The final native packages installed successfully using dpkg on Debian 13,
rpm on Fedora 43 and pacman on Arch. Installed executables returned client
version 7.2.42 and bundled Core version 2.63.0-dev with network access disabled.

Offline version regressions, project shell syntax checks, Python syntax checks,
source publication audit and `git diff --check` pass. No blocking source-audit
findings were reported. The standalone Qt/Core account/chat-list smoke test also
passes. No binary artifacts are tracked and GitHub Actions remains disabled.

These checks cover startup, input and packaging on Linux x86-64. They do not
establish Windows/macOS/ARM support, live messaging, every graphics driver,
complete WebKit functionality or all features in the compatibility tracker.

## Profile photo and account color regression checks — 2026-10-07

The locally modified client built successfully with four release compiler jobs,
preserving the existing Ninja and ccache records. A disposable three-account
fixture used the real Core RPC server with mail IO suppressed by a test proxy.
Settings image selection, cropping and confirmation saved a PNG through Core
and immediately repainted Settings and the sidebar. Replacing the photo and
restarting the client retained the new image. An injected Core save
failure displayed an error toast and retained the existing avatar. Holding an
old, empty avatar response for 40 seconds did not erase a newly saved photo.
Switching between two accounts without photos produced the exact Core colors
`#b23100` and `#c50000` in the selected sidebar avatar, matching their rows.

A separate Qt check disabled image plugin discovery and successfully encoded
and decoded the PNG used for the crop handoff. This avoids depending on the
JPEG encoder for that step. The development shell now exposes the Qt image
plugin paths for native runs. Offline project tests and the source audit passed.
These checks do not establish live avatar distribution to other devices.

`make release` produced the updated `dist/deltagram`. Its isolated Debian
verification passed the bundled Core RPC check, GUI startup and profile-name
keyboard input with networking disabled. The binary remains gitignored.

## Video forwarding and delivery indicators — 2026-10-07

The locally modified release client sent and forwarded an MP4 using the real
Core RPC server in a disposable account with mail IO suppressed. Forwarding
preserved the attachment bytes. The GUI Forward action successfully copied a
video without a caption into Saved Messages; both the inline player and full
media viewer played the video instead of displaying `[Video] filename`.
The attachment picker also sent a silent MP4 with `viewtype: Video`, covering
the donor's animation classification for videos without audio.

Delivery checks seeded the fixture database with Core pending and delivered
states and an MDN, then injected matching Core events through the test proxy.
The open chat and chat list changed from a clock to one check, then two checks.
The client queries Core's receipt count for delivered outgoing messages rather
than assuming every outgoing message is read. A fresh self-message also showed
one check for Core delivery without a read receipt, despite Saved Messages
using different donor flags for locally created messages.

The release compiler ran with four jobs and retained the incremental build
records. Offline project tests and the source audit passed. These checks cover
local MP4 rendering, forwarding and status transitions; they do not establish
live cross-client transport, every video codec or multi-recipient receipt
semantics.

The updated `dist/deltagram` passed the isolated Debian GUI startup, keyboard
input and bundled Core RPC checks with network access disabled.
