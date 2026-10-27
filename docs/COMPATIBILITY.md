# Experimental compatibility tracker

Real Telegram accounts are required alongside Chatmail profiles. Source now
contains persisted backend identity, account-type selection, restored donor
MTProto dispatch/connections, and session-scoped UI/data routing. These changes
link and pass the complete native run at `227cfc4` (994 checks in 24 targets,
zero failures/skips). Its artifact hashes/provenance match. Native credential-free
checks cover compact backend choice, Telegram country lookup and phone-login
entry. A subsequent welcome-copy correction still needs linked/render validation.
These results do not establish Telegram authentication, session recovery, feature
compatibility, or mixed authenticated account isolation.

Last reviewed: 2026-10-27. Deltagram remains experimental. Core provides
Chatmail protocols and encryption; Telegram uses the donor backend. Retaining
a widget or passing a helper test does not establish its runtime behavior.

The feature categories below use the
[Delta Chat platform feature overview, interface issue #39](https://github.com/deltachat/interface/issues/39)
as a reference. That issue compares official Delta Chat clients and is an
incomplete overview, not a Deltagram test report or a roadmap. Its checkmarks
are not copied here. Deltagram evidence comes from [PROGRESS.md](PROGRESS.md),
[VALIDATION.md](VALIDATION.md) and the local Core bridge implementation.

## Status meanings

- **✅ Locally checked:** an isolated Core or GUI check is recorded. This does
  not establish delivery between real accounts or cross-client interoperability.
- **🟡 Partial:** some behavior is implemented, with missing pieces or checks.
- **❌ Unavailable:** explicitly disabled or recorded as unfinished.
- **❓ Unverified:** no adequate Deltagram compatibility check is recorded.

## Current readiness gates

The last verified complete native artifact is `227cfc4`, with 24 Qt suites,
994 checks and zero failures/skips, verified hashes and matching source provenance.
It resolves the prior Qt 6.11 RPC-lifetime CI failure. Native replay confirms both
backend choices and wrapped copy at 380x480 in screen-reader mode, then fetches a
fresh 232-country Telegram catalog and reaches phone login without credentials.
Welcome copy incorrectly claiming the app is official Delta Chat is corrected
in source, with language generation/native syntax checks; the new copy still
needs a linked/render check. RTL, scale, localized/error layout remain open.

Earlier `ef176e9` replay opens Core, adds an Unspecified slot, then selects Telegram
and reaches phone login with a DC connection. Cold restart preserves the offline
Core fixture and both backend sidecars. Core mail IO was suppressed. Authorized
session recovery and mixed message-ID isolation remain unproven. Periodic
unsupported Core background requests remain unattributed. Historical `bd1ea60`
viewer/cancellation evidence applies only to that revision. The forgotten-passcode
guard passes focused Qt tests and an exact reset-method recorder.

Historical Core-only replay covers encryption information, onboarding avatar
layout at tested LTR sizes, startup Loading/Retry/Open Profile, group-sidebar
open/reopen/switch/resize, search failure/retry, restore/retry-open, and profile
name/deletion. See [VALIDATION.md](VALIDATION.md) for exact revisions and limits.
The sidebar metadata storm did not recur. RTL/scale, broader startup cases,
cross-window setup/teardown, latest-head regression replay and final packages
remain open.

Live Core profile setup also retains its shared RPC through mutation replies
and error readback. Thirty-two real-process sanitizer checks and an offline pinned-
Core wrong/correct-passphrase owner-loss retry pass. Native onboarding teardown,
first-run cancellation and actual second-device transfer remain unverified.

Release gates include current native linking, real Telegram authentication and
session persistence, mixed-backend account isolation, and donor feature replay.
Production Telegram API configuration is explicit; test-API binaries are
refused by production packaging/publication guards. Real production credentials
and login have not been validated.

PR builds compile and smoke-test; they do not execute the workflow's optional
package-verification step. After the final source is fixed and built, one
serialized unpublished validation run must also check the portable runtime on
Debian 13 and Ubuntu 22.04 and the Debian/RPM/Arch payloads and metadata.
Artifact hashes and source provenance must match the tested revision. The
unverified feature areas below remain open independently of those build gates.

## What works in local checks

| Capability | Status | Evidence and limits |
| --- | --- | --- |
| Core account creation and chat-list loading | ✅ Locally checked | Qt/Core RPC self-test creates a disposable account and loads its list without mail IO. |
| Contacts and contact deletion | ✅ Locally checked | Native `2908654` passes private alias set/clear with one write per action, rejected and confirmed contact-only deletion preserving the chat/draft, and rejected/confirmed optional deletion of a new synthetic chat. The checked confirmation removes only that fixture and unselects it; deletion persists across restart with no repeated write. Original profile data is preserved. The newer deferred contact-open path now rechecks list lifetime and profile identity after the RPC reply and again at timer dispatch; the exact-method Qt recorder passes 19 assertions and native syntax checks pass. Broader lost-ack/profile-switch UI cases remain unverified. |
| Shared contact cards | ✅ Locally checked | Send/import, card photo and profile navigation checked. Core does not import vCard NOTE biographies; available Core contact status is displayed. |
| Group creation | 🟡 Partial | Name/photo and member selection persist through Core. A disposable two-member group exchanged messages with official Desktop 2.62.0 on `44a91b5`; its persisted first-message info notice remains while Core confirms promotion completed. The wrong sender-profile Message destination reproduced there is corrected and passes native replay on `e669e85`, with the original draft restored. Later permission/request-UI changes still need native replay. |
| Invitation links | 🟡 Partial | Contact/group/channel previews, confirmation and navigation checked locally. Fresh disposable GUI onboarding and QR-image SecureJoin passed against official Desktop 2.62.0 on `3c2def3`. Physical camera scanning remains unverified. |
| Channel profiles | ✅ Locally checked | Adaptive page/modal navigation, centered cover, large photo preview, contained action menu, description links, photo/name/description updates, mute control and Core media counts. Media rows open the selected message. |
| Broadcast channels | ✅ Locally checked | Creation and subscriber write restrictions checked. Owner counts are Core read receipts, not public view totals. |
| Message information | ✅ Locally checked | Context action and Core-backed details checked. |
| Reactions | 🟡 Partial | Live GUI replay on `35f036c` confirms bidirectional reactions, rejected-change recovery, hidden transport-message filtering and group counts without invented avatars. Reactor-list browsing is unavailable. |
| Profile sidebar | ✅ Locally checked | Optional experimental toggle replaces the folder strip with profile photos/names. Switching, active colors, immediate toggling and restart persistence checked locally. |
| Profile ordering | ✅ Locally checked | Core RPC ordering and UI feedback recorded. |

## Partial implementation and known gaps

| Capability | Status | Remaining work or validation |
| --- | --- | --- |
| Text, images, files, videos and forwarding | 🟡 Partial | Live Core API checks passed bidirectional text and byte-identical file delivery against Delta Chat 2.62.0. MP4 sending, GUI forwarding and local playback have earlier local checks. `3c2def3` passed bidirectional GUI text, explicit partial download with rejection, byte-identical GUI saving, and anonymous forwarding. Save As defaults were corrected in `6a6d60d`; broader video/staging checks remain. |
| Delivery and read indicators | 🟡 Partial | A live cross-client Core read receipt passed. The `9397be0` GUI displays a live read receipt; `d05d666` passes event-before-ack reconciliation. Multi-recipient behavior needs validation. |
| Quotes and replies | 🟡 Partial | A live Core reply retained its quote across clients. Complete GUI create/display/jump behavior remains unverified. |
| Message editing, deletion and chat archiving | 🟡 Partial | Live Core edits retained message IDs and remote deletion passed. Rejected deletion preserves messages in verified GUIs. `9397be0` passes plain-text GUI edits in both clients; Core-aware edit eligibility passed on `e846f9b`; immediate quote retention after deletion passed on `8323369`. Broader deletion paths remain. |
| Voice messages | 🟡 Partial | Native waveform, duration, play/pause and seeking are implemented. Ordinary GUI recording on `49454ee` passed preview, rejected storage, explicit retry, restart, quoted delivery and byte-identical playback in official Desktop 2.62.0 using isolated virtual audio. Both clients displayed the 21-second duration. Quoted voice-plus-forwarding also passes rejected-send/restart/delivery and reference playback on `5182d5e`. Physical audio devices and round-video capture remain unverified. |
| File backups | 🟡 Partial | Core-only Advanced Settings export is implemented with selected-profile ownership, private per-attempt directories, optional confirmed database passphrase, and serialized/stale-safe callbacks. Twenty-two export-helper and five real child-process privacy checks pass, plus an unprivileged actual DeltaRpc/Core export/import round-trip with matching IDs/drafts/blobs and 0600/0700 output modes. Normal quit/restart waits for terminal export completion; sixteen exact-method recorder assertions pass, while native quit/restart replay remains pending. Profile deletion and export are mutually exclusive; twenty exact Bridge-method assertions pass. RPC ownership survives originating-session release through terminal completion; ten real-process sanitizer checks and a pinned-Core owner-loss export/restore pass. Core passphrases do not encrypt attachment files. Native export dialog, live-IO/background completion, teardown and package replay remain open. |
| Forgotten local passcode | 🟡 Partial | Chatmail or unknown locked storage preserves profiles and the lock. Guidance requires the passcode, previously enabled system unlock, or an existing backup restored in a fresh installation. Confirmed Telegram-only storage keeps donor logout recovery. Focused Qt/filesystem and reset-method checks pass; locked-start and system-unlock GUI replay on the latest native binary remain pending. |
| Multiple profiles | 🟡 Partial | Switching clears reused histories, drafts, pending messages and attachment caches. Core draft storage and asynchronous callbacks are scoped to the account. `3c2def3` passed distinct drafts, red/blue images, two-/four-second voice clips, and playback/search reset across overlapping message IDs. |
| Profile and group photos | 🟡 Partial | Settings image selection/crop/save and account avatar colors checked locally with Core; group checks recorded. Complete camera workflows and live photo distribution need checking. |
| Onboarding and relay settings | 🟡 Partial | Fresh GUI creation, repeated-submit protection, fail-closed discovery, QR-image join and live exchange passed on `3c2def3`. Native `122a3ec` passes plain restore, preserved draft/photo, retry-open and bidirectional delivery; timed actions during held import issue no duplicate setup operation. Native `a19d9e2` passes delayed discovery, selection-error Retry and opening-error Open Profile without allocating a profile. Offline imports interrupted at measured blob-extraction and SQL-write checkpoints recover on the same unconfigured destination and preserve another restored profile. SQL follow-up compares every non-device message column and passes database integrity checks. Encrypted import now has an optional masked passphrase prompt in source; helper and real-Core wrong/correct-passphrase retry checks pass. Native prompt/cancel/retry, exhaustive interruption stages, GUI/cross-window teardown, broader startup cases and RTL/scale layout remain open. |
| Invite QR scanning | 🟡 Partial | Camera capture, QR image files and clipboard screenshots feed Core invites for an explicit Join. Image decoding is checked locally; physical camera capture needs hardware validation. |
| Second-device setup | 🟡 Partial | Full transfer, restored image bytes and bidirectional synchronization passed on `27656b4` against official Desktop 2.62.0. Early/ready cancellation, duplicate starts and a profile switch during preparation passed. Camera/mobile receive paths remain unverified. |
| Contact status and biographies | 🟡 Partial | Available Core contact status displayed; imported vCard biographies are unsupported by the current Core snapshot. |
| Contact requests | 🟡 Partial | Native `4b5e093` passes 451 checks. Disposable GUI replay covers request inspection/actions/restart, pending sender Message without acceptance, accepted-contact opening, and one-shot lost-ack Group Delete with an unverified GUI outcome. Drafts/history and unrelated requests are preserved. Mailing-list/broadcast cases and final-head replay remain open; Core lookup/create atomicity remains unresolved. |
| Search | 🟡 Partial | Core result IDs feed paged global/in-chat search. `3c2def3` passed 60 matches and final-page navigation. Native `2908654` passes archived Device messages naming, metadata Failed/Retry and read-only opening. Native `122a3ec` passes initial-ID failure/keyboard retry and stale-query error suppression. Native `a19d9e2` passes profile-switch cancellation and offline 61-hit load-more failure/retry. HTML fixture search finds the plain representation; HTML-only remainder is not indexed by Core, and native No Results matches that contract. |
| App picker and webxdc | 🟡 Partial | Embedded app store exists; host rendering, draft preparation and synchronized webxdc updates are unverified. |
| Calls | 🟡 Partial | Chatmail calls remain unavailable and are blocked before call start. Telegram donor call paths and pinned v7.2.10 conference E2E source/dependencies are restored and pass syntax/type checks; the tde2e wrapper and primitive checks pass separately. Current full native linking, Telegram calls, physical media devices and mixed-account call routing remain unverified. |
| Service-event presentation | ❌ Unavailable | Recorded as unfinished in the development log. |
| Donor export, sharing restrictions and auto-delete controls | ❌ Unavailable | These Telegram-only actions are hidden/guarded; no Core-backed implementation is claimed. |
| Settings privacy presentation | 🟡 Partial | Hiding the settings email remains unfinished. |
| Local storage and cache limits | 🟡 Partial | `14878ed` Clear all preserves all 56 fingerprinted Core blob files plus drafts/history through restart and subsequent photo rendering. Verified `a64895c` renders all four Core-specific cache strings. Selective clearing and limit enforcement remain unverified; limits do not cover Core account blobs. |
| Blocked-user settings | 🟡 Partial | `44a91b5` passes 259 native checks and disposable GUI rejection, lost-ack block/unblock, restart, incoming-message suppression/restoration, draft/history preservation and profile-switch cancellation against official Desktop 2.62.0. The group-member profile action additionally passes rejection, lost-ack block/unblock and restart on `e669e85`, preserving both drafts, group history and four request fixtures. Contact-status and latest-head replay remain pending. |

## Compatibility checks still needed

No complete Core-backed Deltagram checks are recorded for these categories.
They are **unverified**, rather than assumed to work because the donor UI has them:

| Feature area | Status | Checks to record |
| --- | --- | --- |
| Media browser | 🟡 Partial | `cacc5ce` passes a real partial-photo placeholder upgrade, two-photo previous/next, Go To Message, and the Photos grid. Rejected viewer deletion preserves the photo; a new confirmed deletion removes only that fixture from history/grid and preserves unrelated data. Combined media types and final-head replay remain unverified. |
| Chat management | ❓ Unverified | Clear history rejection/success and date-range deletion have targeted GUI checks; bulk read state and last-read positioning still need coverage. |
| Member profiles | 🟡 Partial | Native `2908654` passes modal group information, the correct member list, inspection without mutation, and explicit Message to the correct existing chat with its original draft. Native `a19d9e2` passes sidebar open/reopen, group switch and narrow/wide resize restore without the former metadata loop. Broader member-management behavior remains partial. |
| Attachments | 🟡 Partial | Ordinary single-file recovery, prepared/resized/rotated image delivery, uncompressed bytes, private staging cleanup and generated archives have revision-specific checks below. Prepared-image restart/retry passes with an older quote after the Core lookup fix. Multiple-file/video paths and broader prepared-media recovery remain open. |
| Notifications | 🟡 Partial | `94ad667` passes native/package checks and 211 focused checks. Disposable replay verifies inactive-profile delivery/counts, collapsed and sidebar badges, mute persistence/suppression, and rejection of stale notification actions across switches/restarts; valid activation/read actions work. A new Core mute read fixes the reproduced post-expiry notification loss; rejected reads preserve messages without popups, and late reads cannot notify another profile. Native/package validation at `a6d878d` passes 226 checks. Disposable inline-reply recovery covers rejected sends, lost send/storage acknowledgements, restart, exact text recovery and composer preservation against official Desktop 2.62.0. `148156d` fixes the reproduced 15 ms popup loss during dialog refresh and passes native/package checks plus fault-free inline delivery, notification cleanup, draft preservation and muted suppression. `d428416` passes native/package checks, rendered notification/settings-search cleanup and direct-to-Core inline delivery, restart and draft-preservation replay. Unsupported notification defaults and subscription search entries are removed. Idle expiry, inactive-profile popups and desktop-shell rendering remain open. |
| Accessibility and localization | ❓ Unverified | Screen readers, keyboard navigation and RTL layouts. |
| Appearance | 🟡 Partial | General wallpaper: local gallery and custom images in Settings → Chat Settings. Custom-image persistence checked across chats, profiles and restart; per-chat wallpaper is unavailable and removed from the chat menu. `14878ed` opens the local theme editor and exports a valid palette/background archive through its primary Export action. Theme import and full day/night combinations remain unverified. |
| External integration | ❓ Unverified | Camera capture and sharing into/out of the application. |
| Email presentation | 🟡 Partial | Native `bd1ea60` opens full Core HTML explicitly, preserves selection/copy, scrolls long text, confirms external links and cancels pending Loading without reopening. Remote resources issued zero requests in the checked listener replay. Per-message `~` author/quote presentation and differing-author grouping are implemented in newer source; native render/click/quote replay remains pending. Contact identity stays separate. Core search intentionally excludes HTML-only/truncated remainder. Important subjects already occur in `text`; raw subject is viewer title only. |
| Message content | ❓ Unverified | Stickers, copying links and complete media rendering. |
| Trust information | 🟡 Partial | Core contact/chat encryption information and key-contact flags are implemented in source, without inventing Telegram verified badges. Typed lookup and mutation checks pass; final native fingerprint/name/delete replay remains pending. |
| Group duplication | ❓ Unverified | Cloning a group with correct Core membership semantics. |

## Client integrity verification (2026-10-24)

See [VALIDATION.md](VALIDATION.md) for revision-specific build, focused-test,
Core API and GUI evidence. Full native/portable/package CI passed at `ff64414`,
including all 33 focused Qt cases and complete Debian 13/Ubuntu 22.04 runtime
checks. Its GUI/Core/symbol artifacts were checksum-verified before live replay.

Disposable live GUI checks cover account/media isolation, crash-free repeated
profile switches, paged global/in-chat search, explicit partial downloads,
rejected Clear history, date-range deletion, send acknowledgement races,
reaction rendering, configured-onboarding recovery, anonymous forwarding and
standalone quotes. Fresh provisioning and QR-image SecureJoin passed against
official Desktop 2.62.0. Plain text, read receipts, edits, quoted replies and
remote deletion work through both GUIs on the recorded artifacts.

The `8323369` GUI preserves rejected text alongside newer drafts and recovers
late failures into the original profile after switching. Quote text survives
immediate deletion of its original. Delayed dialog/edit callbacks and Core exit
are handled without a crash; an already persisted draft returns after restart.
The `ff64414` normal-text composer keeps Core's durable draft until handoff.
Live replay passes rejected-send quit/restart with quote retention, accepted-send
quit before acknowledgement without duplicates, committed-send error replies,
quit during persistence, pending-send profile switches and Core-crash recovery.
The independent reference GUI displays the accepted texts and quote. Combined
forwarding and attachment-send durability and the broader gaps above remain
open. No production readiness or complete Delta Chat feature parity is claimed.
Existing unscoped draft files are retained without loading into another profile.

`be7b043` passes native/portable/package CI and 41 focused cases. Disposable GUI
replay on 2026-10-25 verifies ordinary single-file restart recovery after deleting
the original file, explicit retry, rejected/successful removal with caption
retention, uncertain send replies, accepted-send quit, profile isolation and
Core-process failure recovery. Official Desktop 2.62.0 received the accepted
attachments and caption-only text. Multi-file batches and transformed-media
durability remain open; this does not establish complete media compatibility.

`8e37b6c` passes native/portable/package CI and disposable generated-ZIP replay:
delayed Core reads, a profile switch while the request is pending, matching
filename/content/bytes in official Desktop 2.62.0, cleanup after acceptance,
and retention followed by explicit deletion after rejection. Background media
preparation guards have focused tests; live interruption of preparation itself
remains unverified. Byte-backed staging cleanup, private permissions, second-device dialog
start/cancel guards and a full transfer passed native/live validation on `a1cbe2a`.
The `27656b4` early-cancellation fix passes native/package checks, all 49 focused
checks and disposable GUI replay, including a repeated complete transfer and
mail exchange. A rejected clipboard image still disappears after quit/restart
with no Core draft, as reproduced on `a1cbe2a`. Failed/pending status display is
corrected in `2e051d8`, which passes native/package checks and GUI display
fixtures for Core failure/pending/read states plus local image rejection.
Assistive-technology runtime coverage remains open. The presentation fix does
not repair transformed-media durability.

`21bbd2a` passes native/portable/package validation and all 50 focused checks.
It removes a separate preparation-time clipboard copy missed by the earlier
uploader-path checks and sends prepared/resized photo bytes instead of original
encoded clipboard bytes. Disposable live replay confirms private staging,
cleanup after acceptance or explicit removal, and matching resized/rotated
images in official Desktop 2.62.0. `703ae5b` also passes native/package checks
and preserves a 5 MB JPEG through the explicit uncompressed path. Core accepts
`File` to skip recompression, then classifies recognized images as `Image`;
document rendering is not guaranteed. `f751489` clarifies the English option
label and passes native/package validation plus single/multi-image label checks.
A deterministic archive race reproduced silent omission of a deleted selected
file, and its rejected send exposed a chat row that kept a pending clock until
repaint. Fixes `a2b2b61` and `1a3017c` pass native/package checks and live replay:
the missing-file race aborts before any send, a healthy archive arrives intact,
and a rejected image updates the row immediately. `10cd8fe` marks preparation
failures explicitly; `d25dcee` reports staging-write failures for items that
bypass the upload queue; `c9962d2` clears stopped upload state. The combined
`c9962d2` build passes native/portable/package checks and all 50 focused checks.
Its verified GUI shows Failed and supports explicit Delete for both the archive
race and an unwritable image-staging path, without sending either. After staging
is restored, a newly selected image arrives intact in official Desktop 2.62.0
through the same GUI session. Failed-image preview/recovery is not established.
Those failure-state checks alone did not establish prepared-media restart recovery.

`7f95cca` passes native/portable/package validation and 56 focused checks. Its
verified GUI retains a rejected prepared image and caption across quit/restart,
then delivers one explicit retry with byte-identical prepared JPEG content in
official Desktop 2.62.0. Temporary staging is cleaned and other profiles remain
isolated. The replay also exposed a jump to old history after recovery; the
`252bfa0` fix selects the actual chat end and passes native/package validation
and uncompressed-image restart/retry replay. Original JPEG bytes reach the
reference client unchanged, with the new message visible at the chat end.
A separate lost-save-ack/restart case retained the quote in Core but dropped it
on explicit retry because its older message was not loaded in the GUI. The
Core-backed lookup and quote-preservation fix `7f25d36` passes all 85 focused
checks, native/portable/package validation and live restart/retry: the reply bar
restores, the outgoing message retains its quote, and official Desktop 2.62.0
shows the original quote with matching image bytes. A rejected quote lookup
keeps the reply through caption edits; explicitly removing it sends without
the quote while preserving the attachment. A delayed lookup cannot restore a
reply removed while it was loading. A generated ZIP also passes rejected-send
restart/retry on `7f95cca`, with identical archive bytes and contents in both
clients. `252bfa0` also verifies failed pre-save attachment retention, blocked
normal quit/profile switching, and explicit Delete cleanup/unlock with caption
retention. A prepared send with a lost acknowledgement reconciles once and
survives restart with no duplicate; both clients retain the prepared image bytes.
Direct retry before Core ownership passes native validation and disposable
unquoted-image replay on `7d64366`: repeated save rejection retains private
prepared bytes, a delayed Retry blocks duplicate actions, and one send delivers
identical bytes to both clients. Normal restart retains one message and no draft.
A separate quoted Retry setup exposed a reproducible search-result crash while
loading saved recent peers. `490cadd` passes native/package checks and opens that
same result with the original cache preserved. Quoted Retry then delivers the
older quote and identical image bytes to the reference client, but Deltagram's
accepted bubble disappears from its open history until restart. A follow-up
history refresh fix passes native/package checks and disposable replay on
`b5c4e9e`: the quoted image remains visible immediately after one Retry, both
clients retain the older quote and matching bytes, and the draft/staging clear.
Broader interruption and delayed-callback checks remain open.

A follow-up source change clears recent/ranked peers and open-chat navigation
on profile switch and prevents importing unscoped donor suggestion records.
Peer suggestions are currently session-local; settings-search history remains
available. `6078103` passes native/package checks and disposable profile-switch
checks: Recent and Ctrl+Tab history clear, the alternate draft stays unchanged,
and the pending queue remains in its original account. Active-popup races and
malformed-record runtime cases remain unverified.

`ca039dc` adds Core-backed batch compose storage, and `8a2b78c` adds durable-send
checkpoint continuation and verified-ID resume without replacing a Core draft.
`47f5752` adds checkpointed advancement and recovery of known draft/outgoing IDs;
`ed427be` preserves newer text/quote drafts and rejects advancement during active
draft operations. All 116 focused checks pass, including queue storage faults,
stale actions, account isolation, real-Core rejected-send restart, and discard
after restart while the second queued file retains its bytes.

Saved-queue review and serial Send remaining controls are wired through the
attachment button in `99df1ac`. Confirmed Discard next in `8c41b45` removes only
the reviewed entry and its matching Core draft, preserving newer drafts and
already-sent messages. `490cadd` passes native/package checks and disposable
recovery from a directly seeded two-file ledger: rejected send, normal restart,
same-ID resume, ordered byte-identical delivery to both GUIs and queue/pin/draft
cleanup. A separate GUI case preserves a newer draft through rejected queue
advancement and both confirmed discards; the remaining file retains its bytes.
`6078103` additionally preserves a matching draft/queue after rejected removal,
and reconciles a committed removal with an error acknowledgement through readback,
clearing only the first entry and matching draft while retaining the second file.
The new batch coordinator preserves preparation order and source owners, waits
for the entire selection, and reconciles uncertain publication without sending.
Real-Core testing exposed blob renaming when a temporary path and display name
have different extensions; staging now privately copies under the intended name
before Core import. The coordinator and this storage fix pass native/package
validation on `8268add`, including restart after original-file removal and
private-copy cleanup. Follow-up source wires new batch selections, preparation
failure review, explicit retry/discard, and saved-queue discovery on chat open.
It waits for prior composer writes, checks for existing attachment drafts, and
retains pending selection metadata. A follow-up fixes recovery when Core recodes
an image after its draft checkpoint: the same confirmed outgoing message can
advance the queue despite its changed blob, while unsent draft identity stays
strict. A disposable Saved Messages probe confirms Core's recoding, and the
153 focused checks include no-resend recovery, mismatched-message rejection and
queue mutation during a held discovery read. Discovery uses a read-only snapshot
so it cannot reserve the mutation lock, and rejects superseded UI replies.
The batch GUI revision `7074268` passes native/portable/package validation and
disposable GUI selection/rejected-send/restart replay. With both original files
removed, explicit recovery delivers them once in order to official Desktop
2.62.0 with matching bytes and caption, then clears queue/pins/draft while
preserving the alternate profile. Subsequent recovery, discovery and
recording changes pass native/portable/package validation on `49454ee` and
`5182d5e`. Ordinary recording passes virtual-audio capture/preview, rejected
storage, restart and quoted cross-client playback on `49454ee`. Seeded-ledger
image recovery on `5182d5e` handles Core recoding plus rejected queue removal
across restart without another send. Held discovery on that GUI allows a
foreground send to finish and ignores the stale queue snapshot afterward.
A separate `7074268` storage-rejection
replay preserves the pending batch, blocks normal quit, and recovers its files
and caption through explicit Retry. Confirmed discard then clears both entries
without delivery or changes to the alternate profile's draft. Broader
interruption/profile cases need live coverage. The review
dialog's clipped Cancel button and one-item count wording pass native visual
replay on `b5c4e9e`.

The generic queue backend now supports text, prepared files and individual
forwards as one durable plan. It persists a forward attempt before calling Core,
requires explicit review/discard after an uncertain result, and never resends a
confirmed forward. All 182 focused checks pass, including source isolation,
interrupted replies and exact publication reconciliation. A disposable Core
probe confirms that a multi-source forward can partially commit before returning
an error. Follow-up source connects composer text, files, recordings and direct
forwarding controls to this backend, removes the MTProto-shaped forwarding
adapter, and exposes explicit review/discard for uncertain forwards. The 184
focused checks and native/portable/package validation pass on `5182d5e`.
Disposable GUI replay delivers a text prefix and two forwards once and in order
to official Desktop 2.62.0, preserving file bytes, caption and forward markers.
Rejected initial send survives restart; an accepted forward with a lost
acknowledgement remains uncertain across restart and cannot be resent from
review. Explicit discard preserves that delivered message before the tail is
sent. The alternate profile draft remains intact. Additional `5182d5e` replays
pass a selected file plus forward after original-file removal, and a quoted
voice recording plus forward after rejected send and restart. Both deliver
once in order with matching bytes; reference voice playback passes. Broader
forwarding interruption/profile cases and round-video capture still need replay.

## Updating this tracker

Record the source revision, client versions, environment and test scope before
promoting a feature. Distinguish disposable local checks from tests with real
mail IO and another Delta Chat client. Include failure cases and account-switch
behavior. Update the README summary when a known gap changes.

`make test` checks source hygiene and offline regressions; it is not a full
compatibility suite. The Qt/Core smoke test is documented in
[DEVELOPMENT.md](DEVELOPMENT.md). The complete GUI release now links with the
locked toolchain. Portable runtime and distribution package checks are recorded
in [VALIDATION.md](VALIDATION.md). Successful builds and startup checks do not
establish live messaging or complete feature compatibility.
