# Publication validation

## Client integrity checks — 2026-10-24

Heavy native builds run remotely, one at a time with four compiler jobs. No
heavy local builds run in parallel; Ninja records and ccache are preserved.
Commits and fork CI use `mamaru-a`. No production release has been published.

The ordinary-attachment native/portable/package CI passed at `be7b043`, including all 41
focused Qt cases (43 including setup/cleanup, no skips):
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40069205736).
Its GUI and Core binaries were downloaded selectively and checked against the
artifact SHA-256 manifest; debug symbols were not downloaded. Disposable GUI
replay on 2026-10-25 verifies ordinary single-file handoff and recovery as
recorded below. No heavy local build was run.

Native compilation, all 33 focused Qt cases (35 including setup/cleanup),
portable runtime checks on Debian 13 and Ubuntu 22.04, and Debian/RPM/Arch/tar
package verification passed at `ff64414`:
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40064157870).
The GUI, Core and symbol-file SHA-256 checksums were verified before disposable
live replay. The preceding `b8e700d` run caught a strong message-ID conversion
error; `ff64414` fixes it. Builds were serialized and compiler caches preserved.
The earlier `374244e` run completed both portable runtime dependency checks.

Live tests use isolated disposable profiles **Deltagram integrity A**,
**Deltagram integrity alternate**, **Deltagram integrity onboarding**,
**Deltagram integrity recovery**, and **Delta Chat reference B**.
The development GUI reports 0.2.1 beta with pinned Core 2.63.0-dev
(`8468f5b5dd603da09d81146abbc52aad48604c6c`). The independent reference client is
official Delta Chat Desktop 2.62.0. No passwords or account addresses are included
in this report. The supported build target is Linux x86-64; GUI checks use
Ubuntu 24.04 under Xvfb.

### Blocked-contact controls — 2026-10-26

The Blocked users settings section now uses Core's blocked-contact list and
block/unblock operations, including the contact picker and direct-contact
profile action. Changes validate the current contact state, submit once and
read back Core state before reporting success. Unknown outcomes require a
refresh; switching profiles invalidates pending callbacks. Chat-list refreshes
reject older snapshots and remove conversations absent from Core's current
list without deleting history or drafts. A confirmed block closes that
conversation if it is open.

The serial focused Qt target passed **259 checks**, with no failures or skips,
including 33 new blocking cases. These cover invalid or duplicate list entries,
rejected writes, lost acknowledgements, unreadable state, stale confirmations,
profile switches at each RPC boundary, and isolated real-Core profiles across
restart. The real-Core case confirms block/list/unblock and preserved drafts;
it also caught and fixed handling of the nullable chat lookup for hidden chats.
The source audit checked 13,223 files with no blocking findings. Native/package
validation and disposable GUI compatibility replay for this change are pending.

### Unsupported passkey controls — 2026-10-26

On the `d428416` disposable GUI, Settings search for “passkey” offers both Add
Passkey and Passkeys. These controls use Telegram authentication APIs and have
no Core implementation. Core profiles now omit both registrations and the
privacy-panel button; legacy passkey links return to Privacy and Security.
Direct creation-box entry is guarded. Opening privacy settings also no longer
starts the unused Telegram auto-delete reload. Rendered verification and native
compilation of this follow-up are pending.

The same baseline GUI exposes “Disconnect all websites”, “If inactive for” and
“Terminate all other sessions” when searching for “terminate”. Core profiles
now exclude the donor sessions, connected-websites, folders and global
Telegram auto-delete sections from search. Their constructors stop before
loading donor data. Standard settings navigation checks section availability
(including parent sections), so a saved URL to an unavailable section returns
to the supported main panel. Core blocked contacts, local passcode and device
preferences remain registered. Rendered search/link replay is pending.

### Sender and chat identity lookups — 2026-10-26

Review of the blocking profile action found that high sender-peer IDs could be
narrowed to an unrelated chat ID during type/status lookup. The blocking action
now resolves known sender contacts before checking chat-only system IDs, and
the group/direct/channel/subtitle caches retain full-width peer IDs. Source
inspection confirms these lookups no longer truncate sender identities; native
compilation and group-member profile replay remain pending.

### Notification reply recovery — 2026-10-26

On `94ad667`, the disposable native notification receiver emitted one inline
reply while the proxy rejected `send_msg` and delayed its error. A normal quit
and restart lost the reply text: there was no outgoing message or recovery
entry. The existing conversation draft and the alternate profile's draft
survived. This was a native notification action test on the private session
bus, not a physical desktop-shell popup test.

Notification replies now use an independent Core `ui.*` record. The client
confirms storage before attempting the send and records the attempt before
calling Core. A missing or malformed acknowledgement leaves the text for
review; reopening it never retries an uncertain send. Confirmed messages can
be removed from recovery without deleting their conversation message. The
main menu's Saved replies view exposes recovery, copy, explicit send for
unattempted replies and dismissal. Replies do not use or replace the composer
draft. Unsaved replies prevent normal quit; callbacks and recovery actions
remain scoped to their original profile.

The serial local Qt target passed **226 checks**, with no failures or skips,
including real Core storage/restart and composer preservation. Fault tests
cover rejected storage, unreadable confirmation, lost acknowledgements,
interruption, duplicate actions, stale review, profile isolation, malformed
entries and failed cleanup. The source audit checked 13,222 files with no
blocking findings. [Native/portable/package validation at `7dd38b7`](https://github.com/mamaru-a/deltagram/actions/runs/40232290686)
passed all 226 checks. The GUI and Core checksums were verified before replay.

On that native artifact, a rejected send with a delayed error survived normal
quit and restart in Saved replies, with the original text and an uncertain
status. The detail view withheld Send; Copy preserved the exact text, Open
conversation preserved the composer, and confirmed dismissal removed only the
recovery record. A separate committed send with a lost acknowledgement reached
official Desktop 2.62.0 once. After restart it retained the same outgoing ID
and uncertain record; dismissal kept that delivered message and both drafts.
A storage rejection retained the reply in memory and visibly blocked normal
quit. The Saved replies detail retained the exact text. After removing the
storage fault, one explicit Send delivered one reply to official Desktop
2.62.0, cleared recovery and preserved both composer drafts.

The recovery-list status could be clipped next to a long chat name, so the
follow-up places status on a separate wrapping row. It also preserves the
existing read action and clears the consumed notification when replying.
[Follow-up native validation](https://github.com/mamaru-a/deltagram/actions/runs/40233985890)
passed for `a6d878d`, including compilation, all 226 checks and package
verification. It includes removal of unsupported call settings. Its checksum-verified GUI
loaded an unattempted reply whose initial storage acknowledgement had been
lost, displayed the complete status on its own row, and sent it once on
explicit review while preserving both drafts.
Official Desktop 2.62.0 rendered one copy of that reply, and the subsequent
Core read confirmed its read state with no remaining recovery entry.

Removing the artificial list delay exposed another notification defect on
`a6d878d`: a normal incoming notification was closed after about 15 ms while
the conversation remained unread. The chat-list adapter supplied an empty
donor notification default for an unmuted chat. Applying it briefly inherited
the donor's unknown/muted default and cleared the popup before Core's explicit
mute state was restored. Core-account dialog refresh now leaves notification
settings to the existing generic Core mute update. [Validation at `148156d`](https://github.com/mamaru-a/deltagram/actions/runs/40235418361)
passed native compilation, all 226 checks and portable/package verification.
Its checksum-verified GUI retained a normal notification through list refresh
without fault injection. One native inline action closed the popup after about
19 seconds, sent one reply to official Desktop 2.62.0, cleared recovery and
preserved both composer drafts. Core later confirmed the reply as read.
Muting through the GUI still suppressed the next real incoming notification,
excluded it from the global fresh list and displayed a grey per-chat unread
badge. The GUI unmute action restored the original setting. These checks use
the private notification protocol receiver; physical desktop-shell rendering
and full production readiness remain unverified.

The global Desktop notifications toggle also suppressed a real incoming popup
while preserving unread state in an unmuted chat; the setting was restored and
both drafts remained intact. An earlier attempt lost the private test RPC
connection and is excluded from the result. The fixture now publishes policy
files atomically and reports worker failures; the repeated check passed.

### Unsupported call settings — 2026-10-26

Notifications and Sounds and Devices still offered "Accept calls on this
device" through Telegram authorization requests. Devices also exposed a
separate set of call-only input/output preferences although this client does
not support calls. Those controls, their settings-search entries and the
authorization reload on opening Devices are removed. Ordinary microphone,
speaker, camera and system-audio settings remain. Native and package validation
passed on `a6d878d`. The rendered Devices panel retains ordinary output/input
and system sound preferences, without call acceptance or separate call devices.
The Notifications panel also omits call acceptance.

### Unsupported notification preferences — 2026-10-26

The native settings panel offered Telegram default notification types,
reaction preferences, contact-signup and pinned-message events. Those paths
still use donor APIs; they do not control Core notification delivery. The
badge toggles also misrepresented the app badge, which counts Core's fresh
messages from unmuted chats across profiles regardless of those donor values.
Core accounts now omit these sections and the donor multi-account toggle.
Global desktop/sound/attention controls, preview privacy and native integration
remain available. The type and reaction subsections are unavailable in settings
search and cannot initialize their donor controls through direct navigation.
Legacy notification-type links open the supported Notifications panel. A
baseline settings search also exposed Telegram Premium reaction features.
Premium, Stars and Business search registrations now respect their existing
unavailability in the main settings menu.
[Native/package validation at `d428416`](https://github.com/mamaru-a/deltagram/actions/runs/40236535237)
passed compilation, all 226 checks and portable/package verification. The
checksum-verified GUI shows only working global, preview and native notification
controls. Settings search no longer exposes the notification reaction, muted
count or call-acceptance controls; the unrelated Advanced private-chat download
result remains. Searching for sound still opens and highlights Allow sound.
The legacy private-chat notification link opens the supported Notifications
panel. Premium reaction search results are absent.

With this GUI connected directly to Core, a real incoming popup remained
available until one inline reply action. Official Desktop 2.62.0 displayed
one reply; Saved replies was empty, and the onboarding and alternate profiles
still displayed their exact original drafts. After normal restart, read-only
Core checks confirmed the same outgoing message ID in read state, no recovery
record and both drafts unchanged. The test-only native-notification preference
was restored to off and remained off after restart; desktop notifications
remain enabled. Fault policies are cleared. Production readiness is still open.

### Inactive-profile delivery and unread tracking

Cold-start replay on `5182d5e` selected **Deltagram integrity alternate** while
official Desktop 2.62.0 sent one message to **Deltagram integrity onboarding**.
The inactive profile stayed disconnected and had no local copy. Starting that
profile's IO through the private test control immediately received the waiting
message without changing the selected profile. Its fresh-message list contained
the new message, but the native profile menu displayed no unread indicator.

The follow-up starts Core IO for all profiles and tracks each configured
profile's fresh messages before the selected-account event filter. Counts use
Core's notification list, which excludes muted chats and contact requests.
Profile rows, the optional profile sidebar, the collapsed profile selector and
the app badge use account-scoped counts. Reads coalesce per profile; events
during a read invalidate its snapshot, removed/reused accounts reject old
callbacks, and failed/malformed reads preserve existing counts. No tracking
request selects a profile or marks a message read.

All 195 focused checks pass with no skips, including 11 new activity cases.
Native compilation, portable runtime and package verification passed at
`71b644c` in [CI run 40222960018](https://github.com/mamaru-a/deltagram/actions/runs/40222960018).
Its GUI/Core artifacts were checksum verified before disposable replay.

Cold start with the alternate profile selected connected all four configured
profiles. The inactive onboarding profile received a message sent while the
app was closed, without selecting that profile. Its menu badge showed two
unread messages and updated to three when official Desktop 2.62.0 sent another
message while the menu remained open. Selecting onboarding and opening the
conversation displayed all three messages, changed their Core states to seen,
and cleared its fresh-message list. The alternate profile's saved draft remained
unchanged. These checks verify the profile menu and local read clearing; the
desktop-shell badge still needs rendered verification.

The optional profile sidebar was also rendered on `132b994`. With the alternate
profile selected, a real incoming message increased onboarding's sidebar badge
from three to four. Clicking that profile switched to its conversation list;
opening the conversation displayed the messages, cleared Core's fresh list and
removed the sidebar badge. The alternate draft remained unchanged. The sidebar
override was then removed from the disposable profile's configuration.

The same replay exposed a clipped collapsed-selector badge and a separate mute
defect described below. The badge follow-up aligns its complete bounds and the
toggle arrow with the profile-name line. Native replay on `132b994` confirms
the complete badge is visible with the expected inactive-profile count.

### Notification action scope

Source review found that desktop notification targets used the donor session's
stable user ID, while Core profiles reuse that session and can overlap chat and
message IDs. Native action callbacks resolve that ID against the current view;
clearing a popup alone does not reject an already queued or persisted action.

Disposable native replay on `71b644c` confirmed this collision. A private
D-Bus notification receiver captured the real activation target for a message
from **Delta Chat reference B** to **Deltagram integrity onboarding**. After
switching to **Deltagram integrity alternate**, replaying that captured action
opened **Deltagram integrity A** in the alternate profile: both conversations
had the same numeric chat ID. The selected Core profile remained alternate,
the original message stayed fresh in onboarding, and the alternate Saved
Messages draft remained intact. The temporary chat-list delay was removed
after the check. This verifies the native action protocol, not a desktop
shell's rendered notification popup.

The follow-up assigns an opaque notification identity at backend startup and
after each successful profile switch. It clears the prior profile's queued and
displayed notifications and any delayed grouped notification before rotating
the identity. Linux, Windows and both macOS adapters use the same identity for
creation and clearing; custom popups retain their creation-time identity.
Activation, reply and action lookup therefore reject targets from the old view.
The identity is not persisted, so it also changes across app restarts.

Native compilation, all 202 focused checks, portable runtime and package
verification passed at `132b994` in
[CI run 40225949053](https://github.com/mamaru-a/deltagram/actions/runs/40225949053).
Its downloaded GUI/Core binaries were checksum verified. The new binary
rejected the old captured activation after restart. A fresh notification used
a different opaque identity; after switching profiles, replaying its activation
and mark-as-read actions neither opened a conversation nor changed either
profile's unread state. A current notification opened the correct conversation,
and its mark-as-read action cleared that profile's fresh messages. After another
restart, replaying the previously valid mark-as-read action left a new incoming
message unread. The alternate profile's saved draft remained intact throughout.

These checks cover Linux GNotification protocol/action handling on Xvfb.
Desktop-shell popup rendering, inline reply durability, and inactive-profile
popup delivery remain separate open checks/features.

### Core chat mute persistence and expiry

On `71b644c`, Core reported the reference conversation as unmuted while its
context menu offered Unmute. After selecting Unmute and then Mute forever,
the menu again offered Unmute, but a Core read still reported `isMuted=false`.
The donor menu had changed local notification settings without saving to Core.

The follow-up applies explicit Core mute state to the notification model and
routes per-chat unmute, forever and timed changes through
`set_chat_mute_duration`. It leaves the visible state unchanged until Core
refreshes it, serializes changes for each chat, rejects malformed mutation
acknowledgements, and never retries automatically. Late callbacks and old
mute-menu actions are scoped to the profile. Unsupported per-chat ringtone and
sound-only menu options are hidden. Notification-settings reads no longer
request donor server defaults while the Core backend is active.

All 202 focused checks pass with zero failures/skips. New real-Core checks use
overlapping chat IDs in two isolated accounts, verify timed/forever settings
across a server restart, and confirm unmuting one account leaves the other
muted. Invalid arguments, malformed acknowledgements and stale callbacks are
also covered. The source audit has zero blocking findings across 13,220 files.
The `132b994` native build and disposable GUI replay also passed. Rejecting a
Mute forever request displayed an error and left Core and the menu unmuted.
An explicit subsequent mute changed Core and the menu, and remained muted after
a complete GUI/Core restart. A message sent by official Desktop 2.62.0 arrived
with a grey chat unread badge, stayed out of the profile fresh-message count,
and produced no native notification. GUI Unmute restored the fresh-message
count; the next incoming message produced a native notification. The GUI's
15-minute mute action also set Core's muted state.

A short timed-mute fixture exposed a further race: Core's mute expired without
an event, leaving the cached chat state muted. With the chat-list reply delayed,
the next real incoming message appeared and became fresh, but produced no
notification even after the list refreshed. Core's own expiry-event regression
is marked ignored in the pinned source. The follow-up reads `is_chat_muted`
before applying a fresh incoming message to the notification path. Only an
explicit boolean is accepted, and profile changes invalidate pending reads.
An unreadable setting preserves the message without a popup and reports the
failure. All 211 focused checks pass with no skips, including strict boolean
parsing, invalid/stale requests and a real-Core read before and after expiry.
Native compilation, all 211 checks, portable runtime and package verification
passed at `94ad667` in
[CI run 40228978735](https://github.com/mamaru-a/deltagram/actions/runs/40228978735).
The GUI/Core artifacts were checksum verified before replay. Repeating the
short timed mute and delayed chat-list refresh now produced a native
notification for the next real incoming message. Rejecting the mute-state read
suppressed the notification while preserving the message, which was then opened
and rendered in the correct conversation. Holding a mute-state reply across a
profile switch produced no late notification; the original message stayed
unread in its own profile and the alternate draft survived. Fault injection and
the disposable experimental-option overrides were removed after the checks.

Idle mute-label/badge expiry without a new event remains open. These replays
do not establish inline-reply durability or inactive-profile popup delivery.

### Batch preparation storage checks

The follow-up generic queue backend supports a text prefix, prepared files and
individual forwards in one durable compose plan. Core's `forward_messages`
returns no message IDs and can partially commit before returning an error. A
disposable Saved Messages probe confirmed one outgoing forward after a mixed
valid-source/draft-source request returned an error; its temporary draft was
removed without repeating the request. This is Core behavior evidence, not a
new GUI forwarding pass.

Each forward now records an attempted state before calling Core. An uncertain
result remains queued for review and explicit discard, without automatic retry
or content-based guesses about message identity. A confirmed forward advances
without sending again. Source account/chat, visible membership and preview
metadata are checked, and newer destination drafts remain untouched. Batch
publication reconciliation compares forward identity and state as well as text,
files and ordering. The subsequent UI integration and live replay are recorded
below.

All 182 focused checks pass with no failures or skips, including rejected and
uncertain forward checkpoints, lost acknowledgements, destruction during a held
reply, source isolation, changed-source rejection, and ordered text/forward
advancement. The source audit has zero blocking findings across 13,217 files.
These checks include the pinned Core storage cases, but forwarding fault cases
use the RPC fixture; native and live evidence is recorded separately below.

The batch GUI revision `7074268` passed native compilation, smoke tests, portable
runtime and package verification in
[CI run 40211761527](https://github.com/mamaru-a/deltagram/actions/runs/40211761527).
Its GUI/Core binaries were checksum verified for disposable replay. Later
recoded-image recovery, discovery, recording and forwarding changes are not
covered by that native build.

Disposable GUI replay on `7074268` selected two generated files through the
native file dialog, with one caption on the second item. Injected send rejection
left both byte-identical files in the ordered queue, with the first Core draft
ID checkpointed. Both original files were then removed, and normal quit/restart
retained the queue, draft ID, caption and blob bytes. Opening the chat displayed
the saved-queue hint; explicit Send remaining delivered both files once, in
order, with matching bytes to official Delta Chat Desktop 2.62.0. Both GUIs
displayed the files and caption. Queue, pins and draft cleared, the alternate
profile draft remained unchanged, and the other profiles had no matching items.
The audit recorded two imports, two draft saves and three send calls: one
injected rejection followed by the two accepted sends. Fault injection is off.

A second `7074268` GUI replay rejected Core's blob import. The pending two-file
selection remained available with Retry/Discard, normal quit was refused with
an explanatory toast, and no draft or send was issued. Explicit Retry retained
both files and the caption, published the queue, and stopped at an injected send
rejection. Confirmed discards removed the first matching draft and then the
remaining entry; queue/pins/draft cleared, no outgoing or reference messages
were created, and the alternate profile draft stayed intact. Fault injection
was removed after the check.

A subsequent source change connects composer text, prepared file batches,
recordings and direct forwarding controls to the generic queue. It captures the
forward selection before asynchronous preflight, keeps changed selections, and
clears the donor forward panel only after the coordinator accepts ownership.
The old MTProto-shaped forwarding adapter is removed. Queue review describes
text/attachments/forwards and withholds Send remaining for an uncertain forward
until explicit review/discard. All 184 focused checks and the Linux offline
regressions pass; the additional cases cover file and voice prefixes before
ordered forwards. Native compilation, focused checks, portable-runtime and
package verification also passed at `5182d5e` in
[CI run 40218897021](https://github.com/mamaru-a/deltagram/actions/runs/40218897021).
The earlier native run for
`49454ee` passed native compilation, focused tests, portable-runtime and package
checks in [CI run 40217054282](https://github.com/mamaru-a/deltagram/actions/runs/40217054282).
It includes recording, discovery, recoded-image recovery and the forwarding
backend, but does not include the subsequent forwarding UI changes through
`eb53438`, which are covered by the `5182d5e` native run.

Disposable ordinary-recording replay on the checksum-verified `49454ee` GUI
captured a generated tone through an isolated virtual microphone, previewed it,
and retained its encoded bytes after an injected Core blob-import rejection.
Explicit Retry published a quoted Voice queue entry and stopped at an injected
send rejection. Normal quit/restart preserved the draft ID, quote and byte hash.
Explicit Send remaining delivered exactly one voice message to official Delta
Chat Desktop 2.62.0, with identical bytes and the quote intact. Queue, draft and
private recording files cleared; the alternate profile draft was unchanged.
Both clients displayed the 21-second duration, and preview/reference playback
advanced with non-silent captured output. The encoded file is mono Opus at
48 kHz. Reference playback required restarting the disposable reference client
with its virtual PulseAudio output configured; no hardware microphone or
speaker was tested. Combined recording-plus-forwarding is covered separately
below on `5182d5e`.

Disposable combined-forwarding replay on the checksum-verified `5182d5e` GUI
selected two files and added a text prefix. Rejected text send retained all
three queue entries and the draft ID across normal quit/restart. Explicit
Send remaining delivered the text, then Core accepted the first forward while
the proxy replaced its acknowledgement with an error. After another restart,
the review identified the uncertain forward and offered no Send button.
Confirmed Discard removed only that queue entry, preserving the delivered
message and remaining forward. Sending the tail completed the plan. Both
clients displayed the ordered text and two forwards, with the second caption,
matching file hashes and no duplicates. The alternate profile draft survived.
The audit recorded exactly two Core forwarding calls, one per source.

The tail send also exercised delayed queue discovery. A real queue lookup was
held while the native review loaded independently and sent the remaining entry.
Releasing that old one-entry reply after completion did not restore the queue
hint or block the composer. Clicking the attachment button opened the ordinary
file picker. Queue and draft were empty; all proxy faults were removed.

Additional `5182d5e` GUI replays cover a newly selected file followed by a
forward, and a newly recorded quoted voice followed by a forward. Rejected
initial send preserved both ordered entries before any forward call. The file
case recovered after the original input was removed. The voice case retained
the recording draft ID, quote and forward source across normal quit/restart.
Explicit recovery delivered exactly two messages in each case, in order, with
matching hashes in official Desktop 2.62.0, retained captions/forward markers,
and the voice quote intact. Reference voice playback produced non-silent
virtual output. Each replay issued one forward call; queues/drafts cleared and
the alternate profile draft remained unchanged. Fault injection is off. These
checks cover ordinary files and voice recordings; round-video capture and
physical devices remain unverified.

The batch coordinator collects prepared files by selection index and retains
their owners through Core storage. No file is queued before every preparation
finishes, and the coordinator never sends. Rejected storage can be explicitly
retried; an uncertain publication is read back and must match the entire saved
queue before ownership is released. An unrelated existing queue is not adopted
as the new selection. Preparation failures and explicit discard reject late
worker completions.

A real-Core regression exposed a filename mismatch: saving a draft can rename
a blob when its temporary path and intended filename have different extensions,
leaving the queue and pin pointing at the old path. Queue staging now makes a
private copy under the intended name before asking Core to import it. Copies
run on one background worker, retain private file/directory permissions, and
are removed after Core finishes reading. Core still owns durable bytes.

Focused coverage includes out-of-order preparation, retained owners during a
held publication, rejected copy/pin/publication, uncertain publication, changed
queue contents, conflicting selections, invalid preparations, and private-copy
cleanup. Real-Core restart cases remove both original temporary files and check
caption/order/bytes with distinct content, duplicate content, and duplicate
content under different extensions. `8268add` passes all 137 focused checks,
native compilation and portable/package validation in
[the serial CI run](https://github.com/mamaru-a/deltagram/actions/runs/40209803349).

The next source revision wires multi-file selections through the coordinator.
Existing workers prepare photos, video and archives without creating sendable
placeholders; all outputs enter one queue before ordered sending starts. The
attachment button reviews a pending batch, retries storage or explicitly
discards it, including cancellation of active conversions. Preparation failure
retains every selection's caption and quote. Composer edits, profile switching
and normal quit remain guarded until ownership is resolved. A preflight checks
for existing queues and attachment drafts before changing the composer.

Batch sending waits for earlier composer writes to finish. Settled callbacks
run after the current draft callback returns, preventing a fresh reload from
being satisfied by an older read. Focused cases hold both draft writes and
fresh reads, exercise rejection without automatic retry, and verify selection
metadata after preparation failure. All 141 focused checks pass with no skips,
and the source audit has zero blocking findings across 13,217 files.

Opening a chat now discovers saved queues and offers a visible notice plus
attachment-button/composer hints. Discovery is account/view guarded and skips
active compose operations. A follow-up removes its mutation reservation: a slow
background read no longer blocks Send, Discard or another queue review. The UI
rejects replies superseded by compose changes or a newer visit to the chat, and
refreshes after a compose change that occurred during discovery. Reviewed
snapshots remain advisory; mutations reload and validate the queue and entry IDs.

All 153 focused checks pass with no skips. A held-read regression completes a
queue removal before releasing discovery, verifies that discovery writes nothing,
and rejects sending the stale snapshot without another send. Subsequent native
validation and disposable GUI replay on `5182d5e`, recorded above, confirm that
foreground queue sending proceeds during a held discovery read and that the
stale reply cannot restore a completed queue.

### Queue recovery after Core image recoding

Core can recode an image during send, changing its blob after the queue's draft
checkpoint. Recovery previously compared that old blob path before recognizing
the same outgoing message, leaving an already-sent entry stuck when queue removal
had failed. Recovery now requires the checkpointed ID, chat, caption, quote, an
outgoing state and a nonempty attachment; Core's final blob/name/type may differ.
Unsent drafts still require their exact attachment identity.

The new regression fails before the fix. All 152 focused checks pass afterward,
including pending/failed/delivered/read outgoing states, rejected queue removal,
and changed ID/chat/caption/quote, missing file and invalid-state rejection. The
recovery cases assert no additional send. The source audit has zero blocking
findings across 13,217 files.

A separate real-Core probe used **Deltagram integrity recovery** and pinned Core
2.63.0-dev, sending one generated image with EXIF to that disposable account's
Saved Messages. Draft 12 (state 19) became message 12 (state 26), with changed
blob and bytes, unchanged chat/caption, and no remaining draft. The selected
profile stayed unchanged. This proves Core's transformation independently of
the subsequent GUI recovery check.

The checksum-verified `5182d5e` GUI now passes recovery from that transformation.
A seeded, Core-backed queue contained one generated EXIF image and an older
quote; its original source file was removed before GUI review. Rejected Send
checkpointed draft 209 with the original bytes. Explicit Send then produced
outgoing message 209 with a different blob, filename and hash, while injected
queue-removal rejection kept the old queue and pin. After normal quit/restart,
Send remaining reconciled that existing outgoing ID and removed the queue/pin
without another Core send. The audit stayed at two send calls: one rejected,
one accepted. Both clients retained one image with matching final bytes,
caption and quote; the alternate profile draft was unchanged. This tests native
queue recovery from a seeded ledger, not file-dialog image preparation. Fault
injection was removed afterward.

### Recording handoff and storage recovery

Ordinary voice/video recording now pauses for preview before Send. A preflight
preserves existing queues/drafts and ignores a recording discarded or trimmed
while the check was pending. The batch coordinator takes ownership of encoded
bytes before the preview clears; one worker writes a private temporary file and
Core imports it into a durable queue before sending. No sendable donor placeholder
is created. Normal quit and profile switching remain guarded during this handoff.

A rejected local write keeps the bytes for Retry or explicit Discard. Rejected
Core storage keeps the private file; an uncertain publication is read back
without automatically sending. Discard during the worker write prevents late
publication and releases the temporary owner. Recording cannot start during an
active compose operation. The unsupported view-once control is disabled, and
scheduled/view-once recording requests are rejected with the preview retained.

All 160 focused checks pass with no skips. Coverage includes local-write failure
followed by recovery, rejected copy/pin/publication, uncertain publication,
private permissions/cleanup, and discard before worker completion. A real-Core
test removes the in-memory/private-file owners, checkpoints a rejected send,
restarts Core, and verifies the same recording bytes/type/name/draft ID. This is
a storage test, not codec or playback proof.

The ordinary recording, quoted cross-client playback and rejected-storage/restart
replay now pass on `49454ee`, as recorded above. The combined forwarding UI is
compiled by `5182d5e` and uses the generic queue. Quoted voice-plus-forwarding
also passes rejected-send/restart/delivery and reference playback as recorded
above; round-video capture and physical audio devices still need validation.

### Recent live regressions and fixes

An additional shutdown regression was reproduced on `8323369`: a text draft
was confirmed in Core before Send, but quitting while a rejected `send_msg`
reply was delayed left neither the draft nor an outgoing message after restart.
The replacement normal-text path saves and reads back a Core draft before
`misc_send_draft`, retaining the composer until Core owns the outgoing message.
It reconciles uncertain replies using the saved message ID without retrying the
mutation. Editing is paused during this local handoff; normal quit is prevented
only until the saved draft is confirmed. Attachment drafts fail closed.
The focused suite passes 33 cases (35 including setup/cleanup),
including a real-Core rejected-send/restart test and simulated lost-ack,
save-rejection, send-rejection, duplicate-submit and account-isolation cases.
The checksum-verified `ff64414` GUI passes:

- Rejected send followed by normal quit: text and quote return after restart;
  one explicit retry creates one Core message and the reference GUI shows its
  quote correctly. Typing during the handoff does not change the locked composer.
- Accepted send with its acknowledgement delayed past quit: restart shows the
  same single outgoing message and an empty composer, with no duplicate draft.
- An injected error reply after Core commits: the known message ID reconciles
  successfully without another send. The reference GUI receives one message.
- Quit before draft confirmation: a visible saving notice prevents normal exit;
  the send completes once Core confirms persistence.
- Profile switches during pending sends: the original text remains visible on
  return; rejected sends retain it and accepted sends clear only that profile.
  The alternate profile's existing draft remains unchanged.
- Core process failure during a pending send: the GUI reports messaging stopped;
  the saved text returns after restart and no outgoing message was invented.

Private evidence is in `data/integrity-live/handoff-evidence.json` and the
`handoff-*.png` captures. The reference GUI displays the accepted messages and
quote. A point-in-time process/socket check records zero TCP connections owned
by the GUI and seven owned by Core; this is not a continuous network trace.
Combined forwarding and attachment sends still use their existing send paths;
this handoff change covers the normal text composer.

- `35f036c`: repeated switches between newly provisioned and alternate profiles
  preserve the correct lists and drafts without the reproduced segmentation
  fault. Reset now detaches dialog rows before clearing their backing list.
- `35f036c`: rejected reactions display an error and retain Core state. Accepted
  reactions reach both actual GUIs without extra transport-message bubbles;
  group reactions show counts without inventing reactor identities.
- `35f036c`: rejected date-range deletion preserves all six fixture messages.
  Accepted deletion removes the five selected messages while retaining the
  message outside the selected day.
- `a8c544f`: an account provisioned successfully but rejected during selection
  remains available in the profile menu. Retrying setup creates no duplicate
  account and performs no repeated configuration writes.
- `d05d666`: an event arriving before its send acknowledgement produces one
  outgoing authoritative message. Deleting that message before acknowledgement
  leaves no ghost or stale pending bubble. A rejected text send restores an
  empty composer and persists the recovered draft through Core.
- `9397be0`: ordinary text is sent as Core text without an unnecessary HTML body.
  The reference GUI renders it normally; a subsequent GUI edit reaches both
  clients and Deltagram displays the read receipt. A reference-client quoted reply renders
  with the matching Core message ID. Remote deletion removes the original from
  both clients; immediate quote rendering after deletion exposed a separate
  presentation bug fixed in `c3e4e8d` and verified on `8323369`.
- `4fd0cb2`: chat export and auto-delete controls are absent from the actual
  chat/privacy/advanced menus. Sharing restrictions are guarded in source.
- `6a6d60d`: Save As suggests the original attachment filename and the previous
  user-selected directory, and saved bytes match the fixture SHA-256. Immediate
  normal quit preserves the complete latest draft across restart.

The `e846f9b` GUI hides Edit for HTML messages and permits plain-text editing;
the edited text reaches the reference GUI. On `8323369`, rejected sends with a
newer composer draft preserve both texts in Core and across profile switches.
A rejection arriving after switching profiles recovers the original profile's
draft without changing the alternate profile. A live quoted reply remains
visible immediately after deleting its original, without reopening the chat.

The same `8323369` artifact passes delayed Connectivity closure and an edit reply
arriving after a profile change. Terminating the disposable Core process shows a
stopped state while the GUI remains responsive; restarting restores an already
persisted draft. These checks do not prove recovery of an unacknowledged send
interrupted by process exit.

A network ownership snapshot on `a8c544f` found no GUI network sockets and mail
connections owned by Core. This is a point-in-time check, not proof for every
execution path.

### Confirmed on the checksum-verified `3c2def3` GUI

- Deleted originals retain standalone quote text. Forwarded messages display
  an anonymous forwarding label without invented sender identity or timestamp.
- Core drafts appear in the composer/list preview without becoming history
  bubbles. Repeated profile switches restore each profile's own content.
- Reused message IDs display the primary profile's red image/two-second voice
  and the alternate profile's blue image/four-second voice correctly. Switching
  during playback clears the player and search state.
- Both global and in-chat Saved Messages searches show all 60 fixture matches,
  load past the first 50 and navigate to the selected message. Delayed replies
  do not replace newer queries or reopen canceled searches. Rejected searches
  show an error, and profile changes clear results/caches.
- A partial 256 KiB attachment remained partial while reading the chat. The
  explicit Download action surfaced a rejected request without retrying. A
  second explicit request completed; native rendering retained the real caption.
  Core bytes and a GUI Save As copy matched the original SHA-256 exactly. The
  temporary Core download limit was restored. Save As incorrectly suggested
  Core's blob filename/directory; `6a6d60d` fixes that default; its targeted GUI replay passed.
- Rejected Clear history calls (immediate and delayed) display an error and
  preserve all three fixture messages in Core and the UI.
- Fresh GUI onboarding created one configured disposable profile after five
  repeated clicks and five Enter presses. Rejected/malformed profile discovery
  made no setup writes; the existing profiles stayed unchanged.
- Importing the reference client's invite QR image through the GUI completed
  SecureJoin. Both actual GUIs exchanged text; Core reported read state 28 and
  Deltagram displayed the double check.

### Focused and earlier live evidence

The standalone Qt suite passes against Core 2.62.0 and pinned 2.63.0-dev. It
covers reordered replies, malformed responses, startup/exit/timeout handling,
no automatic mutation retry, account callback cancellation, independently
persisted drafts with overlapping IDs, serialized draft writes, hidden transport
messages, explicit download states, account-keyed media IDs, non-monotonic
history ordering, confirmed single/bulk/history deletion, partial failure,
timestamp selection, and messages arriving outside a deletion snapshot.

The earlier `4ddcae4` GUI passed group sender/profile navigation, rejected
message deletion, bidirectional group messages, a read receipt, incoming edits,
a quoted reply, delayed Connectivity closure, a delayed edit across profiles,
and direct Core termination with a request pending. Live reactions reached
both clients but exposed hidden reaction bubbles and invented group reactor
avatars; source fixes followed. The `b06680e` GUI passed successful Clear history
(Core and UI empty) and rejected chat deletion with visible feedback.

Core API checks independently passed SecureJoin, bidirectional text/read
receipts, stable-ID edits, quotes, remote deletion, group identity and attachment
byte equality between pinned Core and the reference client. API checks do not
substitute for GUI verification.

### Remaining release gates

Production readiness is not established. Normal-text send handoff durability
now passes the recorded shutdown, Core-failure and uncertain-reply replays.
Combined forwarding, multi-file batches and transformed-media send durability
still need equivalent coverage. The known profile-switch crash, send acknowledgement races,
rejected-send draft loss and quote-deletion presentation defects have passed
their targeted live replays.

Attachment durability is now a confirmed defect on `ff64414`: with a rejected
`send_msg` acknowledgement delayed, sending a file with a caption and quitting
leaves neither a draft nor an outgoing message after restart. The original
selected file remains on disk, but the client's send selection and caption are
lost. Private evidence: `attachment-handoff-pending.png`,
`attachment-handoff-lost.png`, and `handoff-attachment-baseline.json`.
The draft handoff backend accepts attachments and retains Core's managed blob
path. A real-Core regression verifies rejected-send recovery after restart,
byte equality after deleting the original temporary file, and explicit removal
that retains the caption. `be7b043` connects ordinary single-file submissions
to this handoff and adds a saved-attachment review/send/remove dialog behind
the attachment button. File-dialog callbacks are guarded against chat/profile
changes. Failed handoffs reload Core state, including when a save reply is lost.

The focused suite passes 41 cases (43 including setup/cleanup, no skips),
including rejected removal, stale-file protection and committed writes with lost
replies. Native/portable/package CI passed for `be7b043`. Its checksum-verified
GUI passed these disposable-account checks on 2026-10-25:

- Rejected send followed by quit/restart retained the file and caption. The
  original selected file was deleted before quit; Core's recovered bytes still
  matched SHA-256. The recovery dialog showed the original filename, and one
  explicit retry produced one outgoing message with an empty composer.
- A rejected removal preserved the file. Successful removal retained the caption
  in Core and the GUI; sending that caption afterward produced a text message.
- An error reply after Core accepted the attachment reconciled the known ID
  without a resend. Accepted-send quit before acknowledgement restored the same
  single outgoing message and no draft after restart.
- Switching away and back during a pending handoff retained the original
  caption and locked composer; the alternate account's existing draft was
  unchanged. After rejection, the file remained in its original profile.
- Killing Core during a pending attachment handoff displayed a stopped state.
  Restart restored the file and caption with matching bytes and no outgoing
  message. The alternate profile's draft remained unchanged.

Official Delta Chat Desktop 2.62.0 visibly received the recovered attachment,
the caption-only message, the lost-reply attachment and the accepted-before-quit
attachment. Evidence is in `data/integrity-live/attachment-evidence.json` and
`attachment-*.png`. These checks cover ordinary single files; multi-file
batches, transformed media and combined forwarding retain their old send paths
and remain release gates.

### Media preparation defects — 2026-10-25

A generated-folder ZIP on `be7b043` failed before Core could read it: the
preparation object's destructor removed its temporary file after queuing the
asynchronous send. A disposable 205-byte archive reproduced Core's missing-file
error; the proxy confirmed the file was absent on request arrival and after a
three-second dispatch delay. No outgoing message or Core draft existed.
Evidence: `archive-baseline-result.png`, `archive-baseline-lifetime.jsonl` and
`attachment-archive-baseline.json` under `data/integrity-live/`.

`b7e7522` retains the preparation owner through Core's reply and retains rejected
uploads for explicit retry or removal. It also passes the intended filename to
Core. Source review found that background file preparation checked only the
shared UI session, which survives profile changes. The change captures an
account guard at task creation, checks it before dispatch/cache mutation, and
cancels active media preparation on profile reset. Cross-profile disclosure from
that earlier path has not been demonstrated in a live test.

All 41 focused tests still pass, including expanded guard invalidation and
scope-destruction assertions. The first native run caught a missing `ApiWrap`
definition at the reset call; `8e37b6c` adds the required include. `3d21bc1`
also clears retained media owners when the UI session ends. Replacement
native/portable/package CI passed for `8e37b6c`:
[run 40174035329](https://github.com/mamaru-a/deltagram/actions/runs/40174035329).
This includes 43 Qt checks including setup/cleanup, with no failures or skips,
Debian 13 and Ubuntu 22.04 portable checks, and distribution-package checks.
The downloaded GUI and Core checksums were verified; symbols were not downloaded.

The verified GUI passed these disposable live checks against official Desktop
2.62.0:

- A three-second delayed archive send retained its temporary file until Core
  accepted it, then removed the staging copy. The original filename, ZIP entry
  and bytes matched in both clients.
- Switching profiles during a 30-second delayed send retained the file until
  acceptance in the original profile. No matching message appeared in the
  other three profiles, and the alternate draft stayed unchanged.
- A rejected archive retained its staging file. Explicit deletion removed the
  local message and file without creating a Core or reference-client message.

Private evidence: `archive-evidence.json`, `archive-final-reference.png`,
`archive-profile-pending.png` and `archive-discarded.png`. This replay covers
already-prepared media crossing a profile switch; the earlier guard tests cover
invalidating background preparation callbacks. Background preparation itself
has not been deterministically interrupted in a live GUI check.
These fixes do not establish batch/media send durability across shutdown.

A separate byte-backed media check on `be7b043` found image staging files left
behind after Core accepted the sends. The file-selected and clipboard-image
messages exist in Core with their captions and file bytes; temporary copies
remained with mode `0644`. This proves missing staging cleanup, not access by
another user. Private evidence: `staging-clipboard-baseline.json`,
`attachment-staging-baseline.json` and
`attachment-staging-clipboard-baseline.json` under `data/integrity-live/`.
`1f3add9` replaces the reusable filename with `QTemporaryFile`, checks flushing,
and transfers deletion responsibility to the retained preparation owner. Failed
writes are removed automatically. Native/portable/package CI and disposable GUI replay pass on `a1cbe2a`.
With a ten-second delayed image send, the staging file existed with mode `0600`
until Core accepted the message, then disappeared. The image bytes matched in
official Desktop 2.62.0. A rejected clipboard image retained its private staging
file; explicit deletion removed it without creating a Core/reference message.
The other profiles had no matching messages and the alternate draft stayed
unchanged. Private evidence: `media-image-private-pending.json`,
`media-image-accepted.json`, `media-image-rejected.json` and
`media-image-discarded.json`.

Those checks inspected the uploader-owned path. A later whole-temp-directory
check on `2e051d8` found a second copy written earlier by `FileLoadTask`: normal
clipboard photos left an unowned `delta-tel-clip-*` JPEG with mode `0644` after
acceptance and explicit rejection/deletion. This is a cleanup/permission defect;
access by another user was not tested. The earlier uploader fix did not remove
this separate copy.

The same replay exercised the encoded-JPEG clipboard format. Although the
preparation result was a 1280×960 JPEG, the uploader selected the original
3200×2400 JPEG. The protected staging file had the original bytes while the
unowned extra copy contained the resized image. `21bbd2a` removes the early
write and gives prepared photo parts precedence over original clipboard bytes;
send-as-file keeps its original-byte behavior. All 50 focused checks pass
(including setup/cleanup, zero failures or skips), and source audit passes.
Native, portable and distribution-package validation passed in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40182693311).
The checksum-verified GUI passes disposable live replay against official
Desktop 2.62.0: one private `0600` staging file contains the prepared 1280×960
JPEG, rejection retains it, explicit deletion removes it, and delayed acceptance
also removes it. Whole-temp-directory checks find no extra preparation copies.
Core and the reference client have identical resized bytes. A 90-degree editor
rotation also arrives correctly in the reference GUI, with matching bytes and
no staging residue. Other profiles have no matching messages, and the alternate
draft remains unchanged. Private fixed evidence: `prepared-encoded-fixed-*.json`,
`media-encoded-fixed-delivered.json`, `media-rotated-fixed-delivered.json` and
`prepared-rotated-reference.png`.
Private baseline evidence: `prepared-baseline-staging.json`,
`prepared-encoded-baseline-rejected.json` and
`prepared-encoded-baseline-discarded.json`. The generated baseline copies were
removed after recording evidence. This change does not fix restart recovery.

The send-as-document replay preserved a small 3200×2400 JPEG, but source review
found the bridge was discarding the explicit file choice and selecting Core's
`Image` input type. This permits Core to recompress larger files. `703ae5b`
carries the choice through preparation and upload to Core's `File` input type.
Native, portable, distribution-package and all 50 focused checks passed in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40183517022).
Baseline evidence: `prepared-document-encoded-dialog.png`,
`prepared-message-types.json` and `media-document-fixed-delivered.json`.

The initial fixed-binary assertion expected stored type `File` and failed.
Inspection of pinned Core's `BlobObject::check_or_recode_image` showed that
expectation was incorrect: `File` skips recompression, then a recognized image
is recorded as `Image`. A 5,080,933-byte JPEG sent through the `703ae5b` GUI's
uncompressed choice retained its exact bytes. A direct Core `Image` control
with the same source recompressed it to 3,152,369 bytes. This control bypasses
the GUI and is not presented as a second GUI baseline. Normal photo mode still
stages a resized 1280×995 image with private permissions. Both GUI sends arrive
in official Desktop 2.62.0 with exactly matching bytes, no duplicate messages,
no staging residue, no matches in the other profiles, and the alternate draft
unchanged. Evidence: `prepared-message-original-large-703.json`,
`prepared-core-compressed-control.json`, `prepared-message-normal-photo-703.json`,
`media-original-large-703-delivered.json`, `media-normal-photo-703-delivered.json`
and `prepared-normal-photo-703-final.json`. `f751489` changes the
English option to “Send without compression” instead of promising document
rendering. Native/portable/package checks and all 50 focused checks passed in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40184912481).
The checksum-verified GUI shows the corrected label for both single-image and
multi-image selections. Those label checks cancelled without sending; byte
delivery was verified on `703ae5b` as recorded above. Private captures:
`uncompressed-label-f751.png` and `uncompressed-label-multiple-f751.png`.

### Archive omission and stale failure row — 2026-10-25

A deterministic `f751489` replay confirms that `Storage::WriteArchiveFile`
silently skips a selected file that disappears after gathering entries. An
inotify watcher removed the second disposable file only after archive creation
opened the first (32 MiB) fixture. The GUI completed a ZIP containing only the
first file and invoked `send_msg`. The test proxy rejected that request, so no
archive was delivered. Explicit deletion removed its private staging copy.
Other profiles had no matching messages and the alternate draft remained intact.
`a2b2b61` removes the skip; failure to open any selected file must now fail the
archive instead of sending an incomplete result.

The same replay exposed a stale chat-list indicator: the bubble displayed
Failed while the row retained a clock. Switching chats repainted it correctly.
`1a3017c` invalidates the dialog row as soon as `HistoryItem::sendFailed` updates
the message state. Both changes pass native/portable/package validation and all
50 focused checks in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40185835831).
Baseline evidence: `archive-race-baseline.json`,
`archive-race-baseline-result.json`, `archive-race-baseline.png`,
`archive-race-row-refreshed.png` and `media-archive-race-baseline-discarded.json`.

On the checksum-verified `1a3017c` GUI, the same inotify race aborts preparation:
zero `send_msg` calls, no remaining staging ZIP, and no matching Core/reference
message. A healthy small archive still sends successfully: Core and official
Desktop 2.62.0 contain the expected fixture and identical SHA-256
`77727a57f007d7d1dc1220df7f5fee2a70aaf561d0278b1bf0a80cab0079d7fd`.
Its staging file is gone after acceptance. A separately rejected image send
updates both the bubble and chat row to Failed without hovering or switching
chats. Other profiles have no matching messages and the alternate draft is
preserved. Private evidence: `archive-race-fixed.json`,
`archive-staging-fixed.json`, `media-archive-race-fixed.json`,
`media-archive-control-1a3017c.json`, `media-failed-row-immediate.json`, and
`failed-row-immediate-1a3017c.png`.

The failed archive preparation exposed another state bug: the thumbnail said
Failed, but the message itself remained Sending and did not offer Delete.
`10cd8fe` marks photo/document preparation failures as failed messages, reusing
the row invalidation above. Native/portable/package validation and all 50 focused
checks pass in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40186867983).
Its verified GUI repeats the archive race with no send or staging ZIP and shows
Failed immediately in both bubble and row. The stale upload object still makes
the menu say Cancel Upload; confirming that action removes the failed bubble.
`c9962d2` clears that stopped upload state for photo/document preparation failures;
its native/portable/package validation and all 50 focused checks pass in
[CI run](https://github.com/mamaru-a/deltagram/actions/runs/40187692127). Private evidence
from the intermediate `10cd8fe` replay:
`archive-race-failure-state-10cd.json`, `archive-staging-failure-state-10cd.json`,
`media-archive-failure-10cd.json`, `archive-failure-10cd.png`,
`archive-failure-10cd-menu.png`, and `archive-failure-10cd-removed.png`.

A separate staging-write failure is reproduced on `1a3017c` by launching only
the disposable GUI with `TMPDIR` pointing to an owned regular-file fixture.
The prepared clipboard image cannot create its temporary file. No `send_msg`
call or Core/reference message occurs, but the bubble and row remain Sending
and the menu offers only Copy Text. Account databases and build-cache paths
are unchanged by this injection. `deliverDelta` calls the queue-based failure
handler even though this path never entered the upload queue. `d25dcee`
notifies the existing failure handler directly using the preparation result;
it is included in the verified `c9962d2` build above. Baseline evidence:
`temp-failure-baseline.png`, `temp-failure-baseline-menu.png`, and
`media-temp-failure-baseline.json`.

The checksum-verified `c9962d2` GUI passes both failure replays. The unwritable
image-staging path and missing archive input produce Failed in the bubble and
chat row, stop showing an active upload, and offer Delete. Explicit deletion
removes each failed message. Neither attempt reaches `send_msg`, creates a
Core/reference message, or leaves a staging file. The failed image still has a
placeholder preview; this check establishes failure/removal behavior, not
attachment recovery.

After restoring the owned temporary fixture to a writable directory, the same
GUI session sends a newly selected image normally. Core and official Desktop
2.62.0 display the image and caption, with identical prepared JPEG bytes
(1280×995, SHA-256
`fb6f1dbd5258f4579cd08469d636bd9952cdd655a3b45d6b7213dad59c1a6bf3`).
The staging file is removed after acceptance. Other profiles have no matching
messages, the alternate draft stays intact, and fault injection is cleared.
Private evidence: `media-temp-failure-c996.json`, `temp-failure-c996.png`,
`temp-failure-c996-menu.png`, `temp-failure-c996-removed.png`,
`archive-race-failure-state-c996.json`, `archive-staging-failure-state-c996.json`,
`media-archive-failure-c996.json`, `archive-failure-c996-menu.png`,
`archive-failure-c996-removed.png`, `prepared-staging-recovered-c996.json`,
`prepared-message-staging-recovered-c996.json`,
`media-staging-recovered-c996-delivered.json`, and
`staging-recovered-reference-c996.png`.
Prepared-media restart recovery and batch durability remain open.

### Prepared single-attachment handoff — 2026-10-25

`7f95cca` routes a single prepared image, video or generated archive through
Core's draft handoff after preparation. It retains the caption and quote, locks
the composer, and prevents normal quit/profile switching until Core confirms a
durable copy. A failed send reloads Core's current draft for review rather than
restoring the pre-send snapshot. A lost save acknowledgement can establish
ownership through readback, but does not trigger a send or automatic retry.

The focused suite passes 56 checks, with no skips, including prepared-attachment
ownership ordering, rejected saves/sends, lost acknowledgements, quote mismatch,
and real-Core recovery after the original file is removed. The source audit
reports 13,211 files and zero blocking findings. Native/portable/package run
`40189771900` passed, including all 56 focused checks and package verification.

The checksum-verified GUI passes a rejected prepared-image send followed by
normal quit/restart and one explicit retry. Core retains the caption, filename
and prepared 1280×995 JPEG with SHA-256
`fb6f1dbd5258f4579cd08469d636bd9952cdd655a3b45d6b7213dad59c1a6bf3`,
while the temporary preparation file is already gone. The recovery dialog
shows the saved filename. Retry produces one outgoing message (Core 137), one
matching reference message (157), identical bytes in official Desktop 2.62.0,
an empty draft and a read receipt. Other profiles have no matching messages;
the alternate draft is unchanged. Private evidence: `attachment-prepared-rejected-7f95.json`,
`attachment-prepared-restart-7f95.json`, `media-prepared-retried-delivered-7f95.json`,
`prepared-recovery-dialog-7f95.png`, `prepared-retried-reference-7f95.png`, and
`prepared-message-prepared-retried-7f95.json`.

This replay also exposes a navigation defect: after retry, the Qt chat jumps to
old history instead of the new attachment. `fastShowAtEnd` selects the unread
position on this path. Private evidence: `prepared-retried-position-7f95.png`.
The follow-up `252bfa0` fix selects the actual end for Delta sends. Native
validation and the uncompressed-image recovery replay below pass.
Quote retention and broader interruption cases still need exact-binary GUI
coverage. The unquoted archive case below
provides a separate recovery check.

A generated-archive recovery case also passes on `7f95cca`: rejected send leaves
Core draft 150 with caption and `archive-integrity.zip`; normal quit/restart
retains the same draft ID and SHA-256
`77727a57f007d7d1dc1220df7f5fee2a70aaf561d0278b1bf0a80cab0079d7fd`.
The settled recovery dialog shows the filename. One explicit Send after clearing
the fault produces Core message 151 and reference message 163, matching ZIP bytes
and the single expected fixture entry in official Desktop 2.62.0. Both GUIs show
the caption/file, the draft clears, other profiles have no matching messages,
and the alternate draft is unchanged. No temporary archive survives restart.
Private evidence: `attachment-archive-explicit-rejected-7f95.json`,
`attachment-archive-explicit-restart-7f95.json`,
`archive-explicit-review-7f95.png`, `media-archive-explicit-delivered-7f95.json`,
`archive-explicit-sent-7f95.png`, and `archive-explicit-reference-7f95.png`.
An earlier archive attempt was excluded from retry evidence because a click
during the dialog transition selected attachment removal; its caption remained.

Multi-file batches, voice recording and combined forwarding retain their prior
send paths. Failure before a prepared output exists does not make the original
input durable. A failed save before Core confirms ownership retains the local
attachment and blocks normal quit/profile switching until explicit removal;
its recovery UX still needs live validation.

### Batch storage prerequisite — 2026-10-25

An isolated offline probe of the pinned Core confirms a possible storage basis
for multi-attachment recovery: `copy_to_blob_dir` owns the bytes, `ui.*` config
stores client compose state, and a separate config value containing
`$BLOBDIR/<basename>` retains the file during housekeeping. A JSON field alone
is not a retention reference; housekeeping only recognizes a config value
starting with that prefix. `batch_set_config` writes keys sequentially, so it
must not be treated as an atomic transaction.

The probe uses newly created offline accounts, a synthetic identity solely for
backup-triggered housekeeping, and a process-local clock offset. It verifies
account isolation, restart after deleting the original source, retention of the
pinned blob while an unreferenced control is removed, and cleanup after explicit
pin removal. It never starts network IO or changes the host clock. Private
evidence: `queue-storage-evidence.json`; helpers: `queue_storage_probe.py` and
`queue_clock.c`. This probe proves storage prerequisites only; it does not
validate batch preparation, send checkpoint coordination, uncertain-send
reconciliation or GUI recovery.

The new `ComposeQueueStore` implements these storage operations through Core
RPC: stage all files with independent pins, publish one queue value only after
pin confirmation, preserve an existing queue, record the first item's known
Core message ID, remove items in order, and reconcile orphan pins after an
interrupted stage or removal. Readback resolves lost write acknowledgements;
uncertain publication retains every pin. Cleanup inspects all queues in the
account before releasing any orphan and stops on malformed or unreadable state.

The focused suite now passes 66 checks with zero failures or skips. New cases
cover rejected pins/publication, committed writes with lost replies, uncertain
publication/removal, stale item IDs, immutable known-message IDs, preservation
of unreadable queues and other accounts, and real-Core two-file restart after
deleting both originals. The source audit reports zero blocking findings across
13,213 files. This is a storage layer, not completed batch sending: the GUI and
send checkpoint continuation are not connected yet, and no native/live batch
recovery proof is claimed. The prepared-single-attachment native run remains
separate at `7f95cca`.

### Uncompressed recovery and preparation guards — 2026-10-25

The checksum-verified `252bfa0` GUI passes uncompressed clipboard-image recovery.
A rejected send leaves Core draft 154 with its caption and original 1800×1400
JPEG bytes (5,080,933 bytes, SHA-256
`c8c0927ee89854b54b541c12f1cec1f5c50b6b9bed873ea2dd55812dd422fd63`).
The same draft survives normal quit/restart. One explicit recovery Send creates
Core message 155 and reference message 165 with identical bytes, clears the
draft, and leaves no preparation file. Other profiles have no matching messages;
the alternate draft is unchanged. The Qt chat remains at the new attachment,
verifying the navigation fix. Both GUIs display the recovered image/caption.
Core records the recognized JPEG as `Image` even for the uncompressed choice,
as established by the earlier Core inspection; an initial helper expectation of
`File` was corrected without changing application code. Reference download was
still pending on the first observation; a later read confirmed the bytes without
resending. Private evidence: `attachment-uncompressed-rejected-252.json`,
`attachment-uncompressed-restart-252.json`, `uncompressed-review-252.png`,
`prepared-message-uncompressed-retried-252.json`,
`media-uncompressed-delivered-252.json`, and `uncompressed-position-252.png`.

A separate attachment-specific `misc_set_draft` rejection leaves no outgoing
message and retains a text-only Core draft plus the prepared 1280×995 JPEG in
private mode-0600 staging. The GUI shows Failed and a locked composer. Normal
Quit and a profile switch are refused with explicit saving/removal notices.
Deleting that failed local attachment removes its staging file, keeps the
caption, and unlocks the composer. Normal Quit then succeeds and the caption survives restart. Private evidence:
`attachment-pre-save-rejected-252.json`, `prepared-pre-save-rejected-252.json`,
`pre-save-quit-guard-252.png`, `pre-save-switch-guard-252.png`,
`prepared-pre-save-deleted-252.json`, and `pre-save-deleted-252.png`.
This verifies the normal-exit guard, not durability after a forced process kill
before Core accepts the attachment. A failed pre-save attachment currently
requires removal and reselection; direct local Retry remains unfinished.

The same `252bfa0` GUI also passes a lost prepared-send acknowledgement.
The proxy replaces Core's successful `misc_send_draft` reply with an error;
the client reconciles Core message 160, clears the draft and shows the sent
image. Normal quit/restart retains that one message and an empty composer.
The method audit records exactly one send request including restart. Official
Desktop 2.62.0 receives one matching image (168) with the prepared JPEG hash
`fb6f1dbd5258f4579cd08469d636bd9952cdd655a3b45d6b7213dad59c1a6bf3`.
Other profiles and the alternate draft remain unchanged; staging is empty.
Private evidence: `attachment-prepared-lost-send-252.json`,
`prepared-lost-send-252.png`, `attachment-prepared-lost-send-restart-252.json`,
`media-prepared-lost-send-restart-252.json`, and
`prepared-lost-send-restart-count-252.json`. Fault injection was cleared.

### Restored quote lookup regression — 2026-10-25

A second prepared-image recovery case on `7f95cca` failed quote retention.
After a committed `misc_set_draft` with an injected error reply, Core retained
attachment draft 141, the caption and quote ID 27 across normal quit/restart.
The restarted GUI restored the caption without the reply bar. One explicit
recovery Send produced message 144 with the correct image/caption but no quote.
Private evidence: `attachment-prepared-save-lost-quote-7f95.json`,
`attachment-prepared-quote-restart-7f95.json`,
`prepared-quote-recovery-dialog-7f95.png`, and
`attachment-prepared-quote-retried-7f95.json`. This case is a failure, not quote
compatibility proof.

`7f25d36` routes individual message lookup through Core instead of the
unsupported donor `messages.getMessages` request. It verifies the message ID,
chat and visible-history membership, rejects drafts/hidden messages, and scopes
both reads to the active account. The existing presentation adapter adds the
result as an existing item, without generating incoming-message notifications.
Restoring a draft retains its quote ID while the preview is pending or fails;
a failed preview stays explicitly unavailable until retried or removed by the
user. Stale reply callbacks cannot replace a different selected reply.

All 85 focused checks pass, with no failures or skips, including lookup failures,
wrong IDs/chats, hidden messages, draft exclusion, cancellation at both reads,
and real-Core lookup before and after deletion. The source audit has zero
blocking findings across 13,213 files. Native/portable/package validation
passed in [run 40195411077](https://github.com/mamaru-a/deltagram/actions/runs/40195411077),
including all 85 focused checks for `7f25d36`.
It started only after the separate `252bfa0` native/portable/package
[run 40193903762](https://github.com/mamaru-a/deltagram/actions/runs/40193903762)
completed successfully, including all 77 focused checks for that revision.
The `252bfa0` GUI/Core artifacts were checksum-verified; its navigation fix
passes the uncompressed-image recovery replay above.

The checksum-verified `7f25d36` GUI restores and sends an older quoted attachment
correctly. The `252bfa0` GUI first created Core draft 164 with the prepared JPEG,
caption and quote ID 27 after a committed save whose acknowledgement was lost.
It did not send. Normal quit followed by launch of `7f25d36` restored the same
Core draft and a visible reply bar naming the original message. One explicit
recovery Send produced Core message 165 and reference message 171 with the same
prepared bytes and the original quote. Official Desktop 2.62.0 visibly renders
that quote above the image. The draft clears, other profiles have no matching
messages and the alternate draft remains unchanged. Private evidence:
`attachment-quote-fix-before-restart-252.json`,
`attachment-quote-fixed-restored-7f25.json`, `quote-fixed-restored-7f25.png`,
`quote-fixed-review-7f25.png`, `attachment-quote-fixed-retried-7f25.json`,
`media-quote-fixed-delivered-7f25.json`, and `quote-fixed-reference-7f25.png`.

A Core-seeded attachment draft on the same disposable account also verifies the
lookup-failure path. With only message 27's lookup rejected, `7f25d36` displays
“Quoted message unavailable.” Editing the caption keeps quote ID 27 and the
attachment bytes in Core. Explicitly removing the reply clears only the quote;
one recovery Send then delivers the unquoted image/caption (Core 170,
reference 174) with identical bytes. Private evidence:
`seed-quote-unavailable-7f25.json`, `quote-unavailable-loaded-7f25.png`,
`attachment-quote-unavailable-edited-7f25.json`,
`attachment-quote-explicitly-removed-7f25.json`,
`attachment-quote-removed-sent-7f25.json`, and
`media-quote-removed-delivered-7f25.json`.

A second Core-seeded draft delays only the original message lookup by twelve
seconds. The fixed GUI displays a loading reply bar; removing it before the
reply arrives clears the persisted quote. After the delayed callback settles,
the reply bar stays absent, Core still has no quote, and the file/caption remain
unchanged. Nothing is sent. The fixture attachment and caption were then removed
through the GUI, and fault injection was cleared. Private evidence:
`seed-quote-delayed-7f25.json`, `quote-delayed-loading-7f25.png`,
`attachment-quote-delayed-cancelled-7f25.json`,
`attachment-quote-delayed-settled-7f25.json`, and
`quote-delayed-settled-7f25.png`.

### Queued-send checkpoint — 2026-10-25

`DraftStore` can now wait for an asynchronous durable checkpoint after confirming
Core's draft and before issuing `misc_send_draft`. The checkpoint receives the
known ID and Core's canonical filename/type. Rejection leaves the draft intact;
late or duplicate continuations cannot issue another send. The queue stores the
canonical fields without changing the retained file, caption or quote.

An already-checkpointed attachment uses a separate resume path that verifies
the saved ID, chat, state, file, filename, type, caption and quote without writing
another draft. This matters because `misc_set_draft` constructs a new message
and can replace the old draft ID. A changed draft is preserved and not sent.

All 77 focused checks pass, with no failures or skips. Added checks cover delayed,
rejected and duplicate checkpoint continuation, destruction of its owner,
identity/content mismatches, a committed checkpoint whose readback is lost, and
resuming the recorded ID without any draft rewrite. A real-Core rejected resume
also preserves the original draft ID after restart. Batch controller/GUI wiring
and live validation remain open. Native/portable/package validation passed
with all 77 checks at `252bfa0` (run `40193903762`).

### Local Retry and batch advancement — 2026-10-25

`7d64366` adds a Retry context action for failed prepared attachments whose
source file is still retained and whose Core ownership has not been confirmed.
The action checks the active account, preparation generation and draft handoff,
then marks the local message as sending before dispatch. Duplicate actions
cannot initiate a second handoff. An uncertain save reloads Core's draft;
an existing different attachment is preserved. Core-owned failures continue
to use the saved-attachment review flow.

All 89 focused checks passed for that source revision. Its remote native,
portable-runtime and package validation passed in
[run 40198373254](https://github.com/mamaru-a/deltagram/actions/runs/40198373254).
Direct local Retry passes disposable replay with that checksum-verified artifact.
Two rejected attachment saves leave the same private prepared JPEG retained and
the local message failed. After removing the rejection, one explicit Retry with
an eight-second save delay marks it pending and removes Retry/Delete from its
context menu. The audit records exactly one `misc_send_draft` call. Core message
179 and reference message 177 contain the same caption and prepared bytes
(`fb6f1dbd5258f4579cd08469d636bd9952cdd655a3b45d6b7213dad59c1a6bf3`).
Both GUIs display the image; the composer and Core draft clear, staging is
removed, and the other disposable profiles and alternate draft stay unchanged.
Normal quit/restart retains that single delivered message and an empty draft.
Fault injection is cleared. This case is unquoted; it does not establish quoted
local Retry or arbitrary process-interruption recovery. Private evidence:
`attachment-local-retry-after-restart.json`, `media-local-retry-reference-bytes.json`,
`media-local-retry-after-restart.json`, and the `local-retry-*.png` screenshots.
Subsequent test review corrected temporary JSON-reference lifetimes in two
Retry assertions; those assertions now retain owned JSON objects.

The batch sender advances one explicitly reviewed queue entry. It records the
Core draft ID before sending, resumes that same ID after interruption, and
removes an entry only after confirming that Core owns its outgoing message.
A known outgoing message is never resent. Missing or changed messages preserve
the queue. A saved draft whose checkpoint was interrupted can be adopted only
when its retained file, caption and quote match, then checkpointed before send.
Stale entry actions cannot advance the next file. Errors require a fresh queue
load; an outgoing ID returned with a queue-update error must not be resent.

All 103 focused checks pass with zero failures or skips. Added coverage includes
rejected saves/sends/checkpoints, uncertain checkpoint/removal replies,
already-sent reconciliation, changed/missing messages, duplicate/stale actions,
and closing the sender during a delayed checkpoint without leaking a composer
lock or sending. A real-Core unconfigured account retains both queued files
after the originals are removed and preserves the same rejected draft ID across
Core restart. This is storage and rejection recovery proof, not live delivery.
The publication audit checks 13,215 files with zero blocking findings.
Batch GUI wiring, native validation of the batch sender and live batch recovery
remain open. Heavy builds remain serial; no second build was started.

The UI integration review exposed a newer-draft hazard: an uncheckpointed queue
entry could replace a newer text-only Core draft. Queue advancement now rejects
a different nonempty caption or reply target, and refuses to start during an
active draft read/write. A guarded reload API reads current Core state without
interrupting that operation. All 106 focused checks pass, including newer
caption/reply preservation, an in-flight save, and refreshing a cached draft
without issuing any writes or sends. Batch UI integration remains pending.

The attachment button now checks for a saved compose queue before opening the
single-file recovery dialog or file picker. The new review dialog shows the
remaining count and next filename/caption; its explicit Send remaining action
advances the queue serially and pauses on any error. While advancing, the bridge
locks the composer and ignores stale autosave writes. It restores a fresh Core
draft before unlocking, and an account change stops further advancement without
applying the original profile's draft to the new view. This UI source passes the
publication audit but awaits native compilation and disposable replay. New
multi-file selection/preparation, queue discard controls and recovery discovery
after restart still need integration; this is not completed batch support.

Saved-queue review now also offers a confirmed Discard next action. It removes
only the reviewed entry and, when it still matches, its current Core draft.
Different/newer drafts and already-sent messages are preserved. Draft removal
is read back even after an error reply; an unreadable result or rejected removal
retains the queue for explicit review. Queue update failures are reconciled by
a fresh load on the next explicit action. Stale actions cannot discard the next
entry. The bridge restores the actual Core draft before unlocking the composer.
All 116 focused checks pass with no failures or skips, including rejected/lost
discard replies, unreadable readback, queue-write rejection, uncheckpointed
drafts, stale/duplicate actions, and real-Core discard after restart while the
second queued file retains its original bytes. The source audit passes; native
and GUI validation remain pending. New-batch preparation and restart discovery
are still unfinished.

### Search-result restart crash — 2026-10-25

The checksum-verified `7d64366` GUI crashed twice when opening an older message
from search after restart, before the planned Retry test sent any attachment.
The second reproduction used separated focus/type/click actions and a private
crash tracer. Its stack is `Dialogs::Widget::chosenRow` → `RecentPeers::bump` →
`readSearchSuggestions` → `RecentPeers::applyLocal` → `Serialize::readPeer` →
`Data::Session::peer`, ending at the "Peer id type" assertion.

The stored peer record has an explicit format tag/version, but the reader
selected several fields and flag conversions using Telegram's historical app
version thresholds. Deltagram's `AppVersion = 2001` selected the old layout,
skipping fields written by the current serializer and misaligning the next
peer record. The source fix uses the explicit modern record tag for those
fields/conversions, rejects invalid peer types and unsupported record versions,
checks truncated records before applying their fields, and bounds saved
recent/top-peer counts. No account or cache files were deleted for recovery.

The source audit passes. Serial native/package validation of `490cadd`, including
the queue recovery/discard controls, is running in
[run 40202325093](https://github.com/mamaru-a/deltagram/actions/runs/40202325093).
Native compilation and replay against the unchanged
disposable cache remain pending; the 116 focused backend checks do not exercise
this GUI serializer. Private evidence: `retry-native-crash.trace`,
`retry-trace-search.png`, and the checksum-verified artifact symbols. Recent/top
search suggestion isolation across Core profiles also needs a dedicated review:
these donor caches are owned by the reused desktop session.

That review confirmed that recent peers, ranked peers, open-chat navigation and
cached navigation avatars survive a Core profile switch. The follow-up source
resets those collections before publishing the view reset. Core sessions select
transient peer suggestions before desktop-session construction, so unscoped
donor peer records are neither imported nor written; settings-search history
remains separate. Recent/ranked peers therefore last only until profile switch
or process restart. Core profiles no longer invoke donor top-peer requests.
The publication audit passes, but this follow-up still needs native compilation
and GUI profile-switch/restart checks; the active `490cadd` build predates it.

Further serializer review found that a truncated peer record could mark a newly
created peer as loaded before its fields had been read successfully. The
follow-up delays that transition until the complete type-specific record is
valid, rejects zero bare IDs and excess ID bits, and returns no phone from a
truncated peek. It also removes the debug log containing the parsed name/phone.
These malformed-record changes pass the source audit; native/runtime validation
is still pending and they are not covered by the focused backend test target.

Native/portable/package validation of `490cadd` passed in run 40202325093,
including all 116 focused checks with no failures or skips. Its checksum-verified
GUI opens the exact older search result that crashed `7d64366`, with the original
disposable account/cache preserved. No cache reset was used. The later profile
suggestion isolation and malformed-record changes at `6078103` are undergoing
serial validation in
[run 40204220735](https://github.com/mamaru-a/deltagram/actions/runs/40204220735),
started after the preceding run completed.

The unblocked quoted local-Retry case on `490cadd` retains quote 27 and private
prepared bytes after a rejected save. One explicit Retry issues one send and
delivers Core message 184 / reference message 180 with the older quote, caption
and identical prepared JPEG bytes. Core's draft clears and staging is removed;
other profiles remain unchanged. The reference GUI displays the quoted image.
However, the open Deltagram history loses the accepted bubble while the chat-list
preview updates; reopening the chat retains that stale visible range. A normal
restart displays the delivered image and quote without resending. Private
evidence: `quoted-local-retry-*.png`, `attachment-quoted-local-retry-accepted.json`
and `media-quoted-local-retry-reference-bytes.json`.

The follow-up Retry callback invalidates the accepted chat's cached bottom and
loads the end only when that chat and account are still active. It leaves a
newly selected chat/profile alone. This source fix awaits native/live replay.
The initial queue-recovery dialog also clipped Cancel beside its other two
buttons; it now uses the existing wide-box style, pending native visual replay.

### Saved-queue GUI recovery and newer-draft discard — 2026-10-25

The checksum-verified `490cadd` GUI passes recovery from a directly seeded Core
compose ledger. The private helper writes two generated files through Core's
blob/config APIs, confirms the ledger and pins, then removes the originals; it
never sends. This setup exercises recovery controls, not new-batch selection or
preparation, which remain unfinished.

The attachment button reviews both files. With `misc_send_draft` rejected,
Send remaining pauses with both files retained and the first Core draft ID 186
checkpointed. Normal quit/restart preserves that same ID, captions and files.
After removing the rejection, one explicit Send remaining sends IDs 186 and 187
in order; the method audit records two send calls. Official Desktop 2.62.0 receives
one copy of each (183 and 184), with matching bytes and filenames. Both GUIs show
the files and captions. The ledger, pins and Core/composer draft clear; other
profiles and the alternate draft remain unchanged. Private evidence:
`queue-gui-restart1/{rejected,after-restart,delivered,audit}.json`,
`media-queue-restart-{first,second}.json`, and `queue-*.png`.

A second seeded ledger verifies the newer-draft boundary. Typing a newer caption
creates draft 189. Send remaining preserves that ID/text and both queued files
without sending. Each explicit, confirmed Discard next removes only the reviewed
entry and its pin. After the first discard, the second file still matches its
original bytes; after the second, the queue is empty. Draft 189 remains unchanged
in Core and the composer. No send or `remove_draft` call is added during these
actions. The test-only newer caption is then cleared through the GUI; fault
injection is cleared. Evidence: `queue-gui-discard1/` JSON records and
`queue-newer-draft-preserved.png`. Matching-draft discard and broader uncertain
queue/profile interruptions still need live GUI coverage. The clipped-button
fix and corrected one-item count wording also await native visual validation.

### Matching-draft discard and profile search isolation — 2026-10-25

Native/portable/package validation of `6078103` passed in run 40204220735,
including all 116 focused checks. The verified GUI loads a two-file ledger whose
first draft ID 190 was checkpointed after a rejected send on `490cadd`; originals
are absent. A rejected `remove_draft` keeps ID 190, its caption and both pinned
files. On the next explicit discard, Core removes the draft but the proxy
replaces its acknowledgement with an error. Readback confirms removal, so the
GUI clears the composer and removes only the first queue entry/pin. The second
file retains its bytes and pin. No messages from this fixture are sent. Evidence:
`queue-gui-matching1/{rejected,remove-rejected,remove-lost-ack}.json` and
`queue-matching-remove-*.png`.

The same GUI successfully opens the older search result and displays its peer
under Recent. Switching from the onboarding profile to the alternate profile
clears that suggestion; returning also starts with empty transient suggestions.
A separate Ctrl+Tab check first displays the original profile's two opened
chats, then displays neither after switching. The alternate draft remains
unchanged. The saved queue stays only in the original Core account and retains
its remaining file through both switches. Evidence: `isolation-*.png` and
`profile-scope-*.json`. These checks cover ordinary profile switching; active
popup races and malformed saved-record runtime fixtures remain separate gaps.

Serial native/package validation of the accepted-Retry refresh and queue dialog
layout/count fixes at `b5c4e9e` is running in
[run 40206069076](https://github.com/mamaru-a/deltagram/actions/runs/40206069076),
started after `6078103` completed. New-batch preparation remains unfinished.

That run passed, including all 116 focused checks. The verified `b5c4e9e` GUI
shows Cancel, Discard next and Send remaining completely and correctly labels
one remaining attachment. The retained matching-discard fixture survives another
restart with unchanged bytes; explicit final discard removes its last entry/pin
without sending. Its Core draft stays empty and the alternate draft is preserved.

The quoted local Retry display regression also passes replay on `b5c4e9e`.
After navigating to the older search result, a rejected prepared save retains
quote 27, caption and one private JPEG. One explicit Retry delivers Core message
194 / reference message 186 with the same older quote and prepared bytes
(`fb6f1dbd5258f4579cd08469d636bd9952cdd655a3b45d6b7213dad59c1a6bf3`).
The accepted bubble remains visible immediately at the end of the open chat;
no restart or navigation is needed. Both GUIs display the quote and image, the
audit records one send call, staging is empty, and the composer/Core draft clear.
Other profiles remain unchanged and fault injection is cleared. Evidence:
`retry-refresh-{prepared,accepted,reference}.png`,
`attachment-retry-refresh-{rejected,accepted}.json`,
`media-retry-refresh-accepted.json`, and `retry-refresh-audit.json`.
Changing chats/profiles during the new refresh callback still needs live replay.

### Second-device dialog integrity — 2026-10-25

On the disposable `be7b043` GUI, two rapid Continue clicks started two
`provide_backup` requests and two QR requests. Canceling a newly opened dialog
without starting a transfer sent `stop_ongoing_process` twice, even though that
dialog had no operation to cancel. A started transfer canceled with Escape
returned to Core connectivity `4000`. Private method-count evidence is in
`backup-repeat-baseline.json` and `backup-idle-baseline.json`.

`a1cbe2a` disables and guards Continue while a transfer is active, ignores stale
QR callbacks from a completed attempt, and cancels only an active attempt once
when its dialog closes. The destruction fallback cannot issue a second stop.
Native/portable/package validation passed for `a1cbe2a`, after `8e37b6c`
completed: [run 40176649206](https://github.com/mamaru-a/deltagram/actions/runs/40176649206).
The checksum-verified GUI passed rapid repeated Continue (one provider/read and
one stop after closing) and idle Cancel (no provider/read/stop). Private evidence:
`backup-repeat-fixed.json` and `backup-idle-fixed.json`.

A complete transfer to official Desktop 2.62.0 also passed with pinned Core
2.63.0-dev. The restored disposable profile contained the prior archive/image
history, with matching image bytes. A message sent from the restored profile
arrived at the reference account and synchronized to the original Qt client.
A reply arrived on both restored and original devices. Core resumed mail IO
(connectivity `4000`), and the unrelated profile draft remained intact.
Private evidence: `backup-transfer-evidence.json`,
`handoff-backup-restored-send.json` and `handoff-backup-incoming-sync.json`.
Transfer QR files were deleted afterward; account secrets are not in this report.

An additional early-cancel replay found a separate race: delaying dispatch of
`provide_backup` by five seconds and closing immediately sent stop before the
provider existed. After the dialog disappeared, Core still returned a live
backup QR. Explicit cleanup stopped it. This was reproduced on `8e37b6c`; the
`a1cbe2a` UI guards do not change that ordering. Private evidence:
`backup-early-baseline.json`.

The follow-up moves ownership to `BackupTransfers`, returning an attempt-specific
cancellation handle bound to the original account. Cancellation during preparation
is retained until Core reports QR readiness; it then stops the provider once.
UI callbacks remain account-scoped, while cleanup survives dialog closure and
account changes. Duplicate providers for the same account are rejected until the
first completes. Only failed readiness reads are retried; provider starts and
stops are never automatically retried. Six new focused cases cover early/ready
cancellation, owner destruction, account isolation, late replies, readiness
read timeout and Core failure. The full focused suite passes 49 checks including
setup/cleanup, zero failures/skips.

The compiled `27656b4` helper also passed four direct checks against the running
disposable Core: early cancellation with a five-second provider dispatch delay,
cancellation after QR readiness, destruction of the owner during preparation,
and recovery from an injected readiness-read error. Each started exactly one
provider and stopped it once on the original account. Delayed cases stopped
after readiness (about six seconds after starting), with no QR UI callback.
Core returned to connectivity `4000` and the alternate draft stayed unchanged.
This used a private RPC shim to the existing Core owner, not a second process
opening the same account database. Evidence: `backup-backend-evidence.json`.
These are backend runtime checks, not the final GUI replay.

Native/portable/package validation for `27656b4` passed:
[run 40178483592](https://github.com/mamaru-a/deltagram/actions/runs/40178483592).
This includes all 49 focused checks, zero failures/skips, Debian 13 and Ubuntu
22.04 portable checks and distribution packages. The selected GUI/Core artifact
checksums were verified; symbols were not downloaded.

The verified GUI passed early cancellation with five-second provider dispatch
delay, reopening before readiness (one provider, visible duplicate rejection),
and switching to the alternate profile while preparation was pending. Each
started one provider and stopped it once on the original profile after readiness.
Both profiles returned to connectivity `4000`; the alternate draft stayed intact.
A subsequent readiness read timed out, confirming no provider remained.
Idle Cancel issued no backup/stop calls, and cancellation after QR rendering
issued one stop. Private evidence: `backup-early-gui-fixed.json`,
`backup-reopened-profile-switch-fixed.json`, `backup-profile-switch-no-provider.json`,
`backup-idle-276-fixed.json` and `backup-ready-276-fixed.json`.

A repeated complete transfer to official Desktop 2.62.0 also passed on this exact
GUI. The provider dialog closed without issuing stop, mail IO resumed, restored
image bytes matched, and a message from the restored profile reached the reference
account and synchronized to the original Qt client. A reply reached the original
and both restored profiles. The unrelated draft remained unchanged. QR files were
deleted afterward. Private evidence: `backup-transfer-276-evidence.json`,
`qt-transfer-276-final.png` and `reference-transfer-276-reply.png`.
Camera/mobile receive paths remain unverified.

### Clipboard-image restart loss — 2026-10-25

On the verified `a1cbe2a` GUI, a rejected clipboard-image send remained visible
with its caption and private staging file, but Core had neither a message nor a
draft. Normal quit/restart removed the local bubble and caption; no recovery
attachment or draft appeared. Other profiles had no matching message, and the
alternate draft remained intact. Staging-file deletion after quit was not
measured in this replay. Private evidence: `media-durability-rejected.json`,
`media-durability-restarted.json`, `handoff-image-durability-rejected.json`,
`handoff-image-durability-restarted.json` and the two image-durability screenshots.
This confirms a remaining data-loss path for transformed media; it is not repaired
by either the staging-lifetime fix or the failed-status presentation change.

### Failed-send status display — 2026-10-25

The rejected image/archive replays left a local failed message displayed with a
sending clock. Source inspection confirmed that both local failed flags and
Core state `24` were mapped to the pending indicator in message bubbles and chat
rows. Accessibility labels used donor flags and could announce a Core failure
as sent/seen.

The follow-up separates failed status from pending status, renders the existing
localized “Failed” label in the bubble footer and chat row, and uses Core's
pending/failed/read state in accessibility descriptions. It changes presentation
only; it does not retry an ambiguous send. The source publication audit passes.
The `2e051d8` native/package validation passed after `27656b4` completed:
[run 40179984156](https://github.com/mamaru-a/deltagram/actions/runs/40179984156).
All 49 focused checks pass with zero failures/skips, alongside Debian 13 and
Ubuntu 22.04 portable checks and distribution-package checks. The downloaded
GUI/Core checksums were verified; symbols were not downloaded.

The verified GUI shows “Failed” for a read-only Core reply fixture in state `24`,
the sending clock for state `20`, and read ticks after removing the fixture and
reloading the actual state `28`. These are display fixtures, not simulated SMTP
failures or database edits. A genuinely rejected local clipboard-image send
shows “Failed” in both the bubble and chat row. Explicit deletion removes that
local bubble and its private staging file without creating a Core/reference
message. The alternate draft remains intact, and injection policy was cleared.
Private evidence: `delivery-presentation-evidence.json`,
`delivery-core-failed-2e.png`, `delivery-core-pending-2e.png`,
`delivery-core-restored-2e.png` and `delivery-local-failed-2e.png`.
Assistive-technology runtime checks and broader RTL/keyboard coverage remain open.

The focused suite covers partial multi-stage deletion failure, but the tested
group delete dialog does not expose that combined path. Broader operational and
feature coverage below remains open. Native, package and the additional Ubuntu
22.04 portable-runtime gates now pass.

`docs/COMPATIBILITY.md` records wider feature gaps: inactive-profile
notifications, attachment staging/media navigation, member management,
keyboard/RTL and screen-reader coverage, webxdc, camera
workflows and advanced email presentation. Unsupported donor export, sharing
restrictions and auto-delete controls are disabled rather than routed to
Telegram. Calls remain unavailable before device permissions.

The initial failures and follow-up commits remain in Git/CI history. Private
fixture IDs, failure-injection policy, hashes and screenshots are kept under the
ignored `data/integrity-live/` and isolated test-container directories. Existing
unscoped draft files are retained on disk and never loaded into another profile.

## Initial publication — 2026-10-05

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
opens the local Core image in Telegram Desktop’s native media viewer, with
zoom, copy and save controls. Photo changes update the image used by the
viewer. Core accounts do not request Telegram profile-photo history.

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
Configured profiles switch through the backend. Unfinished setup accounts are
hidden from the menu and sidebar; the menu retains one **Add profile** action. No folder, message or account data is changed by the toggle.

A disposable Core account fixture with mail IO suppressed checked the actual
settings row, repeated on/off toggling without restart, switching between
configured profiles, photos/initials, active colors, a narrow window, and
setting/selection persistence after reopening the client. The release build
uses four GUI compiler jobs and retains incremental build records and caches.

## Linux cursor themes — 2026-10-07

The portable launcher now searches the user's and desktop's cursor theme paths
before a bundled Adwaita cursor fallback. Existing XCURSOR_PATH, XCURSOR_THEME
and XCURSOR_SIZE choices are retained. The standalone GUI also resolves desktop
icon paths when XCURSOR_PATH is unset. Clickable controls retain the donor's
standard Qt pointing-hand cursor; no custom cursor bitmap replaces it.

The previous portable runtime could not load a themed hand cursor. The updated
runtime successfully loads the fallback hand image. In the isolated Debian GUI
check, XFixes inspects the cursor Qt installed over the onboarding button and
requires antialiased alpha pixels, rejecting the old monochrome X11 fallback.
Core RPC, WebKit, GUI startup and keyboard checks also pass. These checks cover
the bundled fallback; the user's particular desktop cursor theme remains to be
confirmed on that desktop.

## Direct account editing — 2026-10-07

Settings → My Account opens the existing profile editor directly, with photo,
name and bio controls, instead of the donor's empty Stories profile page. The
click path, name editor and adaptive page/modal layout were checked in a
configured disposable Core profile at wide and narrow window widths. The
portable GUI/Core/WebKit/cursor checks and offline project tests passed.

## Native voice playback — 2026-10-07

Core voice attachments now expose their existing local file to the donor player.
The background waveform pass also supplies a fallback duration when Core has
none; an existing positive duration remains authoritative. Waveform counting
accounts for the bytes in each decoded sample, so 16-bit audio spans the full
waveform and seeking area. The UI refreshes the duration after analysis without
waiting for playback or changing Core's stored message.

A disposable Core fixture with mail IO suppressed sent and forwarded an
eight-second Ogg/Opus attachment whose reported duration was zero. The previous
build displayed `00:00` and a flat waveform. The updated client displayed
`00:08`, the full waveform, advancing elapsed time, native play/pause controls,
and seeking to the expected position. Original and forwarded bubbles and a
fresh client launch were checked. Playback used a null audio output in the
virtual display; physical speakers, microphone capture and live mail delivery
were not tested. Offline tests and the packaged GUI/Core/WebKit/cursor checks
passed. Release compilation retained the incremental build and used four jobs.


## Native channel photo viewer — 2026-10-07

Channel avatar clicks now pass the local image through the donor’s in-memory
photo loader and open the standard media viewer instead of a custom preview
box. This supplies the image dimensions and content required by the viewer.
The donor profile-photo history lookup is disabled for Core accounts.

A disposable Core channel with mail IO suppressed checked wide modal and narrow
page avatar clicks, full-image rendering, zoom, copy, saving a valid JPEG and
Escape returning to channel info. The portable GUI/Core/WebKit/cursor checks,
offline tests and source audit passed. No real account data was used.

## Web app software rendering — 2026-10-07

The portable launcher now configures GTK and WebKit software rendering in
addition to Qt. Raster-mode native webview helpers apply the same settings
on X11 and in external windows. Web content hardware acceleration is disabled
for the Cairo renderer; accelerated Qt configurations retain their existing
behavior. WebKit shutdown checks for an application instance before quitting.

The offline container regression renders a transformed, animated canvas with
missing graphics-driver paths and no GPU device. It starts a session bus to
exercise WebKit's sandboxed D-Bus proxy, which is now bundled and relocated
including its read-only sandbox mount arguments. WebKit's namespace and
seccomp policies remain enabled. These checks do not establish WebGL, camera,
WebRTC or every web app's functionality.

A disposable account also opened the actual Apps window from the attachment
menu and loaded the HTTPS app directory with unavailable GPU driver paths.
The native WebKit helper, sandboxed web process and session-bus proxy stayed
running without the reported GBM/EGL or GLib shutdown errors.

## History after profile switches — 2026-10-07

Profile changes close the previous chat view and cancel its pending loads,
then unload the reused histories after clearing their messages. Clearing a
conversation alone marked an empty history as fully loaded, which could
prevent a later visit from fetching older messages. History replies now
check the profile generation before fetching or applying message data;
queued replies and read-status updates also stay within their profile.

A disposable Core fixture used two profiles with the same Saved Messages
chat ID and twelve distinct messages each. Repeated switches loaded both
full histories. Delaying a history-ID reply for fifteen seconds while
switching away and back preserved the correct messages without reading
one profile's message IDs from the other. This covers local history loading
and cancellation; live server synchronization was not exercised.

### Apps software rendering and font fallback

The portable runtime maps generic CSS families and common web font names to
bundled DejaVu fonts. This prevents arbitrary host fonts from changing glyphs
and monospace column widths. The GTK WebView skips graphics offloading when
using the Cairo renderer, keeping software frames in GTK's normal repaint path.

Validation: `make test`, the incremental four-job release build, and the offline
portable WebKit canvas/startup/input checks passed. Fontconfig resolved serif,
monospace, and Times New Roman to the intended bundled families. The actual Apps
window loaded and was resized through four sizes on an X11 virtual display,
without EGL/GBM failures or an unresponsive web process. This virtual-display
check does not reproduce every desktop compositor's flicker behavior.
