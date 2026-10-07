# Progress

Updated 2026-10-04.

This is a chronological development log. Current feature status and validation
limits are tracked in [COMPATIBILITY.md](COMPATIBILITY.md); newer entries
supersede older limitations.

## Invitation links, contact sharing, groups and channel counts

Core-loaded message text now uses the native text-link parser. Clicking an
`http(s)://i.delta.chat/#...` invitation routes to Core's `check_qr` before
browser handling or URL token rewriting. Contact, group and channel previews
offer Chat/Join and Cancel. Confirmation uses `secure_join`, loads the actual
Core chat metadata and navigates to that chat; incoming channel write access
is available before navigation. Own invitations are identified without
withdrawing or reviving their tokens. Requests are guarded against account
changes and closed dialogs.

The attachment menu has Send Contact and a native contact picker, including
owned channels. Selection uses Core's existing make-vCard/send-file path.
Shared cards open a centered photo/name/bio profile dialog with Chat and
Cancel. The embedded card photo takes priority over Core's generic icon for
address contacts. Core imports the card identity for profile lookup; no chat
is created until Chat. The bio is Core's contact status, when available:
the currently bundled Core does not import NOTE biographies from vCards.

New Group uses the native name/photo page followed by a searchable contact
selection page with selection chips and Create/Cancel. Members and optional
photo are applied through Core. The Telegram-only TTL reload is skipped.
Owned channel posts use the native eye/count display alongside the time,
including zero, from Core's `get_message_read_receipt_count`. Read-count
events refresh existing posts. These are owner-visible read receipts, not
public Telegram-style view totals; subscriber clients cannot obtain them.

Isolated Core regressions cover card photo persistence, import, profile/bio
lookup and chat opening; invitation preview kinds; channel joining, read-only
access and receipt counts; and group member selection. GUI checks confirmed
group selection, chip removal and actual stored members; zero/one eye counts;
attachment contact selection and an actual sent card; card photo/profile
display and Chat navigation; and confirmation/navigation for channel, group
and contact invitation links. SecureJoin network completion was not tested:
the disposable accounts deliberately have no mail transport IO. Real profiles
and incremental build records were preserved.
Release 7.2.37 compiled, packaged and was installed, verified byte-for-byte
against the stripped release. The final GUI check confirmed the centered
photo, name and available biography in the shared-contact dialog.

## Add Second Device adaptive height

The QR canvas now sits in a collapsed SlideWrap on the initial explanation
page, so it reserves no hidden 280-pixel height. Once Core returns a valid QR
code, the wrapper expands and the dialog follows its content height.
Release 7.2.29 compiled, packaged and was installed, verified against the
stripped release. Isolated GUI checks confirmed the compact initial page and
expansion for an actual Core-generated QR. The test offer was cancelled;
real accounts and incremental build caches were preserved.

## Contact list privacy and deletion

Contact rows show names without email subtitles; unnamed contacts use a
generic Contact label. Address lookup remains available through local search.
The context menu has Open Chat, Share Contact and Delete Contact. Deletion
opens a confirmation with an unchecked option to also delete the direct chat
and its messages. Core's existing-chat lookup avoids creating a chat during
deletion. Chat deletion succeeds before contact deletion, with errors surfaced
and lifetimes/account changes guarded. The native chat list is updated after
successful chat deletion and the contact row is removed after success.
The isolated RPC regression passes contact-only deletion while retaining the
chat, contact-plus-chat deletion, and deletion when no chat exists.
Release 7.2.28 compiled, packaged and was installed, verified against the
stripped release. Isolated GUI checks confirmed name-only rows, the contact
context menu, the unchecked chat-deletion option and successful removal of
both the selected test contact and its chat without a crash. Real accounts
and build caches were preserved.

## Message Info action

Both message context-menu paths have an Info action for stored Core messages,
including attachment menus. It opens a selectable, scrollable Message info
dialog populated by Core's `get_message_info`. The callback is guarded by
dialog lifetime, active-account lifetime and account epoch; errors are shown
in the dialog. The isolated Core regression verifies the informational text
for a stored contact attachment, including its sent timestamp and filename.
Release 7.2.27 compiled, packaged and was installed, verified against the
stripped release binary. An isolated GUI check confirmed the menu action and
the populated details dialog for the clicked attachment. User accounts and
build caches were preserved.

## Compact reaction context menu

The collapsed reaction picker preserves Telegram's calculated context-menu
width instead of forcing the 320-pixel expanded picker width onto every menu.
Expanding uses the wider searchable emoji grid; collapsing restores the
original content width. Release 7.2.25 compiled, packaged and was installed,
verified against the stripped release. Isolated GUI checks confirmed the
compact menu and functioning expanded search picker. Caches were preserved.

## Shared contact cards

Core `Vcard` messages use the native contact-card media renderer instead of
the plain `Contact: name/address` text fallback. The generic bridge keeps
Core's parsed contact metadata for history construction, scoped to the active
account. The card shows the shared person's name and embedded profile image
with a circular crop, or initials when the card contains no usable photo.
The address remains accessible in the native details dialog. Media cloning
preserves the photo. Delta-mode cards hide Telegram phone/contact action rows.
The isolated contact RPC regression checks photo preservation through parsing
and message blob storage, in addition to existing sharing/import coverage.
Release 7.2.23 compiled, packaged and was installed, with the installed binary
verified against the stripped release. An isolated GUI check of a stored card
in a one-to-one chat confirmed the embedded photo, name-only card and working
email details click. Build caches and user accounts were preserved.

## Telegram channel presentation on Core broadcasts

New Channel opens Telegram's existing `GroupInfoBox` channel screen, including
the cropped photo chooser, channel name and description. Creation, metadata,
invitations and membership changes use Core RPC. After creation, the native
subscriber list opens with Add Subscribers leading to a channel-specific
invitation with QR, Copy Link and Share through the native recipient picker.
Core requires a SecureJoin invitation to subscribe to broadcasts; group-member
addition is not used. Owners can remove subscribers from the list context menu.

Broadcasts have Telegram's channel icon in the chat list and channel post
alignment. Channel metadata supplies the subtitle, including a live subscriber
count for owners; incoming channels never reveal subscriber lists or counts.
The channel info screen uses Telegram's profile cover widgets and styling,
with Core-backed edits, photos and notification controls. Incoming channels
disable sending and use the existing Telegram Mute/Unmute composer button.
Outgoing channels use the Broadcast placeholder. Personal call buttons are
hidden. Established subscribers can leave; pending joins do not expose Leave.

The isolated RPC test passes creation, name/description/photo updates,
channel-specific invite/SVG generation, Core subscription initiation, read-only
access, mute/unmute, leaving and owner removal. It uses disposable pseudo
transports with no mail IO, Core-generated identity cards, and explicit fixture
membership for post-handshake removal tests; it does not verify live delivery.

Release 7.2.22 compiled and packaged successfully and is installed in
`dist/delta-tel/Telegram`, verified against the stripped release binary.
An isolated Xvfb run of the compiled application confirmed the channel icon,
subscriber subtitle, channel post alignment, Broadcast composer and Channel
Info screen. The GUI fixture used disposable accounts; live mail delivery
was not tested. Incremental build records and ccache were preserved.

## Contact sharing and contact-opening lifetime

Contacts has a native right-click Share Contact action and recipient picker.
The bridge asks Core's `make_vcard` for the card, sends it as `Vcard` through
`send_msg`, and keeps the temporary file alive until Core copies it. Account
changes cancel completion. Cards display Core's parsed name and address in
history. Recipient search is local in Delta mode, without Telegram global
search requests. The isolated RPC regression verifies generation, parsing,
attachment persistence after deleting the source file, and import.

Opening a contact now copies the completion callback before closing the list
and defers the transition to the next event-loop turn. This prevents destroying
the callback while invoking it and avoids closing widgets inside the RPC reply.
Live reproduction of the reported contact-opening crash remains pending.

The toolchain launcher now passes only `flake.nix` and `flake.lock` to Nix.
Previously each source edit caused another multi-gigabyte project snapshot in
the store; the final verification attempt hit disk exhaustion. Using the same
toolchain definition independently of application sources resumed the build
without clearing any cache or changing the incremental release tree.

Version 7.2.20 beta compiled and linked with four jobs, finished packaging, and
was installed atomically in `dist/delta-tel`. The installed binary matches the
stripped release output. The updated launcher and isolated contacts/card RPC
test passed. Live contact opening and delivery remain unverified here.

## Native expandable reactions and Contacts layout

The reaction popup uses Telegram's existing `EmojiPickerOverlay`, including
its expand/collapse arrow, Unicode emoji grid, search, and selection feedback.
The redundant Reactions menu entries were removed from both history menus.
Emoji search uses the bundled keyword suggestions in Delta mode without
requesting Telegram language packs. An isolated test of the compiled suggestion
source and generated dictionary passed for heart, smile, and thumb.

Contacts now uses the native `PeerListBox` layout and contacts row styling,
with local avatars, initial placeholders, search, recent/alphabetical sorting,
alphabetic section headers, Close, and Add Contact. Add Contact opens the
existing share/invite QR panel. Contact rows retain Core IDs, and opening one
uses Core's create/open-chat API rather than Telegram peers or network calls.
The isolated contacts RPC test passed. Version 7.2.18 beta compiled and linked
successfully with four jobs and was installed in `dist/delta-tel`; the installed
binary matches the stripped release output. Live visual verification remains
pending. Release builds now hold a lock through packaging to prevent overlapping
builds from writing the shared Ninja output.

## Reaction badge geometry, floating picker, and Contacts

Static reaction artwork now fits the 18-pixel badge slot inside the existing
32-pixel animation canvas, preventing overlap with counts and timestamps.
Delta's right-click popup also has a floating row of eight static emoji with
hover and selected feedback; choosing an emoji toggles it through Core without
Telegram animation downloads.

The left menu now opens a searchable Contacts page. It loads both key and
address contacts, excludes special contacts, and opens or creates the selected
contact's chat through Core. Async results are guarded against a closed page
or a changed account. `nix/test-contacts-rpc.py` passed using a temporary
account, covering contact listing and stable chat IDs when reopening a contact.
Version 7.2.17 beta compiled and linked with the floating-picker changes and
was installed in `dist/delta-tel`; the installed binary matches the stripped
release output. The contacts test passed again. Live visual verification
remains pending. A concurrent build was detected after a link failure; this
task stopped its retry before compilation and verified the completed shared
build instead. Build caches were preserved.

## Portable runtime JPEG support

The portable bundle omitted Qt Base's image-format plugins, including JPEG.
The avatar upload path encodes JPEG, so it failed before calling Core; received
JPEG avatars could not be decoded either. A standalone Qt codec probe using
the packaged libraries and explicit packaged plugin directory reproduced the
save failure (exit 1). Adding the matching `libqjpeg.so` made the same save/read
round trip pass (exit 0), outside the Nix mount. The probe requires only the
GL dispatch library for linking, and creates no window or graphics context.

Release builds now run `nix/package-image-codecs.sh` to include JPEG, GIF, and
ICO support with relative library paths while preserving the existing runtime.
`nix/test-avatar-codecs.cpp` checks JPEG encoding and decoding without touching
accounts. Live Settings and received-avatar UI verification remains pending.
Version 7.2.15 beta was built and installed; the installed binary matches the
stripped release output. The final packaged JPEG save/read test and isolated
Core avatar save/replace test both passed. A concurrent release build appeared
during toolchain preparation, so this task stopped its own pending build before
compilation and verified the shared build output instead.

## Emoji reactions in the main chat

Delta reaction images are rendered from the bundled emoji artwork instead of
waiting for Telegram reaction-animation documents. This supplies the missing
icon in received reaction badges. The main history context menu now includes
a Reactions submenu with eight emoji choices. It uses the existing Core
`send_reaction` path; choosing one's current reaction again retracts it.
Previously the emoji actions existed only in the separate list-view menu,
not the main history widget. Menu callbacks resolve the message by ID so
deletion while the menu is open is safe.

Version 7.2.14 beta compiled and linked successfully with four jobs. The stripped
binary and version sidecar were copied into `dist/delta-tel`; the binary matches
the release output. Live two-device verification remains pending.

## Received message deletions

The bridge now handles Core's `MsgDeleted` event by destroying the matching
loaded history item and refreshing the dialog list. Core emits this event
for authenticated incoming delete requests as well as local deletion and expiry.
Deleted IDs are remembered for the active account so pending media fetches
and history batches cannot restore a removed message. Account switches clear
that set and invalidate older message fetches.

`nix/test-deletion-event.py` uses an isolated temporary Core account to verify
the JSON event's account/chat/message IDs and removal from Core history.
It passed against the packaged RPC server. Version 7.2.13 beta compiled and
linked with four jobs; the stripped binary and version sidecar were copied to
`dist/delta-tel` and compared with the release output. Two-device UI verification
remains pending.

## Photo progress completion and build identification

Settings subscribes to photo-upload progress before starting the operation,
including synchronous failures. Completion and failure now clear the overlay
directly and restore the peer's stored photo, without depending on fade-out
animation callbacks. The My Account preview no longer overwrites Delta's peer
image before Core accepts the save. The remaining donor preview encoder now
opens its buffer before writing.

Both application build scripts increment the patch version, with a CMake
dependency ensuring the new version is embedded. Version-script regression
tests cover consecutive increments and refusing numeric patch overflow.
Version 7.2.12 beta compiled and linked successfully with four jobs. Both
version tests passed, including matching the UI header to build metadata.
The stripped binary and `Telegram.version` were copied into `dist/delta-tel`;
the binary matches the build output and contains 7.2.12. Photo-spinner runtime
verification remains pending. The supplied log contained normal relay/IMAP
activity, without a photo-save error.

## Profile photo storage and display

Known Core avatar files now become in-memory image locations before donor
peer-photo location construction. Delta contacts without a Telegram input
peer otherwise produced invalid locations, preventing even local decoding.
Self-photo upload no longer replaces Core's updated image with a random-ID
image encoded through an unopened buffer. Each upload owns a unique temporary
file until Core has copied it; save/read errors report upload failure, and
account switches cannot apply the saved image to the wrong profile.
`SelfavatarChanged` events reload the account avatar through the normal peer
update path. Core remains responsible for storage and image distribution.
An isolated RPC test passed for avatar save, replacement, self-contact image
paths, temporary-file removal, and preservation after a failed save. The
four-job incremental release build passed, and the stripped binary was copied
to `dist/delta-tel/Telegram` and compared with the build output. Live UI
verification remains pending; no real profiles or caches were cleared.

## Profile confirmation and reorder feedback

Delete confirmation now uses the confirmation helper's close callback before
issuing removal. Previously it stayed open after success, allowing another
click to remove the same ID and report "no account with id". Reorder errors
are now displayed through a guarded controller callback rather than ignored.
The order parameter is explicitly wrapped as one JSON array argument.

A Qt serialization probe verified both the previous and explicit forms send
the correct nested array. An isolated test with three temporary accounts on
the bundled RPC server verified `set_accounts_order`, returned profile order,
and deletion preserving the remaining order. No real accounts were changed.
The reported live-menu reorder failure has not been reproduced.

## Apps browser with GL disabled

Apps already uses `Ui::BotWebView::Show`, the same panel as Telegram bot
apps. The bundled launcher defaults to `QT_XCB_GL_INTEGRATION=none`, but the
graphics detector still attempted OpenGL/QRhi probes. The detector now honors
that setting on XCB before creating probe widgets or graphics contexts,
selecting the existing raster fallback for the browser panel. The embedded
browser and its storage remain unchanged.
Incremental release compilation and linking passed with four jobs. The
stripped binary in `dist/delta-tel/Telegram` matches the build output. Apps
opening on the affected host remains unverified; no caches were cleared.

## Profile deletion callback lifetime

The profile menu captures a weak menu reference before opening confirmation,
and guards confirmation and deletion completion with the session controller.
This removes the access to a menu that may already have been destroyed by
the confirmation dialog. Profile-list and reorder callbacks are also guarded
against menu closure or list replacement. Add-profile callbacks capture their
controller before closing the menu. Account deletion still goes through Core;
no protocol or storage behavior changed.
The incremental release compile/link passed with four jobs; the stripped
binary was installed in `dist/delta-tel/Telegram` and compared with the build
output. Runtime deletion verification remains pending. No real profiles or
caches were deleted during verification.

## Account identity in the menu

Startup reads Core's configured `displayname` instead of the self-contact's
localized "Me" label. Saved Messages chat-list artwork no longer overwrites
the account's `selfavatar`. Profile photo updates now use the existing peer
update path so open profile widgets refresh, and successful display-name
edits update the bridge's self identity immediately. Empty names on account
switches use the default label rather than the previous account's name.
This corrects the existing bridge without adding protocol dependencies.
Verified against Core's self-contact and self-chat implementations; the
incremental release compile/link passed with four jobs. The stripped binary
was copied to `dist/delta-tel/Telegram` and matched against the build output.
Live menu verification is pending; caches and account data were preserved.

## Reply preview correction

`delta/delta_bridge.cpp` now creates reply previews only from Core's `quote`
field. The `parentId` field represents email threading and may be present on
ordinary messages, so it must not create a reply preview. Explicit message
quotes and text-only quotes retain their existing rendering. This corrects
the existing JSON-to-history boundary without adding a protocol dependency.
The mapping was checked against Core's `MessageObject::from_msg_id` and the
reference desktop client's quote rendering; live conversation verification
is still pending.

## Compact header profile photo

Peer image views now read known Core avatar files through
`Delta::AvatarImage`, sharing the loaded image between the chat list and
header. This avoids sending a local avatar through the donor's file download
path. `UserpicButton` also prepares a fallback while a photo is loading,
instead of leaving its space blank. No cache or account data is cleared.
Visual verification in a live narrow-window conversation is still pending.

Both fixes compiled and linked in the existing release tree with
`DELTA_TEL_JOBS=4`. The stripped binary was copied to
`dist/delta-tel/Telegram` and checked byte-for-byte against the build output.

## What you can run

The laptop bundle is `dist/delta-tel/`:

```text
telegram            wrapper
Telegram            Debug Qt binary, about 6.2 GB
telegram-lib/       glibc 2.40 loader, Qt, PipeWire, libspawnfix.so
telegram-lib/rpc-rt/  glibc 2.44 loader and libs for deltachat-rpc-server
telegram-plugins/
intro1.png
```

Launch with `./telegram` from that directory. The wrapper leaves
`LD_LIBRARY_PATH` unset, does not ship Nix `libGL`, `libGLX`, `libEGL`, or
`libGLdispatch`, and puts the host lib dir after `telegram-lib` on
`--library-path` when `libGLX.so.0` or `libGL.so.1` is there.

Accounts go to `~/.local/share/delta-tel/accounts`.

Checked here:

- `deltachat-rpc-server --version` prints `2.63.0-dev` when started with
  `telegram-lib/rpc-rt/ld-linux-x86-64.so.2` and `--library-path` set to
  `rpc-rt`, outside the Nix namespace.
- An offscreen launch that could see a libGLX stayed up and spawned that RPC
  server. Core then created an account directory.
- This machine has no host `libGLX` and no X server, so a 4.3 core GLX
  framebuffer config was not created here. Without some `libGLX.so.0` on the
  second library path, the dynamic loader stops at startup.

`./delta-shell` is an earlier small window. It is not this build.

## Onboarding

The Telegram window no longer constructs `Intro::Widget`, so the phone and
QR login is not started. The first screen is the Delta Chat welcome: title
"Welcome to Delta Chat" or "Add Profile", body "Secure Decentralized Chat",
"Create New Profile", and "I Already Have a Profile". The next page asks for
a display name and a profile photo, links to `https://delta.chat/gdpr`, and
calls `init_transports` on `deltachat-rpc-server`. The menu can set a proxy
URL and a team profile. "I Already Have a Profile" restores a backup or
copies a profile from a pasted second-device code. "Use Other Server" lists
chatmail relays, takes a classic email and password, or accepts a pasted
server code.

The onboarding is built from tdesktop's own widgets and styles
(`Ui::RpWidget`, the intro cover, `introTitle`, `introNextButton`,
`introName`, `UserpicButton`, `GenericBox`, `PopupMenu`), not plain Qt
widgets or stylesheets. It compiles and links; it has not been looked at on a
screen yet.

## Messaging

A configured account opens the Telegram chat list. Chats, history, and send
go through `deltachat-rpc-server` (`tdesktop/Telegram/SourceFiles/delta/`).
The UI still uses Telegram widgets. `ApiWrap`, `History`, and `PeerData` are
still in the binary. New send and list paths are answered in
`Delta::AnswerRequest` instead of MTProto.

Working in the client:

- Welcome and add-profile, including a back button when an account already
  exists. Profile rows show the Delta Chat photo and name. Right-click can
  move a profile to the top or delete it (`stop_io`, then `remove_account`).
  The last profile cannot be deleted.
- Chat list keeps empty chats. The left column does not collapse to a strip.
- Text, articles, photos, and voice. Pasted screenshots are copied into an
  owned file before send. Chat images are decoded with `Images::Read` and
  painted when the core file is downloaded. The stock `[Image – …]` size line
  is not shown once the photo is attached.
- Reply, edit, forward, archive, and delete. A forward replaces the local
  sending bubble and shows "Forwarded from".
- Device messages is a channel: no call button and no composer, mute and
  unmute only. Its welcome image stays visible.
- Settings header uses the profile photo, a larger name, and a relay count
  ("1 relay" / "N relays").
- Settings → Advanced → Relays lists transports. A row opens an Edit Relay
  box (email, password, More Options for IMAP/SMTP, Delete, Log In) and
  saves with `add_or_update_transport`. Add still pastes a relay QR.
- Settings → Add Second Device uses `provide_backup` and a QR.
- Attach menu has Apps and no Poll. Apps opens the store URL inside
  Telegram's in-window web panel. The URL is Settings → Advanced → App
  picker URL, default `https://apps.testrun.org/`, stored in
  `~/.config/delta-tel/app_picker_url`.
- The hamburger menu does not have My Profile. The profile photo menu does
  not have Set Public Photo. Avatar changes go through core `selfavatar`.

## Not done

- Voice playback bubbles, service events, hiding the settings email,
  and replacing the whole chat list when the profile switches are not finished.
- `MTP*` types are still the in-process message model. Do not add new MTProto.
