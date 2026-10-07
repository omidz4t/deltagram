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

## Portable WebKit regression — 2026-10-07

The isolated runtime check now loads an offline HTML page with bundled
WebKitGTK and requires JavaScript to set the browser window title. This catches
missing private subprocesses and injected modules that a Qt startup check does
not exercise. The portable runtime relocates WebKit's production helper paths
and preserves its sandbox configuration. This check covers basic HTML and
JavaScript; network access, WebRTC and application-specific WebViews require
separate validation.

## Native channel profiles — 2026-10-07

The channel profile now opens through the window navigation stack, with a
centered avatar/name/subscriber cover, compact mute/invite/edit/menu actions,
linked descriptions and native shared-media rows. Empty media categories
collapse without leaving gaps. Photo/video/file/GIF totals come from Core;
selecting a listed attachment opens its message in the channel.

A disposable offline Core account verified name and description saves,
notification changes, photo updates, photo/video counts, selection of a media
message and return from the profile to history. Repeated use of the menu also
passed after correcting its ownership. The release GUI built with four jobs,
and project tests and the source audit passed. Network delivery and a complete
media gallery remain outside these checks.

## Invite QR scanning

The Scan QR Code tab offers Qt camera capture, image files and clipboard
screenshots decoded with ZXing,
then fills the invite field for an explicit Join through Core. Switching tabs
collapses the invite canvas and changes the instructions; late invite-loading
callbacks cannot replace the scan instructions or access a closed dialog.

Decoder checks use a disposable Core account's generated invite: SVG rendering,
PNG and JPEG decoding, a 90-degree rotation, rejection of a blank image,
and conversion through QVideoSink/QVideoFrame before decoding. Qt reports zero
camera devices on the local machine. GUI checks passed for the no-camera
fallback, transparent image selection, clipboard image decoding, clearing an
old invite after a failed scan, and explicit joining through real Core
`check_qr` and `secure_join` calls with mail IO disabled.
Camera capture stops when switching tabs, after successful decoding, and when
closing the dialog. Physical camera capture is unverified: the local test
machine has no camera device. The portable bundle includes the Qt FFmpeg media
plugin and its dependency closure; the alternate GStreamer media stack is
excluded.


## Portable startup — 2026-10-07

The launcher caches each runtime by payload SHA-256 and uses file locks to
coordinate extraction and protect running older versions during cleanup. Five
launcher regression tests cover cache reuse and recovery, concurrent first
launches, active-version preservation, failed extraction and cache symlinks.

The same local `--bundle-version` runtime startup check took approximately
19.2 seconds on both launches with the previous executable. With the cached
gzip runtime, first extraction took 2.316 seconds and reuse took 0.003 seconds.
These measurements isolate runtime preparation; they do not measure account
loading, network synchronization or time to an interactive chat window.

The spellchecker starts with a valid UTF-8 dictionary instead of empty paths.
Portable startup installs a managed desktop entry matching the Deltagram Qt
application ID, while preserving an existing user-provided entry. It no longer
automatically registers the donor's Telegram and TON URL schemes. The isolated
release checks exercise GUI startup, keyboard input, Core RPC and sandboxed
WebKit JavaScript. The bundled Qt FFmpeg media plugin also passed image and
video-frame QR decoding without a host Qt or Nix installation. Physical camera
capture and registration with a real host desktop portal remain unverified.


## General chat wallpaper — 2026-10-07

The per-chat wallpaper action is removed from the chat menu. General wallpaper
remains under Settings → Chat Settings → Chat wallpaper, with a local gallery
and image-file selection. The gallery uses bundled backgrounds and the current
custom image instead of requesting a Telegram catalog. Applying a general
wallpaper saves it locally without uploading or installing it on a server.
Starting the donor's unauthenticated UI placeholder no longer resets global
preferences before Core profiles open.

A disposable offline Core profile verified gallery rendering and applying a
bundled background, then selecting and applying a custom PNG. The same custom
background appeared in another chat and after switching profiles and restarting
the GUI. The chat menu contained no wallpaper action. The release GUI compiled
with four jobs and preserved its incremental records. Full combinations of
day/night themes, tiling, blur, and imported theme files remain unverified.


## Adaptive channel information — 2026-10-07

Channel information uses the donor's modal-width threshold: narrow windows
show a navigation page, and wider windows show a centered modal over chat
history. Resizing changes between these presentations in both directions.
The More menu opens toward the inside of the panel. Clicking a channel photo
opens the original local Core image in an aspect-preserving large preview;
the preview width is limited to the available window width. Photo changes
also update the preview's source. No server photo or theme request is added.

A disposable offline Core profile verified modal rendering, transition to a
380-pixel-wide page and back to a 900-pixel-wide modal, header visibility in
the page, photo previews at both widths, repeated More menu opening, and
return to channel history. The GUI compiled with four jobs using the preserved
incremental build. These checks cover local window navigation and rendering;
physical multi-monitor placement and platform-specific window scaling remain
unverified.

## Forward labels and receipt refresh — 2026-10-07

Core-forwarded MP4s now retain the forwarded header in Saved Messages as well
as regular chats. The donor's Saved Messages sender layout no longer hides the
header or moves outgoing Core messages to the incoming side. Delivery state
from Core takes precedence over temporary donor sending flags in both the
message and chat-list indicators. Receipt callbacks refresh the view after
applying the message state, including the distinct Saved Messages peer mapping;
subsequent group receipt events also refresh the chat list.

A disposable Core account with mail IO suppressed verified attachment bytes,
forward labels in both chat types, and video indicator transitions using seeded
pending/delivered states and a seeded receipt with matching Core events. These
are local rendering checks, not live transport or cross-client receipt tests.
One check means Core reports sent; two checks require Core's read state or a
recorded receipt. A recipient who disables receipts cannot provide a reliable
seen indication. Release compilation uses four jobs and preserves build caches.


## Experimental profile sidebar — 2026-10-07

Settings → Advanced → Experimental settings → Interface contains **Show
profiles instead of folders**, also searchable by “profiles.” It is off by
default and uses the existing persistent experimental options store. Enabling
it immediately places profile shortcuts in the vertical folder strip; disabling
it restores the existing folder/sidebar behavior. Each shortcut uses Core's
photo, display name and color, and the selected profile has an active marker.
Configured profiles switch through the backend; an unconfigured profile opens
account setup. No folder, message or account data is changed by the toggle.

A disposable Core account fixture with mail IO suppressed checked the actual
settings row, repeated on/off toggling without restart, switching between
configured profiles, photos/initials, active colors, a narrow window, and
setting/selection persistence after reopening the client. The release build
uses four GUI compiler jobs and retains incremental build records and caches.
