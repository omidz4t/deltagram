# Publication validation


### Current native chooser and Telegram country catalog — 2026-10-27

[Run 40468612014](https://github.com/omidz4t/deltagram/actions/runs/40468612014)
at `227cfc4` passes all 24 CTest targets: 994 Qt checks, zero failures/skips.
The downloaded archive digest and all five artifact-file hashes match. Provenance
identifies merge `99be7f3`, parents `b713bff` and `227cfc4`; its tree matches local
`227cfc4`. GUI SHA256:
`9e521746ea175c9a283cbeed502fe2c2784afd479932d009fa05b8c7afc3fc3e`.
The preceding retained-RPC correction also passes the complete run
[40455308293](https://github.com/omidz4t/deltagram/actions/runs/40455308293)
at `1fea603`: 24 targets, 994 checks, zero failures/skips, verified archive/file
hashes and source-matching merge provenance. The `9714b03` CI lifetime failure
is resolved by that implementation; full GUI setup/export teardown remains open.

A fresh UID 65534 native GUI/store on a separate disposable display verifies the
chooser at 818x642 and 380x480 with screen-reader mode enabled. The description
now occupies 72px and all text is painted. At 380x480, the Telegram button is
fully inside the window (global y456..498, window bottom532), with Chatmail above
it and no overlapping copy. Both choices are visible. RTL, other interface
scales, long localized copy and persist-error presentation remain untested.

The fresh store has no country cache before backend selection. Explicit Telegram
selection, through the same account's MTP rebuild, creates a 21,458-byte catalog
with 232 visible countries. No `getting countries failed` error is logged.
Start Messaging and Log in using phone number reach the donor phone-login screen;
no phone number, code or password is entered and no QR token is captured. This
checks native unauthenticated country lookup, not Telegram authorization/session
persistence. No Core store/RPC is opened in this fresh Telegram-only replay.
The earlier `ef176e9` Core-plus-preauth-Telegram restart proof remains separate.
The test GUI quits normally; its own Xvfb is stopped. Evidence and bounded AT-SPI
snapshots are under `data/integrity-live/chooser-native-227cfc4/` (ignored).

This replay also exposed incorrect product copy on the donor welcome screen:
"Welcome to the official Delta Chat app." The source now uses separate Deltagram
language keys: "Sign in to Telegram" and "Use your Telegram account in Deltagram."
It changes no authentication or transport behavior. Full/subset language
generation and configured native syntax checks pass for `intro_start.cpp`.
The updated copy still needs a new linked artifact/render check. No production
or real Telegram account compatibility claim follows from these checks.


### Mixed backend entry and compact chooser — 2026-10-27

The complete artifact from [run 40438754289](https://github.com/omidz4t/deltagram/actions/runs/40438754289)
was downloaded and all five published hashes verified. Its head is `ef176e9`,
checkout `c145c1e`, and merge tree matches the local head tree at that revision.
The GUI SHA256 is `a1f930b473e609122fc8ec659fabe3cccc477c4bc7d08cb85d84c7ec6c5923d4`.
Its integrity log records 23 Qt suites, 962 passed, zero failures/skips. This is
newer than the previously verified complete `bd1ea60` artifact, but predates
`9714b03`/`1fea603` retained-RPC corrections and the source fixes below.

An isolated UID 65534 native GUI with its own Xvfb/DBus and private disposable
store shows the backend chooser before starting Core. Chatmail opens an offline
imported fixture; Add Account opens a second chooser without replacing it.
Core and Unspecified stages have zero GUI TCP sockets. Explicit Telegram
selection opens the donor login flow and phone-number screen, with one established
GUI connection to a built-in production Telegram DC on port 443. The Core child
has zero TCP sockets. No phone number, code or password was entered, and no QR
token was captured. This establishes unauthenticated transport/entry only.

Normal quit and cold restart retain backend sidecars `[Core, Telegram]` and reopen
the Core fixture. Before/after snapshots match: eight non-device chats, one draft,
nine blob hashes; snapshot SHA256
`a49512b769bd9d1f61dae6bf6b2d73a7a75dfc650c2e14c8ddced95d06a2d160`.
The GUI sent no add_account/configure/init_transports. Two start_io calls were
suppressed by the fixture proxy, so this is not live mail delivery proof. The
source archive remains byte-identical. Both test GUI instances exited normally
and the test-owned display/processes were stopped; existing desktop/store data
were untouched. Evidence: `data/integrity-live/mixed-native-ef176/` (ignored).

This artifact reproduces clipped chooser description text and a hidden Telegram
button at 380x480 with screen-reader mode. The source now gives chooser labels a
300px wrap style, reflows on label-height changes, and fits both buttons plus
errors to available height. Donor intro styles are unchanged. Style generation
and configured native syntax checking pass; corrected native rendering, error
copy, RTL and interface-scale replay remain pending.

Country catalog binding previously issued a request through Unspecified/Core MTP;
its asynchronous unsupported failure destroyed the observer before explicit
Telegram selection. The constructor now binds its sender only to Telegram MTP
instances, retaining observation across same-account MTP rebuild and backend
choice. An extracted exact-constructor recorder uses real rpl, account/MTP/domain
doubles, Qt 6.11.2 and strict ASan/UBSan as UID 65534: baseline fails nine of 11
assertions; corrected source passes all 11. Native syntax checking passes. This
is reactive binding proof, not actual native catalog/cache success. Evidence:
`data/integrity-live/countries-backend/` (ignored).

Periodic Core DELTA_NOT_SUPPORTED background requests were also observed and
remain unattributed. This country fix does not claim to resolve them. Authorized
Telegram login/session recovery, mixed authenticated message-ID isolation and
full current-source GUI regression remain open.

### Core file backup export and private creation modes — 2026-10-27

Advanced Settings now has an Export backup action for the owning Chatmail
session. It exports only the selected Core profile into a new
`chatmail-backup-XXXXXX` subfolder of the chosen destination. The optional
backup passphrase must match a second masked field and is forwarded without
trimming. Blank remains the Core null/unencrypted path. A failed attempt keeps
the masked values so a retry cannot silently become unencrypted; success and
closing the dialog clear/destroy them. Passphrases are not application settings.

The UI states the actual Core format: the passphrase protects the database,
not attachment files. A direct tar read of the disposable passphrase export
retrieves all nine attachment payloads without its passphrase, with hashes
matching the export source. The database header is not plain SQLite, and the
previous wrong/correct-passphrase restore test verifies its access boundary.
No whole-archive encryption is claimed or implemented by the client.

The bridge API rejects non-owning sessions before creating a directory or
accessing RPC. It captures the selected account ID, holds one export per RPC
across profile changes, and drops stale UI completion through AccountScope,
weak session/controller and box guards. Closing a dialog does not cancel a
submitted export or release its reservation early. Profile reset closes the
dialog. Only a typed null acknowledgement confirms success; errors/malformed
replies do not auto-retry. Each attempt has a distinct directory and does not
overwrite earlier output. On Unix, the directory helper refuses a newly
created folder whose mode is not owner-private. Extended ACLs and unsupported
filesystems are not established by these normal-filesystem checks.

DeltaRpc's Unix child modifier now applies umask 0077 before exec while retaining
Linux parent-death handling. It does not change the GUI process mask or chmod
existing files. A real DeltaRpc/QProcess test reproduces two failures with the
old code (new files 0666/0644 under parent masks 0000/0022). The corrected target
passes five Qt 6.4.2 checks with zero failures/skips as UID/GID 65534, including
0600 files, 0700 directories and an unchanged parent mask. This is POSIX mode
validation, not Windows ACL or macOS runtime proof.

The export helper target passes 22 checks as UID/GID 65534, including exact
parameters, directory uniqueness/parent preservation, invalid input, malformed
acknowledgements, cross-account serialization, and profile invalidation without
early reservation release or stale UI delivery, pending ownership after UI/bridge
loss, multiple RPC reservations, and idle registry cleanup. The full current Core integrity
target passes 307 checks, zero failures/skips, with the pinned Core server.
A separate unprivileged end-to-end driver links the actual DeltaRpc and export
helper to that server: both plain and passphrase archives are 0600 in distinct
0700 directories even while the parent mask is 0000. The protected archive
restores with matching non-device message-ID lists, drafts and blob hashes.
The source archive remains byte-identical; no mail-IO-starting RPC is called.

Language/subset generation and native syntax/type checks pass for all four
changed production translation units. Full application linking, new-dialog
rendering/keyboard/cancel/error retry, background completion, live-IO export,
profile/window teardown and release packaging remain pending. Private evidence
and the standalone Core driver are under `data/integrity-live/backup-export/`.

Normal quit/restart now checks all pending export reservations before the donor's
1500ms delayed-exit path. Closing the export dialog or invalidating its profile
scope does not remove the reservation held by the RPC callback. A blocked restart
returns before arming restart/update flags; the user explicitly retries after the
terminal reply. Export submission also refuses once shutdown has started, before
creating an output directory. Existing donor upload/download/export prevention
continues to run when no Core backup is pending. Startup/crash-recovery update
restart setters are outside the running export path.

An exact-source recorder with real Qt Core, the production reservation helper and
minimal application/window/RPC doubles reproduces 11 baseline failures. The fixed
methods pass all 16 assertions under ASan/UBSan: normal quit, normal/update restart,
owner loss, terminal release, donor upload prevention, donor restart intent, and
submission after shutdown. This is method-level evidence, not a native Application
or GUI replay. The two affected production translation units pass native syntax/type
checks after full/subset language generation. Normal close/quit/restart GUI replay
remains pending. Force kill, OS shutdown, power loss, and a permanently unresponsive
RPC are not recovered or cancelled by this guard. Private recorder evidence is
under `data/integrity-live/backup-quit/`.

### Qt 6.11 CI RPC-release correction — 2026-10-27

[Run 40439784146](https://github.com/omidz4t/deltagram/actions/runs/40439784146)
at `9714b03` links `bin/Telegram` and creates `deltagram.stripped`, then fails
`callbackCannotDestroyRpcInsideReadHandler` on Qt 6.11.2. Twenty-three of the 24
registered CTest targets pass; the failed run is not a validated release artifact.
The original retained-RPC helper passed Qt 6.4.2 but scheduled QObject deferred
deletion before invoking completion. A completion's nested event loop can then
release the last owner before the read handler returns. The same case reproduced
locally with the pinned Qt 6.11.2/GCC 16.2 toolchain; strict UBSan reports invalid
dynamic-type member access in `onReadyRead`. The first local native attempt had
an unwritable inherited Nix TMPDIR for UID 65534 and did not reach this case;
rerunning with TMPDIR=/tmp produced the actual diagnostic.

Retained RPC release now queues after completion and checks active dispatch depth.
A release arriving during another response/error callback's nested loop parks its
owner until the outermost RPC dispatch unwinds, then queues release again. The
start/call/stop/read/finish/transport-failure paths participate. A buffered second
reply and a buffered malformed-line event exercise subsequent non-owning callbacks.
A control that only moves deferred deletion after the first callback still fails
on that buffered second-reply case; it does not establish the required boundary.

The corrected real-process suite passes 32 checks with ASan/UBSan and strict UBSan
halt-on-error as UID/GID 65534 against pinned Qt 6.11.2. The suite includes the
existing export/stop/nested-loop cases, twenty live-setup/readback cases, and two
buffered reply/error cases. Every success checks eventual RPC release. The same
32 strict sanitizer checks pass on system Qt 6.4.2/GCC 13.2.0. Pinned Qt 6.11.2
also passes 307 Core integrity, 30 setup-action and five child-file privacy checks
with zero failures/skips as UID/GID 65534. Both strict sanitizer drivers also pass
against pinned Qt 6.11.2 and the actual pinned Core: export completes after owner
loss, and wrong-passphrase import completes its readback before an explicit
same-ID correct-passphrase retry. Checked IDs/drafts/blob hashes and source
archives are preserved, stale UI does not run, and retained RPCs release. No
mail-IO-starting method is used. Complete linked-app GUI teardown remains unverified. Private CI/control/fixed evidence is
under `data/integrity-live/rpc-native-release/`.

### Live profile-setup RPC ownership — 2026-10-27

Live add-profile setup now builds its mutation request with
`MakeOwnedProfileSetupRequest`. It uses the retained/deferred RPC handoff for
name/avatar writes, import, second-device copy, and transport setup, including
the `is_configured` readback after an error. The prior request captured its shared
RPC in the first callback only. With the UI/Bridge owner gone, terminal callback
cleanup could destroy that RPC inside `onReadyRead`; after an error the readback
callback did not retain the request either. The first-run QObject-parented RPC
path and Telegram intro are unchanged.

A raw-call adapter preserving the old live-request ownership reproduces UBSan
invalid dynamic-type member access in `DeltaRpc::onReadyRead`, both on successful
import and on error/readback. Strict `halt_on_error` makes those negative controls
fail immediately. This is not reported as an ASan heap-use-after-free: the
observed diagnostic is member access while the object's dynamic type is QObject.

The expanded permanent real-QProcess target passes 30 checks with ASan/UBSan,
strict UBSan halt-on-error, UID/GID 65534 and Qt 6.4.2. Twenty new cases combine
import, copy, classic creation and invitation setup with success, unconfigured
readback, configured readback, malformed readback and child failure during
readback. They release UI and external RPC ownership before the first reply,
hold readback separately, assert the correct final lease/outcome and exact
mutation/readback method sequence, drop stale UI, and verify eventual RPC release.
The existing 30 setup-action checks and both registered CTest targets also pass.
The process fixture simulates replies; it does not configure mail transports or
perform SecureJoin/copy against another device.

A separate strict sanitizer driver uses the real RPC/helper and pinned Core. It
creates a disposable encrypted archive, submits a wrong-passphrase import, drops
the UI-equivalent and external RPC owners, and observes Retryable after the real
unconfigured readback. Reopening Core confirms that target remains unconfigured.
An explicit correct-passphrase retry on the same ID also drops the owners and
succeeds; checked non-device message IDs, drafts and blob hashes match. The
checked source-profile snapshot and immutable input archive remain unchanged.
No mail-IO-starting RPC is used, no stale UI runs, and both retained RPCs release;
no unprivileged Core process remains afterward.

Native `welcome.cpp` syntax/type validation passes. Full native onboarding
window/session destruction, live transport setup, second-device transfer and
first-run cancellation remain separate gates. Private evidence is under
`data/integrity-live/setup-rpc-lifetime/`.

### Export RPC lifetime after session teardown — 2026-10-27

The export request closure held a shared RPC only until submission returned.
The pending callback kept its export reservation but not the RPC itself. Releasing
the session/Bridge's final RPC owner could therefore stop the child before export
finished. Export now submits through `CallOwnedDeltaRpc`, which retains an
unparented shared RPC through success or transport failure. Terminal completion
now queues release only when no RPC dispatch is active, parking/rescheduling an
owner if a subsequent callback opens a nested loop. The initial QObject deferred-
deletion approach passed Qt 6.4.2 but failed Qt 6.11.2; the correction and negative
controls above supersede that release mechanism.
Null or QObject-parent-owned RPC inputs are rejected. Existing ordinary calls,
explicit stop semantics, Core protocol/format and Telegram transport are unchanged.

The permanent real-QProcess Qt target passes ten checks under ASan/UBSan as
UID/GID 65534 with Qt 6.4.2. A pre-fix raw-call adapter against the old DeltaRpc
implementation fails six of those cases. The cases cover owner loss before a
success/error reply, child failure, malformed transport, no stale UI callback,
reservation cleanup, a nested completion event loop, explicit stop, stopped/null
RPCs and rejection of parent-owned objects. The fixed target verifies final weak
owner and QObject release rather than merely retaining the process indefinitely.

A second unprivileged sanitizer driver uses the actual DeltaRpc/export helper
with the pinned Core server and immutable disposable backup. It imports offline,
submits export, invalidates the UI scope and releases the session-equivalent RPC
owner before processing the reply. Export finishes, stale UI is not called, and
the retained RPC is released. Reopening Core confirms the original profile is
unchanged; restoring the exported archive matches checked non-device message IDs,
drafts and all blob hashes. Output remains 0600 under a 0700 folder; the input
archive is byte-identical. No mail-IO-starting RPC is used, and no unprivileged
Core process remains after completion.

All three registered CTest targets (RPC lifetime, child-file privacy, export
helper) pass as UID/GID 65534. Native `delta_rpc.cpp` and `delta_bridge.cpp`
syntax/type checks pass. This is real RPC/process and offline Core evidence, not
a full Bridge/session/window teardown GUI replay or live-IO export claim. It does
not recover forced process termination or a permanently unresponsive server.
Private evidence is under `data/integrity-live/rpc-export-lifetime/`.

### Profile deletion during backup export — 2026-10-27

The profile menu could issue `remove_account` for an exporting profile. Pinned
Core's removal path closes that database and recursively removes its profile
files; export's per-account ongoing lock does not guard account removal. The
client now shares a per-RPC reservation between export and deletion. Deletion
acquires before inventory lookup and holds through any switch away from the
current profile and the terminal removal reply. Export refuses while a deletion
is reserved, before creating its destination folder. A second deletion also
refuses. This applies to every profile on that RPC, retaining the existing
per-RPC export serialization boundary.

Terminal success/error releases before the UI callback. Last-profile refusal,
switch error, and discarded stale callbacks release ownership as well. Normal
profile switching remains allowed during export: its captured target ID stays
fixed, and invalidated UI completion stays dropped. Telegram account logout and
account deletion paths are unchanged.

The updated export-helper suite passes 22 checks as UID/GID 65534, zero
failures/skips, with four new mutual-exclusion/ownership cases. An exact-source
Bridge-method recorder reproduces eight baseline failures and passes all 20
assertions under ASan/UBSan. Cases cover removal before/during export, zero
output-directory creation on refusal, duplicate removal, current/inactive target
IDs, inventory/switch/removal phases, terminal errors, last-profile refusal,
expired owner/scope, and an explicit export after deletion completes. The recorder
uses real Qt Core and production reservation/scope helpers with inventory,
window/session and RPC recording doubles. Native `delta_bridge.cpp` syntax/type
validation passes. This does not prove a native profile-menu or linked-app
teardown replay; those remain open. No account was deleted in a live user store.
Private evidence is under `data/integrity-live/backup-removal/`.

### Deferred contact-open ownership — 2026-10-27

`ContactListController::rowClicked` already copied the completion callback
before posting it, because closing the contact list destroys its controller.
The posted zero-delay timer did not retain either the list-lifetime check or
the captured profile guard. A profile switch or list/window destruction after
the Core reply but before timer dispatch could therefore invoke the old open
callback. Checking only the RPC callback did not protect that second stage.

The normal open path now captures the account guard at click time and checks it
with the list's weak lifetime at both RPC completion and timer dispatch. The
callback remains copied outside the controller, preserving the existing safe
close-before-navigation behavior. Contact picking, member selection, identity
mapping, RPC parameters and donor Telegram contact behavior are unchanged.

An exact-source method recorder uses real Qt zero-delay timers and the
production `AccountScope` with minimal list/controller/RPC recording doubles.
The baseline fails four assertions; the corrected method passes all 19 under
ASan/UBSan. Cases cover normal deferred opening, controller death, profile
invalidation before timer execution, controller replacement, invalidation
before the simulated RPC callback, and error presentation. Native `delta_boxes.cpp`
syntax/type validation also passes. This does not instantiate the complete
PeerListBox, exercise a mixed-account window, or prove native GUI close/switch
behavior; that replay remains pending. Private evidence is under
`data/integrity-live/contact-dispatch/`.

### Encrypted backup import — 2026-10-27

Restore now offers a masked optional backup-passphrase field after file
selection. Blank input preserves the existing JSON-null unencrypted import;
nonempty input is forwarded without trimming. The dialog explains that the
backup passphrase is distinct from the local application passcode. The setup
lease remains held through the prompt, Cancel/close releases it, and submission
consumes the prompt before closing so the close hook cannot unlock an in-flight
import. The originating onboarding widget/account is rechecked after close.
The existing mutation acknowledgement/readback policy remains authoritative;
there is no automatic retry after an error or lost reply.

The setup-action target passes 30 Qt 6.4.2 checks as UID/GID 65534, zero
failures/skips, including exact whitespace/Unicode passphrase forwarding,
explicit same-ID retry after a wrong-passphrase error plus unconfigured
readback, and empty input remaining JSON null. Existing exclusivity,
cancellation, unknown-result and retry-open checks continue to pass.

On disposable offline accounts, the SHA256-verified pinned Core server exports
an encrypted archive with a generated passphrase that contains Unicode and
leading/trailing whitespace. Missing and wrong passphrases are rejected and
leave the same destination unconfigured. The exact passphrase then restores
that same ID with matching non-device message-ID lists, drafts and blob hashes.
The immutable source archive is unchanged, and no mail-IO-starting RPC was used.
Generated passphrases are absent from the checked Core diagnostics and account
metadata. They are not printed or recorded as evidence. This Core test and the
injected C++ tests do not establish native dialog rendering, cancel/focus,
clipboard behavior, wrong-passphrase UI retry, locked-profile recovery or
Telegram compatibility. Native GUI replay remains a release gate.

Language and subset generation pass, and the updated `welcome.cpp` passes
the configured native compiler in syntax/type-only mode after adding the
complete window-controller include. No object generation/linking is claimed.

Private evidence: `data/integrity-live/backup-passphrase/qt-result.txt` and
`core-result.json`, with the reproducible offline Core driver at
`data/integrity-live/backup_passphrase_core_check.py`.

### Interrupted offline database import — 2026-10-27

The same SHA256-verified pinned Core server and immutable disposable source
archive used by the extraction check also pass one SQL-write interruption.
A copy of the archive has a 64 MiB `txt_raw` probe on a retained normal-message
row, with no change to visible text, identity or message IDs. A full control
import first preserves the reference message IDs, drafts and blob hashes and
retains the probe. The first attempted control picked a device-notice row,
which Core intentionally deletes; that fixture attempt stopped before fault
injection and is not counted as a crash-recovery test.

On a new destination, the owned subprocess was stopped with its 67,710,976-byte
backup database fully extracted and its destination WAL grown from 2,401,992
to 7,214,152 bytes. The main database was still 4,096 bytes and the import reply
was pending. The stopped PID was confirmed before SIGKILL; its terminal exit
was awaited before reopening the same disposable accounts directory.

After restart, the destination remained unconfigured. Import retry on that
same account ID succeeds. The other restored profile preserves its message-ID
lists, draft records and blob hashes. Read-only follow-up inspection passes
`PRAGMA integrity_check` on all four reference/control/recovered databases;
every column of all 27 non-device message rows matches its appropriate control,
and the 64 MiB probe hash matches exactly. The original archive is unchanged.
No mail-IO-starting RPC was used, and no owned test process remains.

This is a measured destination SQL-write checkpoint, not exhaustive crash-stage
coverage, power-loss durability, a native onboarding/GUI teardown check, network
isolation or Telegram validation. No Core/client patch was required by this
result. Private scripts/evidence are
`data/integrity-live/backup_sql_interruption.py`,
`data/integrity-live/verify_sql_import_recovery.py`, and
`data/integrity-live/backup-sql-interruption/*.json`; generated accounts remain
under `/tmp/deltagram-live/sql-import-interruption-20261027-b`.

### Interrupted offline backup extraction — 2026-10-27

The pinned Core server from native artifact `bd1ea60` matches SHA256
`89cbcddf509b197618b17dd3c1137a966b2fcc89f2a08d974156ac9d7374f70f`.
A previously exported disposable archive remains byte-identical before and
after the check. A test copy adds one 64 MiB synthetic leading blob; successful
control import establishes that the augmented archive remains importable.

An owned Core subprocess was stopped while that first blob had 155,648 bytes
written, before backup database extraction. The import reply was still pending
and `/proc` confirmed the process stopped. Only that PID was killed; its SIGKILL
exit was awaited before starting a replacement on the same disposable storage.
The destination remained unconfigured after restart. Importing the original
archive into that same account succeeded. Another already-restored profile
retained exactly its non-device-chat message-ID lists, draft records and blob
hashes across termination, restart and retry. No mail-IO-starting RPC was called.

This is one measured pre-database extraction checkpoint, not a SQL-import crash
test, GUI close/recovery test, network-isolation proof or Telegram validation.
Private reproducibility/evidence: `data/integrity-live/backup_import_interruption.py`
and `data/integrity-live/backup-import-interruption/result.json`; generated
accounts remain under `/tmp/deltagram-live/import-interruption-20261027-a`.

### Blocked-contacts settings ownership — 2026-10-27

The Core blocked-contacts section now includes its bridge declarations and
checks session ownership before loading contacts. Replies and unblock buttons
retain the originating account guard, generation and refresh revision. Profile
reset clears rendered contacts without fetching through the old account during
the reset callback. Non-owning refresh shows an unavailable state with no list
request. Native profile-switch/unblock replay remains pending.

Ten additional settings sections pass native syntax/type validation, including
the corrected blocked-contacts file, premium, business, active sessions, search,
notifications, storage, advanced and chat settings. The focused native set is
now 52 distinct source files. This does not establish GUI or service behavior.

### Held Telegram authorization during unresolved startup — 2026-10-27

Unresolved/Core accounts no longer start queued or stale MTProto key deletion.
Authorization serialization retains the stored self ID when session publication
is held, and includes both held and already-running key-deletion queues.
Explicitly Telegram accounts retain the donor destruction path. The encrypted
account/auth format is unchanged.

An exact-method recorder reproduces nine failing assertions before the fix and
passes all 13 afterward under AddressSanitizer and UndefinedBehaviorSanitizer.
It uses the production authorization serializer, stale-key method and raw key
writer with synthetic key/account/MTP doubles; no real authorization, crypto
session or server deletion is used. The backend/store target passes 44 tests
and startup policy passes 13 as UID/GID 65534, zero failures/skips. The updated
`main_account.cpp` passes native syntax/type validation. Native corrupted-tag
restart, encrypted-storage writes and post-selection destruction remain pending.

### Native syntax checks for account-lifecycle guards — 2026-10-27

The current `main_domain.cpp`, `main_session.cpp` and
`settings/sections/settings_information.cpp` pass the actual incremental native
build's compiler commands with `-fsyntax-only`. Object and dependency output
flags are removed; Ninja records and ccache are retained. These checks use the
configured Nix/Qt headers and generated sources, including the new Core
container/passcode and Telegram app-config guards. All three compiler exits
are zero with empty diagnostic logs. This is syntax/type validation, not
object generation, linking, encrypted-storage replay or GUI proof.

The same native checks also pass `main_account.cpp`, `storage_account.cpp`,
`mainwindow.cpp`, `backend_choice.cpp`, `mtp_instance.cpp` and MTProto
`session.cpp`. The first main-window attempt found a raw `Main::Account*`
passed to `windowFor(SeparateId)`. The existing-Core chooser route now supplies
the donor `not_null` account wrapper; the corrected file and the remaining
authentication/transport files pass. No login or real connection is exercised
by these compiler invocations.

The restored conference implementation, call-entry gates, language/provider
routing, language box, settings builder/main page, main/filter menus, app badge,
main widget and session navigation also pass, bringing the focused native
syntax/type set to 22 distinct source files. Initial menu/navigation checks
found a second raw-account window lookup, an unsupported FlatLabel click API,
a missing complete Domain include, a shadowed session accessor and private
base-field access. The fixes use typed window IDs, the pinned donor label
handler, the Domain header and public session accessors. No menu interactions,
conference calls, native language switching or badge behavior are established
by compilation alone.

History/composer, dialog search, message/reply/element rendering and data-session
sources also pass, bringing this focused set to 28 files. Initial checks found
an unmatched parenthesis in the send-button disable expression and a captured
controller pointer called as a function in the QR callback. The Core readiness
condition is now a named boolean combined with the existing slow-mode rule;
the QR callback passes its captured controller directly. These checks do not
prove sending, search behavior, author painting or mixed-ID isolation at runtime.

### Passcode retention before Core session startup — 2026-10-27

Automatic empty-account passcode cleanup now requires a single, logged-out
Telegram account. Core containers and unresolved accounts retain their local
passcode and system-unlock preference while session startup is pending or has
failed. The zero-account guard remains before the active-account dereference.

The account-policy target passes 19 QtCore tests as UID/GID 65534, zero failures
or skips. A separate recorder compiles the complete production
`Domain::removePasscodeIfEmpty` method with account/storage/application doubles:
the previous method fails ten assertions for unopened Core/unresolved accounts;
the corrected method passes all 90 assertions under AddressSanitizer and
UndefinedBehaviorSanitizer. This verifies recorded passcode writes, unlocks,
preferences and return values across backend/session/count combinations, not
native encrypted-storage or lock-screen startup. Those native checks remain
pending.

### Telegram app-configuration ownership — 2026-10-27

`Session::appConfigRefreshed` now clears Telegram freeze/appeal, premium-purchase
and edited-date state for Core and unresolved accounts before reading Telegram
app configuration. Telegram keeps its donor configuration path. An exact-method
recorder reproduced eight failing assertions before the guard; afterward all
15 assertions pass in both ordinary and `OS_MAC_STORE` policy builds under
AddressSanitizer and UndefinedBehaviorSanitizer. The recorder uses configuration
and session doubles, not a native `Main::Session` or a macOS application.
Native cached-configuration migration and frozen-composer UI replay remain
pending.

### Core container cleanup policy — 2026-10-27

Donor redundant-account cleanup now retains a Core-tagged container when its
session has not opened. Previously, switching to another account could remove
that container from the live domain and the next saved account list. Telegram
and unresolved abandoned login slots retain the donor cleanup rules. The
settings Add Account availability check now uses the same reuse predicate as
`Domain::addActivated`, so a session-less Core container cannot be advertised
as a reusable Telegram slot.

The updated production policy helpers pass 17 QtCore tests on Qt 6.4.2 / GNU
13.3 as UID/GID 65534, with zero failures/skips. Native `Domain` cleanup,
account switching and persisted restart remain unverified by these helper
tests. No account-map, authorization or backend-sidecar format changes.

### Mixed-backend identity, conference dependency and release guards — 2026-10-27

Local source integration `9215659` includes per-account backend selection,
Telegram transport and donor conference restoration, and Core ownership gates.
It is not a native or Telegram-login pass. Session identity/prediction helpers
pass 15 checks, zero failures/skips, including Core with missing or unexpected
stored donor self data, Telegram user ID `1_000_000`, and unchanged Telegram
production/test identifiers. Native storage loading and mixed callbacks remain
pending. Core's old unprefixed account-order entries are intentionally not
aliased onto a real Telegram user; that can reset the Core row's old order.

The exact donor tdlib `51743dfd01dff6179e2d8f7095729caa4e2222e9` builds as
tde2e 1.8.49. The full application configures with it and the actual
`td_tde2e` wrapper compiles against its exported CMake package. Restored
conference call sources have not yet compiled or run. The existing incremental
release and ccache trees are retained while native Bridge compilation runs.

Release tooling passes 17 build/API-mode checks and 53 offline release checks
on Linux. Mode changes retain Ninja records, production builds require explicit
inputs, and missing/invalid cached mode forces configuration. Staging checks
binary SHA256, API mode, version file and CMake mode before replacing output;
publication checks production provenance and the CI source commit. Empty or
multiline version files and changed/missing version assets are refused. Python
lint/import checks, shell syntax, and workflow parsing without duplicate keys
pass. The workflow keeps production API inputs out of PR validation builds.
No production credentials have been requested or used, and real Actions
publication has not run. The macOS host test attempt lacks `flock`; it is not
reported as a passing Linux build-tool test.

### Native complete-message access — 2026-10-27

[Native run 40387188858](https://github.com/omidz4t/deltagram/actions/runs/40387188858)
succeeds at head `bd1ea60cec099602243c14a21c45a378fb319b06`, merge
`0807ae50182fae36adcaf64dd037688a79e2abe2`, base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`. All five artifact hashes match,
and the bundled test log records 827 passes, zero failures/skips. This compiles
the complete-message UI and language code generation. It predates the newer
mixed-backend routing, identity, entitlement and conference changes.

The disposable offline HTML fixture now has a visible Show full message footer.
Opening history makes zero `get_message_html` calls. One explicit click makes
one call and renders the HTML-only marker, heading and table; all are selectable
and copyable. The HTTP link opens a confirmation prompt, which is cancelled.
A separate synthetic reply containing a long body and image/frame/script
references to a local HTTP listener displays its first marker and scrolls to
its final marker with zero listener requests. The first synthetic attempt did
not open the viewer and is not counted; the second begins from a verified
history view. Local-file denial remains covered by the renderer tests, not by
this HTTP-listener observation.

Holding the real HTML reply for 15 seconds and closing Loading after 1.53 seconds
does not reopen the dialog when the reply arrives. The saved backup draft and
offline IO policy are preserved. The initial real-body case issues zero send
or chat-create requests. Private evidence is under
`/tmp/deltagram-live/backup-search-pager-a19/full-bd1-*`. Profile-switch
cancellation, mixed Core/Telegram runtime, and real long-mail delivery remain
separate gates.

### Backend integration checks and native encryption replay — 2026-10-27

A frozen source snapshot of the updated backend metadata, namespace allocation,
and startup policies passes 44 backend/storage checks and 12 startup checks on
Qt 6.4.2 / GNU 13.3, as unprivileged UID/GID 65534, with no failures or skips.
The initial compile found a missing test-fixture `sidecarBytes` accessor; the
accessor was added before the final build. Sparse namespace allocation beyond
index 5 preserves occupied map/auth/metadata sentinels. These tests do not run
native `Storage::Domain::startModern` or `Main::Account` reconstruction.
The unread aggregation helper separately passes eight checks, zero failures
or skips; its native menu and badge consumers remain untested.

[Native run 40376361341](https://github.com/omidz4t/deltagram/actions/runs/40376361341)
succeeded at head `f9c9f6f3fe43f379273e43eb8648f2a453a20ea2`, merge
`5b56b223e155dcbd83338cab5c10ead29bd093c4`, base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`. All five artifact checksums match;
the bundled Qt test log records 751 passes, zero failures/skips. This artifact
includes the encryption presentation and onboarding layout fixes, but predates
the backend identity, full-message viewer, and subsequent routing changes.

On that binary, the disposable group Encryption info dialog displays the Core
header and final fingerprint line. Selecting the body and copying it produces
the exact 170-character Core plaintext. Full-window default OCR missed most of
the dialog; cropped, enlarged OCR and visual inspection establish the visible
lines instead. With the encryption reply held for 15 seconds, closing Loading
after 1.65 seconds returns to Group Info and the late reply does not reopen the
dialog. The original fault policy is restored. No fingerprint/address contents
are included in the result JSON. Private results are
`/tmp/deltagram-live/native-f9-encryption-result.json` and
`/tmp/deltagram-live/native-f9-encryption-cancel-result.json`.
Long-body scrolling, profile-switch cancellation, and mixed-backend native
behavior remain separate gates. Telegram transport is still under integration;
these Core checks do not establish real Telegram compatibility.

A separate fresh disposable onboarding root verifies the same binary's avatar
layout. Personal copy sits below the avatar at 818×642, and the avatar center
opens the image picker. Team copy, name field, Next and privacy link remain
visible without overlap at 818×642, 818×480 and 380×480. The upper avatar edge
also opens the picker in the compact team form. This phase allocates one empty
local account but makes zero `set_config`, `init_transports`,
`add_or_update_transport`, or `start_io` requests. No profile is provisioned.
RTL and other interface scales remain untested. Private evidence is
`/tmp/deltagram-live/backup-avatar-layout-f9/native-result.json`.

The production-used domain account-limit policy separately passes 15 QtCore
checks, zero failures/skips. It restores donor 3/6 account capacity while
excluding Core/Unspecified from Telegram premium/test entitlement. Its initial
build exposed identifiers named `slots` colliding with Qt's keyword macro;
those identifiers were renamed before the final build. Native account creation
and persisted mixed-account restart remain pending.

The working-tree transport restoration now uses the immutable MTP instance
backend. Exact extracted production methods pass five dispatch checks and eight
startup/resume checks against recording doubles. Telegram performs one callback
store and signed registration, including the deferred dependency path. Core and
Unspecified avoid DC lookup/connection workers, and resume returns safely when
no private worker exists. Config/CDN/HTTP-time/DNS entry points are backend-gated
in source, but are not executed by these doubles. No sockets or login are used.
The extracted method hashes and results are under the ignored
`data/integrity-live/telegram_transport_harness/restored/` tree. Native compile,
real test-DC authentication and mixed-session runtime remain pending.

Three focused integrity slots for per-message author names, adjacent-author
grouping, and existing edit eligibility pass (five QtTest entries including
setup/cleanup), zero failures/skips. These execute production metadata helpers;
native HistoryView painting, quote selection, attachment grouping and contact
click identity are still untested.
The same frozen source then passes the complete integrity target: 307 checks,
zero failures/skips, using the pinned Core RPC server and disposable temporary
databases as UID/GID 65534. This does not compile the native account/UI routing.

The production-used Telegram entitlement helpers pass 11 QtCore checks, zero
failures/skips. Telegram premium follows the donor user flag, and purchasing
follows server policy with the donor blocked-by-default and Mac Store behavior.
Core/Unspecified flags cannot grant Telegram entitlement. Native Main::Session
consumers and server app-config updates remain untested. Publication audit of
the current tree checks 13,270 files with zero blocking findings; this checks
publication hygiene, not backend isolation or protocol behavior.

### Backend identity and complete-message source validation — 2026-10-26

`c6f3abc` adds explicit per-account backend identity and a separate versioned,
atomic metadata store. The donor encrypted map and MTP authorization serializers
retain their existing format. An exact Git-index snapshot builds on Qt 6.4.2 /
GNU 13.3 and passes 34 QtCore checks as unprivileged UID/GID 65534, with zero
failures/skips. Tests cover migration evidence, corrupt/unsupported metadata,
failed persistence, independent identities, private file permissions, and
unchanged sentinel map/auth bytes. They do not instantiate native
`Storage::Account` or prove migration of a real Telegram session. Running the
same permission-denial fixture as container root produces one failure because
root can write through the read-only directory; that run is not reported as a
passing suite.

`bd1ea60` adds explicit full-message controls and an on-demand Core HTML reader,
with owning-session checks before cache/RPC access and account-generation /
cancellation guards. Its exact staged helper and renderer each pass 21 checks,
zero failures/skips. The renderer forces bounded raster decoders, denies remote
and local-file resources and automatic navigation, and preserves valid
image-only messages. The previously failing image-only case is retained. Raw
subject is used only as a plain viewer title. Native compilation of the UI
translation units, language code generation, footer geometry, and real Core
full-message GUI replay remain pending.

Separate probes extract the current production transport methods verbatim into
recording doubles. Core dispatch remains inert; all three Telegram dispatch
cases fail because the global intercept still handles them. Five of eight
startup/resume cases fail, covering absent Telegram connections and attempts to
queue against a null private connection. These probes open no sockets and do
not authenticate. They establish the regression baseline for transport
restoration, not Telegram support.

Settings and message/data routing edits are under integration. Backend choice,
donor login, transport restoration, and mixed-session runtime proof remain
required. No personal Telegram account, credentials, or authorization files
have been used for these checks. The draft PR is not production-ready.

### Real Telegram requirement and multi-profile startup replay — 2026-10-26

The user explicitly requires real Telegram accounts alongside Core. Current
`MTP::Instance::Private::sendRequest` unconditionally intercepts requests through
Delta; `MTP::Session::start` returns before constructing its connection worker.
These global cuts do not provide Telegram account compatibility. A per-account/
session backend integration is active, including auth/storage and global Core-cache/UI
hooks. Removing those returns alone is not sufficient isolation. No personal
Telegram account/session or credentials are used in this audit.

Separately, verified Core-native `a19d9e2` passes four-profile cold-start faults.
A string-valued selected ID renders Retry with no guessed selection, opening or
allocation. After clearing that fault, rejected selection renders Open Profile
and does not start IO. A rejected config read after successful IO startup also
retains Open Profile. Clearing faults and retrying recovers into original profile
3 with IDs `[1,2,3,4]` unchanged. The complete sequence has zero add/import/
configure/config/send writes. Baseline drafts, history, requests and device
visibility remain preserved; fault policy is restored. Private evidence is
`/tmp/deltagram-live/native-a19-multi-startup/native-result.json`. This proves
Core recovery only, not the newly required Telegram backend.

The email subject audit also corrects the earlier presentation finding: Core
already prepends important classic/list subjects to `text` and deliberately
omits generated chat subjects from bubbles. Raw `subject` is appropriate for
the full-message title, not an additional bubble prefix. Per-message sender
override remains a separate missing presentation path; it must retain the real
sender contact identity rather than renaming global contact data.

### Full-message access gap and Core search boundary — 2026-10-26

An isolated offline Core fixture retains a shorter plain representation and a
full HTML body with an extra detail marker and table. Its message reports
`hasHtml: true`; `get_message_html` returns the complete fixture. Core search
finds the plain marker and does not find the HTML-only marker. That matches
Core's documented search over its stored text, not a Qt result-list failure.
No client HTML indexing is added.

On verified `a19d9e2`, the plain marker is searchable and its hit opens the
matching Saved Message with the saved draft preserved. The bubble and context
menu offer no full-message control; the GUI makes no `get_message_html` request.
Message info also lacks the extra HTML-only content. Native HTML-only search
shows No Results, matching Core. The GUI probe makes no send/create/accept/block
operation. Startup IO acknowledgements are stubbed without dispatch in this
offline copy. Private evidence is under
`/tmp/deltagram-live/backup-search-pager-a19/html-message/`.

Source review confirms this is an access defect: Core sets `hasHtml` when
simplification can omit content, including long-text truncation and MIME
remainders, and expects a Show full message control. Current Qt code only uses
the flag to refuse editing. An on-demand Core-backed viewer and controls are
under scoped implementation. Complete body access, resource/link restrictions,
account/cancellation handling and native rendering must pass before this gap
is closed. The offline fixture is stopped before restoring the main profiles.

### Native search account cancellation and load-more recovery — 2026-10-26

Verified `a19d9e2` switches from disposable profile 2 to 3 at 1.82 seconds while
the old search error is held for 15 seconds. After that old reply arrives, the
destination retains its normal chat list and empty search field, without old
hits, Loading or Failed/Retry returning. Earlier manual/menu attempts did not
land within the hold and are not counted as cancellation proof. Main profile
selection and all baseline data are restored. Private evidence is
`/tmp/deltagram-live/native-a19-search-account-cancel-timed/`.

Load-more uses a separate offline restored copy with 61 new local Saved Messages,
leaving the main history untouched. Startup IO acknowledgements are stubbed
without dispatch; search and message fetches use actual Core. Page one fetches
50 messages. Rejecting the remaining 11 preserves those first rows and shows
Failed/Retry below them; further scrolling does not automatically retry. After
clearing that fault, one Retry fetches 11 messages without another ID query.
The remaining rows through `00` render, original rows from `60` remain, the
total stays 61 and failure chrome clears. There are 72 message fetches across
the successful first page, rejected second page and explicit retry. The saved
backup draft is preserved; the GUI phase issues no send/create/accept/block/
config writes. Private evidence is
`/tmp/deltagram-live/backup-search-pager-a19/native-result.json`. This is paging
and failure-state proof, not mail delivery. The fixture is stopped before the
main disposable GUI is restored.

### Native startup and group-sidebar corrections — 2026-10-26

[Native `a19d9e2`](https://github.com/omidz4t/deltagram/actions/runs/40354448605)
compiles and passes **742 checks**, zero failures/skips. All five artifact
hashes match. Provenance identifies merge
`cbbfc2ec5a36fdbe5f5779f58a471f4451b18e43`, head
`a19d9e2973847d27dd02b649bca2f16b4a663719`, and base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`; independently read merge parents
match. It includes startup and sidebar corrections, but predates the newer
encryption dialog and onboarding layout.

Cold-start replay uses the single configured disposable restore destination.
With discovery delayed eight seconds, early Next/Enter/Space remain in Loading.
A rejected selected-account read shows Retry without selecting/opening or
allocating a profile. Clearing that rejection and retrying reaches Open Profile
after deliberately rejected opening. Unfocused Enter and Space each retry
selection/opening. The destination remains the single configured selected ID 1,
its backup draft survives, and the audit contains zero add/import/configure
writes. Mail IO is rejected before dispatch. Private evidence is under
`/tmp/deltagram-live/backup-lock-122/startup-a19/`. Multi-account malformed
selection and post-setup Profile-page keyboard cases remain separate gates.

The original encrypted group's sidebar now settles with its correct name and
two members: two FullChat reads over 10.56 seconds, compared with 3,813 on the
older binary. Closing produces zero reads during a three-second idle window;
reopening produces two in ten seconds. Switching to another group updates the
name/member list with three reads in ten seconds. Resizing from 1112 to 820 and
back restores Group Info with two reads and no Loading loop. The initial-open
audit contains no create/accept/block/send/config writes. Original profiles' drafts, history/request
state and normal device visibility still match their baselines. Fault policy
is restored. Private evidence is `/tmp/deltagram-live/native-a19-sidebar/`.

Source `6abce56` moves onboarding description copy below the avatar, caps the
title beside the photo and stacks the name/status/buttons below growing copy.
Available title width uses logical coordinates in RTL. Source audit has zero
blocking findings and whitespace checks pass. Native compilation and center-click,
compact/team/RTL layout replay remain required for this newer source.

### Native initial search error and stale-query cancellation — 2026-10-26

Verified `122a3ec` shows Failed / Retry when `search_messages` is rejected,
with no message-page fetch and no No Results claim. Clearing the rejection and
pressing Enter on the same query issues one new ID request and fetches its two
hits once; both rows display Device messages. A separate query-change check
holds the old query's error for 15 seconds, then starts the successful query.
After the old error arrives, the newer two results remain and Failed / Retry
does not return. Audits contain no create/accept/block/send writes. Fault policy
is restored after each check. Private evidence is under
`/tmp/deltagram-live/native122-search-id-failure/` and
`/tmp/deltagram-live/native122-search-query-cancel/`. Load-more failure and
account-switch cancellation remain separate native gates.

### Native setup-avatar ownership and layout finding — 2026-10-26

Verified native `122a3ec` selects and crops a synthetic red PNG in a separate
unconfigured onboarding destination. Delayed config dispatch keeps the submitted
`selfavatar` callback pending for 10.1 seconds: the unique staging file remains
present with owner-only read/write access until Core copies it. After terminal
callback completion, the staging file is gone and Core's separate blob remains
readable and red. Provisioning is rejected before dispatch; the destination
remains unconfigured and no mail IO starts. The first verifier incorrectly
looked directly in `/tmp`; a second explicit attempt uses the GUI's actual
private Nix temporary directory. This is two deliberate submissions, not an
automatic retry. Private evidence is
`/tmp/deltagram-live/backup-avatar-122/native-result.json`.

That replay also proves a profile-page layout defect: the description overlaps
the avatar button. Two clicks at its covered center do nothing; its uncovered
top opens the picker. The selected avatar still renders red and copies correctly.
The geometry correction is under scoped implementation. Native center-click,
compact/long-copy layout and widget-teardown ownership remain pending; the
passing file-lifetime check does not establish those behaviors.

### Native restore recovery, media and delivery — 2026-10-26

[Native `122a3ec`](https://github.com/omidz4t/deltagram/actions/runs/40348261095)
passes compilation and **735 checks**, zero failures/skips. All five artifact
hashes match; provenance identifies merge
`ddc697938d88f0e665d808df5ee7429cb6499046`, head
`122a3ec8b5f1f806b8cb6392522b41493c021b3e`, and base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`. The merge parents match.
This binary includes setup serialization/recovery and avatar staging, but
predates the cold-start, sidebar and encryption-dialog corrections.

In a separate disposable destination, cancelling the backup picker sends no
import and leaves the same fresh account available. One plain import uses a
null passphrase, preserves the saved draft and configures that single account.
Rejecting `start_io` before dispatch leaves Open Profile available; an explicit
retry targets the same ID without another import, account allocation or
provisioning. Late progress does not replace the terminal error. The attempted
in-flight click occurred after import completed, so it does not prove the
rendered setup lock. Cold-start failure recovery is a separate pending gate.

After removing the opening rejection, that restored profile opens successfully.
A restored photo renders from its new destination blob directory; all 985,178
bytes match the stopped source copy. One explicit GUI probe sends through Core
and reaches OutDelivered. After stopping the restored client and restarting the
main disposable client, the recipient group contains that exact probe once.
The original source profile stays stopped; no concurrent copy of that identity
runs. In the reverse direction, one new GUI probe from the recipient reaches
OutDelivered; reopening the restored client receives it exactly once. Its
history renders the full body with the correct group and sender. The restored
client is stopped before returning to the main test profiles. The restored
saved draft and all four main profiles' baseline drafts,
history, request states and normal device-chat visibility remain intact. Mail
IO is enabled only for this explicit delivery phase. Private evidence is under
`/tmp/deltagram-live/backup-gui-122/`. This does not prove protected-backup GUI
support, in-flight lock/teardown or avatar
selection in the native onboarding surface.

An additional fresh destination closes the timed-action gap above. The proxy
holds import dispatch for 30 seconds. Within 0.99 seconds of submission, while
Core confirms the destination is still unconfigured, Create, alternate setup
and Escape are attempted. The onboarding remains open and reports setup already
in progress. The audit contains one import and one destination allocation,
with zero config/provisioning/copy operations. That same import completes once,
preserves the backup draft, and reaches Open Profile after the deliberately
rejected opening. No mail IO starts in this check. Private evidence is under
`/tmp/deltagram-live/backup-lock-122/`. This verifies those in-flight actions;
cross-window contention and widget destruction remain separate native gates.

### Native local-language restart round trip — 2026-10-26

On verified `2908654`, a local custom `.strings` file that omits the new restart
key shows the app-neutral fallback once: “The app will restart to change the
language.” Confirming restarts into the custom fixture; selecting English
restarts into English. Capture utilities do not run during either restart.
The old English-switch process exits normally with code zero; its remaining
zombie process entry caused a `kill -0` wait timeout, not an application crash.
Baseline data remains preserved. Private evidence is
`/tmp/deltagram-live/language-native290/native-result.json`. This controlled
round trip does not attribute the earlier capture-associated abort.

### Native profile mutation replay and encryption presentation — 2026-10-26

On checksum-verified `2908654`, Edit name resolves the intended Core contact,
shows Core key-contact/encryption-availability flags and permits an empty private
alias. A temporary alias updates the profile and chat list; clearing it restores
the recorded empty baseline. Each explicit action issues one name write. No
chat creation, acceptance, blocking, deletion or send occurs during that replay.

A new synthetic contact/chat with only a local draft isolates deletion from
the original conversations. A rejected contact-only delete submits once, shows
an error and preserves the contact list and draft. After cancel/reopen and a new
confirmation, one contact delete removes the fixture from all three relevant
Core lists while preserving its existing chat/draft. Core still permits reading
the hidden contact, as expected. A rejected checked chat-delete submits once,
keeps that chat/draft, and does not proceed to contact deletion. After another
explicit confirmation with the checkbox checked, one chat delete plus one
contact delete removes only that fixture and unselects its exact active chat.
Normal quit, verified process shutdown and restart retain the deletion without
repeating either write. All four original profiles, their baseline drafts,
history/request state, the original private contact name and device-chat
visibility are preserved. No fixture message was sent. The initial fixture
verifier wrongly assumed the default contact list includes address contacts;
read-only reconciliation found this one in flags 4, without a second create.
Private evidence is under `/tmp/deltagram-live/native290-profile-actions/`.
The replayed name/delete UI bodies are unchanged in the newer source.

The encryption correction builds the final selectable, height-limited dialog
from complete plaintext with the donor box-label style. A separate cancellable
Loading box retains immediate feedback. Controller/account guards and an
explicit flag set at `boxClosing` prevent presenting after cancellation, including
the close-hook interval before widget destruction. Empty/whitespace replies are
unreadable rather than blank Verified information. Nonempty multiline text is
preserved. All eleven focused suites pass **751 checks**, zero failures/skips,
serially; the tested helper header and test source match the checkout. Source audit has
zero blocking findings. These checks do not compile the changed QtWidgets code
or prove complete real/synthetic text rendering, scrolling, cancellation or
profile-switch behavior; the new native replay remains required.

### Group sidebar signal correction — 2026-10-26

The native sidebar request loop is traced to repeated unchanged write-access
values. FullChat reads bump `ChannelRevision`; the write-access producer emits
the same boolean again; the third-column subscriber recreates Group Info and
starts another read. The source now filters identical booleans within that
producer and identical `(chat key, canWrite)` values after switching producers.
Chat changes and real permission changes still reach the existing handler.
Core permission observation and the unresolved-group restore tick are unchanged.
Whitespace checks pass. The prior 742 focused checks do not cover this widget
subscription; native compilation and sidebar open/reopen/restore replay remain
required before calling the loop fixed at runtime.

### Integrated native group replay and startup correction — 2026-10-26

[Native `2908654`](https://github.com/omidz4t/deltagram/actions/runs/40338239128)
passes compilation and **700 checks**, zero failures/skips. All five artifact
hashes match. Provenance identifies head `2908654`, merge
`4ca80d34a8da6a865c8381df6a95f1d91ab7ef28`, and base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`; the merge parents match those
head/base revisions. The standard GitHub downloader succeeded after the older
range downloader failed. This PR run does not verify portable packages.

Disposable native replay opens Group Info for the existing encrypted group,
shows the correct two members and omits donor contact controls. Member inspection
shows the intended contact profile without Mute. With RPC auditing enabled, the
inspection issues Core reads and no create/accept/block/send writes. Explicit
Message resolves contact 10 to existing chat 12, restores its original composer
draft and makes no create/accept/block/send call. Baseline history, drafts,
request state and device-chat visibility remain preserved.

Two native gates remain failed/unverified. The group Encryption info action
requests the correct Core method, whose nonempty result was confirmed separately.
OCR detected only the title, but a later synthetic rendering fixture proved that
OCR misses visible body text, so this does not prove a blank real dialog. Visual
inspection of that explicitly synthetic three-line fixture confirms that its
final line is clipped. The override is cleared; complete body presentation is
under correction/review. Toggling the third-column Group Info leaves Loading visible and produces
3,813 FullChat reads in a ten-second audit window. Closing the sidebar stops
that replay; the reactive feedback path is under correction. No account mutation
was used for either probe. Private evidence is under
`/tmp/deltagram-live/native290-fixture/` in the isolated test container.

The configured-startup correction now uses a production startup state for
discovery, explicit retry, known-profile opening and deliberate live Add Profile.
Creation stays blocked during unresolved/failed discovery; known-profile retries
select that same ID before opening. Next and keyboard activation share one
handler. Seven new focused checks pass, and all eleven Qt suites pass **742
checks**, zero failures/skips, serially. Source audit checks 13,247 files with
zero blocking findings. Native compilation and rendered startup retry/failure
injection for this newer correction remain pending; `2908654` predates it.

Archived-search replay also passes on `2908654`. The disposable device chat is
archived once, the GUI quits normally and its processes are verified stopped,
then the same binary restarts without the chat in its ordinary list. Rejecting
BasicChat metadata makes the search show Failed / Retry, rather than No Results
or placeholder names. After clearing the rejection, one explicit Retry reuses
the result IDs and fetches the two message hits once. Both rows display Device
messages. Opening a hit retains that header and Mute-only controls, with no
composer. The audit records no create/accept/block/send calls. The chat is
restored to Normal visibility; original drafts, history and request states
match their baseline. Private evidence is
`/tmp/deltagram-live/native290-device-search/native-result.json`. This does not
cover initial-ID failures, load-more failures, account-switch cancellation or
HTML search.

### Setup-avatar isolation — 2026-10-26

Profile creation stages each selected avatar in a distinct owner-private
temporary file. The pending Core `selfavatar` callback retains the file owner
until Core finishes copying it, including when the onboarding widget closes.
This replaces the shared temporary filename that concurrent setups could
overwrite. Qt's non-movable temporary file is held through `unique_ptr`.

All ten focused Qt suites pass **735 checks**, with zero failures or skips,
serially. The eight avatar checks cover distinct bytes/paths, private access,
callback retention and cleanup. The tested avatar header and test source match
the checkout. An actual helper/DeltaRpc/pinned-Core check uses two temporary
offline accounts: each retains its distinct red/blue image after callback
completion removes the staging file, and Core's copies remain readable. No
mail IO starts. Source audit checks 13,245 files with zero blocking findings.
Native onboarding compilation and GUI avatar-selection/teardown replay remain
pending; the helper tests do not establish those UI behaviors.

### Configured-profile cold-start recovery — 2026-10-26

The isolated restored-backup profile was reopened on the verified `cacc5ce`
GUI with `start_io` rejected before dispatch. Core still reports its single
configured account, but the welcome surface shows Add Profile / Create New
Profile and the opening error, without a retry-open action. Create was not
clicked; no import or provisioning was submitted. This cold-start route is
separate from the post-import Completed-state recovery correction. Its current
source path is under scoped review.

Private evidence is `backup-gui-fixture/startup-recovery-evidence.json` and
`startup-recovery.png` in the test container. The isolated client was quit
normally and verified stopped before restoring the main disposable client.
Readback then confirms the original drafts, requests, group history and normal
device-chat visibility remain intact. The version regressions (2), release-build
regression (1), Python script regressions (24), and shell syntax checks also
pass on `450c8de`; these do not prove native startup recovery.

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
The source audit checked 13,223 files with no blocking findings.
[Native/portable/package validation at `06ff824`](https://github.com/mamaru-a/deltagram/actions/runs/40239506691)
passed all 259 checks with no skips. The GUI and Core checksums were verified
before replay. A rejected blocked-list request displays an error rather than a
false empty list. The first rejected block reached one write and a state
readback, then the native GUI crashed. On restart, Core confirmed the contact
was still unblocked, the chat remained listed, all baseline message IDs were
present and both drafts were unchanged.

Source review found that the contact-selection callback can destroy its own
picker while executing. The existing ordinary contact-opening path already
copies and defers its callback; selection now does the same, with picker and
profile lifetime guards. A traced repeat retained the data but did not refresh
the list after completion. A second traced replay with allocator poisoning
reproduced a double-free while destroying the settings refresh callback retained by `ConfirmContactBlocking`.
Matching checksum-verified symbols resolved the callback destruction through
`Bridge::setContactBlocked` and `DeltaRpc::handleLine`.

[Validation at `44a91b5`](https://github.com/mamaru-a/deltagram/actions/runs/40242501856)
passed native compilation, all 259 checks, portable runtime checks and package
verification. Disposable replay on the checksum-verified GUI then passed:

- A rejected block submits once, shows failure, refreshes the blocked list and
  keeps the GUI alive. Core preserves the unblocked state, existing history and
  both drafts.
- A committed block with a lost acknowledgement is confirmed by readback. The
  contact appears in the blocked list, its chat row disappears and the active
  conversation closes without deleting its composer draft. The audit records
  one block attempt in the original profile.
- An incoming probe from official Desktop 2.62.0 is stored by Core as state 16
  while blocked, with no fresh count or visible conversation. Blocked state,
  the hidden row and both drafts survive normal quit/restart.
- Unblocking with a lost acknowledgement is confirmed by readback. The chat
  row returns and the blocked list becomes empty. A new reference-client probe
  is fresh (state 10), with a blue unread badge. Opening the conversation clears
  unread state, displays each probe once and restores the original composer.
- Switching to the alternate profile during a delayed contact preflight
  invalidates the request chain. The audit observes the old profile's read but
  no block/unblock write after the switch. Both profile drafts remain intact.
- After restoring the onboarding profile and normal quit/restart, Core retains
  the unblocked state, all baseline message IDs, both probes, no fresh messages
  and both drafts. Fault policies are cleared and one project GUI/proxy/Core
  ownership chain is verified.

These checks cover the direct-contact settings flow. Group-member profile
navigation, broader donor entry points and the remaining compatibility gates
still require their own evidence. PR #1 was merged externally at `ea5bec5`;
[draft PR #2](https://github.com/omidz4t/deltagram/pull/2) carries the callback
correction and its evidence.

### Unsupported passkey controls — 2026-10-26

On the `d428416` disposable GUI, Settings search for “passkey” offers both Add
Passkey and Passkeys. These controls use Telegram authentication APIs and have
no Core implementation. Core profiles now omit both registrations and the
privacy-panel button; legacy passkey links return to Privacy and Security.
Direct creation-box entry is guarded. Opening privacy settings also no longer
starts the unused Telegram auto-delete reload.

The same baseline GUI exposes “Disconnect all websites”, “If inactive for” and
“Terminate all other sessions” when searching for “terminate”. Core profiles
now exclude the donor sessions, connected-websites, folders and global
Telegram auto-delete sections from search. Their constructors stop before
loading donor data. Standard settings navigation checks section availability
(including parent sections), so a saved URL to an unavailable section returns
to the supported main panel. Core blocked contacts, local passcode and device
preferences remain registered.

[Validation at `ea5bec5`](https://github.com/mamaru-a/deltagram/actions/runs/40241348390)
passed native compilation, all 259 checks, portable runtime checks and package
verification. On its checksum-verified GUI, “passkey” and “terminate” searches
show No Results. A legacy passkey-creation link opens Privacy and Security with
Local passcode and Blocked users still present. Legacy website-disconnection,
session-termination, folder-creation and global auto-delete links open the
supported Settings panel; all four rendered captures are byte-identical. The
single-instance URL checks retained one project GUI/Core owner. After the
instrumented crash replay and normal restart on this artifact, Core confirms
the original unblocked contact, chat-list entry, message IDs and both drafts.
The contact-picker fix is a later change and remains separately unverified.

### Additional Core entry-point review — 2026-10-26

The follow-up source review found Telegram cloud-password/login-email factories
outside the settings registry and CodeBlock handlers that could run before
standard settings navigation. Core profiles now reject those factories and
redirect the affected cloud-password, login-email, privacy, gifts, peer-color,
my-profile and call-list entry points to supported panels. The Main settings
folder button is omitted as well as its section/search registration. Local
passcode factories retain their existing behavior.

The review also found remaining `IsSystemUser(int)` conversions and profile /
contact-status blocking actions using donor APIs. System-user membership now
keeps full-width peer IDs; direct-chat narrowing is range-checked before Core
requests. Profile, peer-menu and contact-status blocking share the Core-backed
predicate and operation. Reserved contact IDs are excluded. Device-chat type
membership survives absence from the normal chat list, which excludes archived
chats; absence is not interpreted as deletion or a type change. Deferred picker
selection clears its busy state only while its controller remains alive.

The serial focused Qt target passed **260 checks**, with no failures or skips,
including the sender-ID / Core-ID numeric boundary case. The source audit
checked 13,223 files with no blocking findings. This focused target does not
compile the complete native UI. Full native compilation and rendered replay of
these additional changes are pending. Required cases include cloud-password /
login-email links, local passcode, the profile and contact-status blocking
controls, group-member profiles, and archived device-chat protections.

Two upstream builds were already active when this work was ready. Cancellation
of the PR-triggered duplicate was refused with HTTP 403 because `mamaru-a`
lacks upstream admin rights. After the main build completed, the follow-up was
pushed to the same PR branch; its build queued behind the existing PR run under
the unchanged concurrency lock. No heavy local build was started.

### Group-sender Message routing — 2026-10-26

A disposable two-member group exchanged one message in each direction with
official Desktop 2.62.0. On the checksum-verified `44a91b5` GUI, clicking the
incoming sender name and then **Message** requested Core history and drafts
for chat `10`; Core's contact lookup identified the intended direct conversation
as chat `12`. The audit recorded only method names and numeric account/chat IDs.
No message was sent through the wrong conversation. Returning to the direct
conversation preserved its draft, the alternate-profile draft and prior history.

The source correction resolves sender contacts through Core before opening
history, rejects unmapped sender identities and prevents out-of-range peer IDs
from narrowing into Core chat IDs at the request parser. Sender profiles omit
the chat-level mute control. History metadata also refreshes device-chat
classification for chats absent from the normal list. Matching positive numeric
chat IDs are required before metadata changes classification or cached full-chat
information; unknown types preserve prior Direct/Group membership. History still
loads when metadata fails.

Native PR validation at `e669e85` passes 262 checks. Its checksum- and
provenance-verified GUI now passes the previously failing route: incoming sender
name opens the profile without an accept, block, send or create-chat call. The
profile omits the chat-level mute control. Message performs one Core contact
resolution and loads direct chat `12`, with no history/draft dispatch to chat
`10`. The original composer is visibly restored; no message is sent. Core
readback confirms prior history, both drafts and all four prepared request
fixtures remain unchanged. Evidence is
`/tmp/deltagram-live/request-fixture/e669-sender-message-verified.json` and
`e669-after-message-preservation.json`; rendered captures are
`/tmp/e669-sender-profile.png` and `/tmp/e669-sender-message.png` in the container.
A stopped-state checkpoint was taken before replacing the disposable GUI, and
one GUI/proxy/Core ownership chain is verified afterward. This revision predates
the later settings, permission and request-UI changes; those still need their
own native compilation and replay.

An archived device chat can still briefly offer a composer before metadata
arrives, and the bridge send paths do not independently check device membership.
Core rejects sending to device chats, but this client-side permission gap remains
open. Core-blocked contact state is not synchronized to the donor's
`UserData::isBlocked()` flag. Core's `Chat::why_cant_send` does not reject blocked
contacts, so copying the donor restriction would impose a policy Core does not
provide. Official Desktop 2.62.0's
[`Composer`](https://github.com/deltachat/deltachat-desktop/blob/v2.62.0/packages/frontend/src/components/composer/Composer.tsx#L569)
uses contact-request actions and `canSend` to decide whether to render its input.
The next permission slice must follow that authoritative field rather than the
Telegram blocked-state flag. These questions are separate from the routing
correction.

Read-only disposable Core checks confirm `canSend: false` for device chat `11`
and `true` for Saved Messages `10`, the direct contact chat `12` and group `13`.
No outgoing request was made by this permission probe.

The final serial focused Qt target passed **262 checks**, with no failures or
skips, including numeric sender boundaries and matching-ID / known-type metadata
policy. The source audit checked 13,225 files with no blocking findings. The
Linux scripts suite passed 24 cases, including five behavioral artifact-staging
cases; ruff, isort, actionlint and shell syntax checks passed. The release-script
regression passed in Linux; its host macOS attempt exited before its assertions.
These checks do not compile the complete native UI or prove the corrected
sender-profile routing in the GUI.

The group's first-message notice is a historical Core info message, not a live
promotion banner. Readback confirms `isUnpromoted: false` and `canSend: true`
after delivery; message `284` retains the notice with `isInfo: true`. Core's
`ChatId::add_start_info_message` stores this message during group creation.
Its continued presence is not evidence of a promotion-state defect.

### Unpublished PR validation artifacts — 2026-10-26

PR builds now stage the stripped GUI, Core, version and integrity-test log with
checksums and machine-readable provenance. PR artifacts record both the head
revision and the merge checkout revision; the artifact name uses the latter.
Manual unpublished runs retain their symbol map. The upload remains limited to
successful unpublished builds, with three-day retention. Publication permissions,
action pins, cache policy and the serial PR concurrency lock are unchanged.
Offline staging checks cover file sets, checksums, provenance, stale symbol
cleanup and missing inputs. Real PR artifact verification now passes for
[run `40251516509`](https://github.com/omidz4t/deltagram/actions/runs/40251516509):
head `e669e85`, merge checkout `b3626bb`, base `b713bff`. All five payload
checksums pass; the manifest contains GUI, Core, version, test log and provenance,
with no symbol map. Provenance matches the run/repository and GitHub's merge
parents. The workflow and staging-helper blobs are identical to the current
ones. Its native log passes 262 checks. This older GUI reports 0.2.1 and predates
the settings/permission/request-UI changes; it cannot validate the latest head.

### Native settings compile correction — 2026-10-26

[Native run `40288544930`](https://github.com/omidz4t/deltagram/actions/runs/40288544930)
at `7e6bf1e` fails in `deep_links_settings.cpp` before the smoke-test step.
Inside `Core::DeepLinks`, unqualified `Settings::Type` resolves to
`Core::Settings`, producing the first missing-type error and the subsequent
conversion-to-int diagnostics. The redirect signature now uses global
`::Settings::Type`. The two public deep-link section fields use the same
qualification, removing their dependency on include order.

A syntax-only compile of the actual header after a `Core::Settings` forward
declaration reproduces both field errors before the correction and passes after
it. The source audit checks 13,228 files with zero blocking findings. This does
not compile the complete settings translation unit or later UI objects; a new
native run must prove those. No release cache or Ninja record was removed, and
no additional heavy local build was started.

The corrected `14878ed` native run
[`40293239947`](https://github.com/omidz4t/deltagram/actions/runs/40293239947)
passes the complete GUI compilation plus 305 integrity and 82 request-action
checks, with no failures or skips. All five validation payload checksums pass.
Provenance matches head `14878ed`, merge checkout `2c067bf`, base `b713bff` and
GitHub's merge parents. The downloaded GUI reports 0.2.1. This proves the newer
settings, permission, request UI and language code compile; it predates the
subsequent contact-open correction.

### Native permission refusal — 2026-10-26

On the verified `14878ed` GUI, Device messages displays Mute without a composer
or request actions. For an ordinary sendable chat, a fault-injected FullChat
claims `canSend:true` for a different ID. The composer retains its original
draft, but Send displays the availability-unknown error, performs a fresh
metadata read and makes no `send_msg`/`misc_send_draft` call. Core readback
confirms both profile drafts and prior message IDs are unchanged. The fault is
cleared afterward. Evidence is
`/tmp/deltagram-live/request-fixture/native-wrong-permission-evidence.json`;
captures `/tmp/14878-device-chat.png` and
`/tmp/14878-wrong-permission-refused.png` show the rendered results. This does
not cover every attachment/notification send surface or archived-device timing.

### Additional settings audit and baseline — 2026-10-26

Read-only source review found more CodeBlocks that bypass section availability,
including proxy settings and Telegram identity/catalog controls. On the
checksum-verified `44a91b5` disposable GUI, `tg://settings/data/proxy` opens a
Proxy settings dialog with IPv6, system/custom proxy controls and explanatory
Telegram text. The dialog was closed without changing a setting. One project
GUI/proxy/Core ownership chain and the existing drafts/history remain intact.

`tg://settings/language` opens the English fallback picker, including Telegram
translation controls. It does not reproduce a missing-picker failure. Source
inspection confirms `requestLanguageList()` inserts the default language before
attempting the unsupported catalog request. Local custom-language import and
the built-in/custom wallpaper gallery also have local implementations; the
cleanup must preserve them. Those changes were still pending at this baseline.

### Native settings replay — 2026-10-26

On the verified `14878ed` GUI, single-instance URL dispatch reaches the following
rendered destinations without starting another GUI/Core:

- `privacy/2sv` and `login_email`: Privacy and Security, containing Local
  passcode and Blocked users, without the Telegram cloud-password/email flow.
- `calls/all` and `calls/start-call`: local Speakers and Camera preferences.
- `data/proxy`: Advanced, with Relays and local storage/download preferences.
- `folders`, `premium` and `faq`: Main Settings. The Main list omits Folders;
  settings Search opens and the FAQ query has no results.
- `experimental`: Experimental settings remains available; searching IPv6 has
  no matching control.
- `language`: the picker opens immediately with English and local `.strings`
  import, without translation controls in this environment.
- `data/storage`: Local storage opens. Its Telegram-cloud retention promise is
  incorrect for Core and is under separate review; Clear all was not pressed.

Local passcode Create, Check and Change work through the Core settings allowlist.
The disposable passcode was changed; the old code was visibly rejected and the
new one opened management. Disable restored Off. A normal restart retains Off,
the request-action states, prior history and both profile drafts. Temporary
codes stayed in private files, were never printed, and were deleted after
disabling the passcode. No cache limit, relay, language or media preference was
changed. Captures are `/tmp/14878-settings-*.png` and
`/tmp/14878-local-passcode-*.png` in the test container, with URL/PID records in
`/tmp/deltagram-live/settings-fixture/`. Preservation checks are
`native-request-actions-native-settings-before-restart.json` and
`native-request-actions-native-settings-after-restart.json` in the request
fixture directory. This does not validate every settings/search route, physical
devices, custom-language import or wallpaper/theme persistence.

The same `14878ed` GUI opens Chat Settings → Create new theme → the local color
editor. Its primary action is Export theme and opens a local save dialog.
Export produces a valid 344,626-byte ZIP containing `colors.tdesktop-theme` and
`background.jpg`; CRC checks pass and the palette contains `windowBg`. The
archive SHA256 is
`b69aa76d621c066e7712aae53cf82ad9a5e170460d491892ca2d746abed397d5`.
No palette color or wallpaper was edited. The editor disables auto-night mode;
the original System setting was restored after closing it. Core readback
confirms prior drafts/history and recorded request-action states are preserved.
The private archive and member hashes are in
`/tmp/deltagram-live/settings-fixture/native-local-theme.tdesktop-theme` and
`native-local-theme.evidence.json`. Captures `/tmp/14878-theme-*.png` show the
menu, editor, save dialog and restored setting. Theme import remains untested.

During follow-up fixture arrival, a native popup displayed an Unknown title
with the encryption-info notice. Sanitized Core metadata identifies ChatE2ee
info messages separately from ordinary text; the current fresh-message list
does not include those info rows. This is a separate notification/service-event
finding under source review, not a passing presentation check. Private captures
and `notification-fixture/info-popup-{fresh,candidates}.json` retain the evidence.

Source review confirms the bridge feeds both `IncomingMsg` and `MsgsChanged`
through `addIncoming`. Its non-fresh branch uses `NewMessageType::Unread`, so
the noticed ChatE2ee info row can create a popup before the dialog name is
loaded. `Unknown` is the client's empty-name fallback. Core emits that local
notice as `MsgsChanged`, not `IncomingMsg`; it is not in the fresh-message list.
The correction keeps these updates in the transcript without a popup and
resolves a Core name before notifying incoming user messages. Only an
`IncomingMsg` with matching message/chat IDs, boolean `isInfo: false` and fresh
state 10 takes the Unread path. Drafts and malformed/mismatched messages are
skipped. Mute and name lookup failures preserve the transcript without a popup;
a legitimate Core name of `Unknown` remains valid. Existing photo/document
updates call the donor's public content-update method before processing as
Existing, so media refresh does not acquire notification side effects.

Serial focused tests pass **305 integrity, 82 request-action, 64 contact-open
and 59 notification checks**, with no failures or skips. An isolated QtCore
harness extracts the exact current `Bridge::addIncoming` method and passes
18 runtime cases covering event classification, name/mute reads, account
invalidation, preview/event ordering and existing media updates. The earlier
round-25 method fails the existing-media regression under the same current
headers. The final case also requires exactly one public media update. RPC,
MTP/session/media and notification scheduling are stubbed: these results do not
prove donor `mainView`, native media replacement or rendered popup behavior.
Private logs are `/tmp/incoming-round28-focused-tests.log` on the host and
`/tmp/incoming-bridge-{old,new}/results.json` in the test container. Full native
compilation and notification/media GUI replay remain pending.

### Pending-contact opening and lost-ack group decline — 2026-10-26

[Native validation at `4b5e093`](https://github.com/omidz4t/deltagram/actions/runs/40302056479)
passes compilation and **451 checks** (305 integrity, 82 request-action and
64 contact-open), without failures or skips. All five artifact payload hashes,
head `4b5e093`, merge checkout `66fa52e` and base `b713bff` match provenance and
GitHub merge parents. This binary predates the storage-copy and notification
corrections.

In the alternate disposable profile, pending Group `14` → sender profile →
Message opens existing pending Single `13` with Block/Accept controls and no
composer. The audit records one `get_chat_id_by_contact_id` for contact `13`,
followed by history/draft reads for chat `13`; no create, accept, block or send
occurs. Both requests remain pending and the original drafts/history survive.
Private evidence is `request-followup-fixture/native-open-request-message-open-evidence.json`;
captures `/tmp/4b5-request-sender-profile-top.png` and
`/tmp/4b5-message-request-loaded.png` show the route.

A separate confirmed Delete of Group `14` submits one `block_chat`. The
test proxy records Core's successful null acknowledgement, then replaces it
with a lost-ack error for the GUI. Core subsequently reports the group unreadable
and absent from the list; the GUI reports that the change could not be verified
and makes no second write. It does not infer acknowledgement from a generic
read error. The remaining Single request stays pending, the sender stays
unblocked, and original drafts/history are unchanged. Selecting that Single
shows its original Block/Accept overlay. Fault policy is cleared. Evidence is
`request-followup-fixture/native-lost-group-delete-evidence.json`, with captures
`/tmp/4b5-lost-group-{confirm,after,settled}.png`.

Contacts → the accepted direct conversation also opens correctly: one lookup
for alternate-profile contact `11` loads chat `12`, without create, accept,
block or send calls. Both original drafts/history remain unchanged. Evidence
is `request-followup-fixture/native-accepted-contact-open-evidence.json` and
`/tmp/4b5-accepted-open.png`. The Saved Messages menu opens chat `10` and
visibly restores the alternate draft (`/tmp/4b5-saved-messages-open.png`).
Contacts intentionally filters out the self row, so that menu check does not
exercise `OpenCoreContactChat`'s self-contact branch. New/blocked contact-open
routes and broader native compatibility gates remain open.

The same binary exposes a separate group-profile gap. Core identifies primary
chat `13` as Group with members `1` and `10`, but its header opens the donor
contact profile: Edit contact and Delete contact are present and no member list
is available. Neither action was activated; the audit records no mutation.
`request-followup-fixture/native-group-header-finding.json` and
`/tmp/4b5-group-profile{,-more}.png` retain the evidence. This needs a Core group
information/member path rather than a contact-profile success claim. The
onboarding profile was restored and its previous request-action states,
original drafts/history still match their recorded preservation baseline.

The initial dedicated group-info helper snapshot compiles as its own QtCore
target with `-j1` and passes **31 checks**, with no failures or skips. These
cover matching Group metadata, member identity boundaries, read-only member
inspection and account invalidation. CMake registers the separate target;
the complete group UI and later profile/encryption integration are still under
implementation. This helper result does not establish native group rendering
or member-profile behavior. The private log is
`/tmp/round30-group-info-tests.log` in the test container.

The first search-name metadata helper snapshot compiles separately and passes
**34 checks**, without failures or skips. Search integration must also propagate
metadata failures through the primary `Delta::LoadSearchResults`/dialogs callback
and the legacy search adapter; silently shortening a result vector would falsely
look like a complete search. That integration is still in progress. Its private
log is `/tmp/round32-search-chat-metadata-tests.log`.

The initial Core profile-action helper snapshot passes **76 checks**, but a
separate adversarial probe demonstrates two uncovered deletion defects:
malformed acknowledgement plus failed contact read is reported as success, while
a valid acknowledgement with a hidden-but-readable contact is reported unknown.
An isolated offline real-Core check confirms normal deletion with an existing
chat hides the contact from `get_contacts` while `get_contact` remains readable;
the chat is preserved. No live mailbox or mail IO is involved. Corrections to
mutation acknowledgement/readback and actual UI wiring are required before
committing these profile changes. Private evidence is
`/tmp/core-profile-delete-contract.json`, `/tmp/core-profile-actions-probe` and
`/tmp/round31-core-profile-actions-tests.log`. Passing these initial injected
tests is not treated as proof of correct deletion or full native profile UI.

The corrected profile helper snapshot passes **95 checks**, with no failures
or skips. The original probe now rejects malformed acknowledgement plus unreadable
state and accepts a normal hidden-contact deletion; rename also supports clearing
the private alias and readback after lost acknowledgement. UI contact deletion
uses this shared helper. Further adversarial review exposes two remaining edges:
the completed deletion retains its request through a callback ownership cycle,
and checked “also delete chat” treats a null lookup as no chat. An isolated offline
Core fixture proves null also means a hidden blocked chat, which survives contact
deletion. Those corrections are in progress; this snapshot is not committed as
validated profile behavior. Logs are `/tmp/round35-core-profile-actions-tests.log`
and `/tmp/core-profile-hidden-chat-contract.json`.

### Integrated group, profile and search corrections — 2026-10-26

The source batch now supplies a dedicated Core Group Info surface with matching
Group metadata, member count, description, read-only contact inspection and Core
encryption information. Member profiles use full-width sender identities; Message
retains the explicit contact-chat opener. Header, third-column toggle, restore
and resize routes select the group surface; unknown real Core peers are classified
before presenting a contact profile. Third-column group widgets do not adapt
themselves into a layer or navigate the main history backward.

Core profile contact actions resolve exact contacts rather than treating chat
or synthetic sender IDs as contact IDs. Edit name changes the private alias,
including clearing it; it does not claim to add a contact by changing a name.
Encryption text and contact flags come from Core, without Telegram verification
badges. Contact deletion verifies key/address lists because a hidden contact may
remain readable. Optional chat deletion validates the reviewed Single chat and
peer before one write, retains separate chat/contact outcomes, and refuses an
ambiguous null lookup when no reviewed chat ID is available. Lost acknowledgements
are not retried. Completed/cancelled requests release their callback state.

The original adversarial probe now reports all expected outcomes: malformed
delete acknowledgement is not success, acknowledged hidden-contact deletion
succeeds, checkbox null lookup is not success, and completed request state is
not retained. A separate driver connects the actual helper and `DeltaRpc` to a
temporary offline Core account: ambiguous lookup and wrong-peer review issue no
mutation; the exact reviewed blocked chat and contact are deleted once each;
an unrelated chat and its draft remain intact. No live mailbox or mail IO is used.
Evidence is `/tmp/round38-core-profile-real-check.json` and
`/tmp/core-profile-actions-probe` in the test container.

Search hydrates matching BasicChat names before building results for unlisted
chats. Missing metadata or malformed hits fail the page instead of silently
shortening a complete result set. Visible Failed/Retry state distinguishes errors
from a successful empty search, retains earlier hits and retries the same page
without advancing its offset. The widget now uses the tested `CoreSearchPager`
directly; account/query generation guards reject stale outcomes. An isolated
QtCore harness extracts the exact `loadSearchHits` and `SearchHitMessage` source
and passes **21 runtime cases**, including name hydration, whole-page failure,
cached-name recovery, ordering and account invalidation. RPC, message construction
and session boundaries are stubbed; native failure injection remains pending.
Private results are `/tmp/search-bridge-current/results.json`.

Serial focused checks pass **700 checks**: 305 integrity, 82 request-action,
64 contact-open, 59 notification, 31 group-info, 35 search metadata, 111 profile
actions and 13 search-failure checks, with no failures or skips. Snapshot source
files match the tested helpers. The source audit checks 13,241 files with zero
blocking findings; whitespace checks pass. These checks do not compile every UI
translation unit or prove final rendered behavior. Native compilation, language
code generation, group/sidebar/profile/search fault replay and remaining
compatibility/package gates are still required. Native run `40315236704` at
`cacc5ce` completed successfully but predates this source batch. Its artifact
and notification GUI replay are verified separately below.

[Integrated native run `40328676156`](https://github.com/omidz4t/deltagram/actions/runs/40328676156)
at `5f86956` stops in `delta_boxes.cpp`: the Group Info member callbacks pass
`UserData` into `showPeerInfo`, but the translation unit only has its forward
declaration. The correction includes `data/data_user.h`, making the public
`PeerData` inheritance available to the existing `not_null` conversion. Member
IDs and navigation calls are unchanged. The run compiled the new search widgets,
history top bar and Core profile-action UI before this failure; it did not reach
the full smoke-test stage or prove the remaining native translation units.
The corrected complete native build remains required.

### Native incoming notifications — 2026-10-26

[Native run `40315236704`](https://github.com/omidz4t/deltagram/actions/runs/40315236704)
passes compilation and **510 checks** with no failures or skips: 305 integrity,
82 request-action, 64 contact-open and 59 notification checks. All five downloaded
payload checksums match. Provenance and merge parents identify PR head
`cacc5cef21a0435bdc5144d269b68954de0914bd`, checkout
`5a86af5885e3bc38205e90da57049b6be04b4b43` and base
`b713bff697ad3450bf842da5f24f83b64a9d68e6`.

After normal shutdown, verified process exit and a fresh private checkpoint, the
disposable GUI runs that artifact. A new group from the disposable request sender
produces a popup with its Core group name and actual user text. Core's preceding
`ChatE2ee` notice is state 13 and remains in the transcript; the captured arrival
sequence shows no extra encryption or empty-name popup. An incoming synthetic
photo renders with its caption. After navigating to another conversation, a new
message in the test group still produces a named popup. GUI Mute suppresses the
next popup while retaining the message in history; Core omits it from fresh
messages. GUI Unmute restores the prior state.

The GUI audit contains no send/create/accept/block/delete operation during these
receiving probes; the isolated sender submits each fixture once. Original drafts,
baseline message IDs, accepted/blocked/deleted request states, the remaining
alternate-profile request and the restored device-chat visibility are preserved.
Fault policies are cleared, and one project GUI/proxy/Core chain remains running.
Private captures and numeric evidence are under
`/tmp/deltagram-live/cacc-notification-fixture/` in the disposable test container.

A further one-shot probe temporarily limits automatic downloads on the disposable
account. A larger image arrives as a Text placeholder with Core download state
Available. The GUI's Download action upgrades that existing item in the open
history to Image/Done, retaining the caption and showing no extra popup in the
download capture. The delivered 985,178 bytes match the sender's Core image by
SHA-256. The viewer opens the downloaded photo, navigates to the previous blue
photo and back, and closes to the conversation. The prior download limit is
restored, and the original drafts/history/request checks still pass.

The preparation observer initially times out because Core appends size metadata
to the placeholder caption. Read-only reconciliation identifies the already-sent
message; no send is repeated. Private partial-download evidence records this
observer limitation separately from the verified GUI/Core result.

This verifies the captured notification/name/mute flow, photo rendering and one
real Existing-message placeholder upgrade. It does not prove every document type,
donor scheduling state or the newer group/profile/search source batch.

The same generated photo is then used for a viewer-deletion replay. Go To Message
returns to its conversation, and View all photos lists both test photos. Under
Xvfb the windowed viewer stays above the opened Photos layer until the viewer is
closed. A rejected viewer Delete submits once, displays the rejection and retains
the photo in Core history and the grid. After removing the fault, a new explicit
Delete confirmation submits once and removes only that photo; the grid retains
the other image. Normal quit, verified shutdown and restart preserve that deletion
without another delete request. The original drafts, unrelated message IDs,
request states and restored device visibility remain unchanged.
Private evidence is under `cacc-notification-fixture/viewer-delete/`; deletion is
intentional fixture cleanup, not loss of an original account message.

### Offline backup restore contracts — 2026-10-26

The checksum-verified pinned Core binary exports and imports both unencrypted
and passphrase-protected backups in temporary account trees. The source is an
offline copy of the stopped disposable sender, with relative account paths
checked before opening it. No mail IO or provisioning is started. All original
source files are fingerprinted before and after and remain unchanged; temporary
backup files and copied profiles are removed at completion. Passphrases are
generated in process and are not included in evidence or chat.

Eight non-device chats and 23 message IDs, plus a known saved draft, survive each
successful restore. Core deliberately deletes/resets device-local messages on
import, so those notices are not treated as portable history. A missing
passphrase rejects the encrypted archive and leaves the destination unconfigured;
the correct passphrase then restores into that same destination. An invalid
archive is rejected, and a later explicit valid import into the unconfigured
destination succeeds. Import into an already configured destination is rejected
and preserves its history/draft.

The first test incorrectly expected device-local message IDs to survive import;
the verifier was corrected against Core's explicit device-message reset, then
rerun with new temporary trees. This is an offline Core contract check, not GUI
restore proof. The current onboarding restore call supplies no passphrase, and
its protected-backup and repeated-submit UI behavior still require correction or
validation. Private evidence is
`/tmp/deltagram-live/backup-restore-offline-evidence.json` in the test container.

An isolated GUI replay on `cacc5ce` confirms an onboarding recovery defect. A
plain backup exported from an offline disposable copy imports once into a new
GUI data directory. The proxy rejects `start_io`, so no mailbox IO starts. Core
confirms the restored account is configured and its saved draft is intact, but
the welcome screen has no retry-open action. Choosing Restore again correctly
refuses the configured destination without a second import. The Create control
also remains enabled during import, and a queued progress event replaces the
opening error with `100%`. No provisioning is attempted. This test does not
prove concurrent overwrite; the missing setup lock and recovery/status behavior
are under source correction. Evidence and the stopped isolated GUI profile are
under `/tmp/deltagram-live/backup-gui-fixture/`, separate from the four main
disposable profiles.

The source correction shares an exclusive setup lease per RPC/account across
onboarding windows and uses it for create, import, invitation, classic-relay and
second-device setup. Submitted writes retain ownership through their terminal
callback, including live-widget teardown. Results capture the submitted account
ID. A typed import/copy acknowledgement completes setup; an error reads
`is_configured` once: configured permits opening only, fresh permits a later
explicit retry on that same ID, and unreadable state blocks new mutation.
Completed setup exposes Open Profile after an opening failure, rather than
importing or provisioning again. Progress rendering uses the same held-lease
state and exact event context ID, so late/completed or foreign-account progress
cannot overwrite terminal status. Plain import still passes a null passphrase;
passphrase-protected GUI restore remains unsupported, as in the pinned reference
onboarding.

The actual new QtCore target passes **27 checks**, and all nine focused suites
pass **727 checks**, zero failures/skips, serially. All 41 tested helper/source
files match the checkout. The source audit checks 13,243 files with zero blocking
findings; whitespace checks pass. The first header/test compile caught a Qt
`slots` macro collision; the production member is now named `entries`, without
changing test-target keyword settings.

An additional driver connects the production setup helper and `DeltaRpc` to the
actual pinned Core in temporary accounts. Normal import preserves the saved
draft. Replacing a committed import acknowledgement with an error yields Open
Only after exactly one import and one configured-state read, without claiming
verified restoration or retrying the import. Invalid import leaves the account
fresh, and a later explicit valid import into that same ID succeeds. Completed
setup refuses another import/create lease. No mail IO starts. Private results are
`/tmp/profile-setup-final-core-result.json` and
`/tmp/profile-setup-final-tests.log`. These helper/Core checks do not prove the
QtWidgets lock, rendered Open Profile action, or native failed-open recovery;
that replay and compilation of the new onboarding UI remain required.

### Archived device history from search — 2026-10-26

On the verified `4b5e093` binary, the disposable device chat `3:11` was archived
once through Core and confirmed absent from the ordinary chat list. After normal
quit, verified process shutdown and restart, global search for its existing
account-transfer notice finds two messages. Opening a result loads the device
history with a Mute-only bottom bar, no composer and no Accept control. Core
still reports `isDeviceChat: true` and `canSend: false`; the audit observes
matching BasicChat and FullChat reads with no send/create/accept/block/delete
operation. This verifies the archived-before-startup device read-only path,
not every transient UI frame or outgoing attachment surface.

The same check reveals an archived-search naming defect: both result rows and
the opened header display `Unknown` while matching Core metadata names the
chat `Device messages`. `loadSearchHits` builds peers from `_names` without
hydrating metadata for unlisted chats. The notification-only name fix does not
cover this search path. A separate metadata-hydration correction is pending.

The original Normal visibility was restored with one explicit Core write.
Original drafts, message IDs and the remaining pending alternate request match
their baseline; original primary request-action states remain intact. Fault
policy is cleared. Private evidence is
`/tmp/deltagram-live/archived-device-fixture/{before,native-open-evidence,restore-attempt}.json`;
captures `/tmp/4b5-archived-{start,search-settled,device-loaded}.png` show the
normal-list absence, results and read-only history. The valid pre-test checkpoint
was copied only after verified process shutdown. An earlier copy started before
shutdown settled and is retained with an `-unverified` suffix; it is not used as
recovery or validation evidence.

### Local-storage cache wording — 2026-10-26

Native settings replay exposed a false claim that all media remains in the
Telegram cloud and can be downloaded again. Source tracing confirms this
screen clears `session.data().cache()`, `cacheBigFile()` and unused emoji cache
files. The first two are donor `tdata/user_*/cache` and `media_cache`; their
cleaner removes old versions under those cache bases. It does not call Core
JSON-RPC or traverse Core's account blob directory. Size/age limits likewise
apply to these donor caches, not total Core account storage.

The Core-specific copy now says this cache uses the displayed space and that
clearing it does not delete original message files. Tooltip and limit text also
refer to cache files; donor copy is retained outside Core mode. No cloud
retention or re-download guarantee is made. The source audit checks 13,230 files
with no blocking findings and the diff is whitespace-clean. New language keys
and rendered copy await native compilation/replay. Limit sliders remain
untested; the later Clear all replay is recorded below.

A stopped-state checkpoint precedes the disposable `14878ed` Clear all replay.
The GUI completes clearing and donor cache files shrink from 18,788,724 bytes
to 272 bytes. All 56 fingerprinted Core blob files, totaling 10,965,850 bytes,
remain byte-identical immediately afterward and after normal quit/restart.
Core readback also preserves both drafts, original direct/group message IDs
and request states, including the fresh alternate-profile requests.

After restart, in-chat search locates existing photo `103`; its blue image
renders in the transcript and opens in the native media viewer. Normal loading
rebuilds donor caches to 20,849,640 bytes while all 56 original Core files still
match their hashes. Private manifests are
`/tmp/deltagram-live/cache-retention-fixture/{before,after,restart,media}.json`;
captures `/tmp/14878-cache-*.png` show clearing and subsequent image rendering.
This validates the populated Images/Clear all path on that revision. Selective
clearing, other populated cache tags, slider enforcement and the new rendered
wording remain separate gates.

[Native validation at `a64895c`](https://github.com/omidz4t/deltagram/actions/runs/40306519833)
passes compilation and all **451 checks**, without failures or skips. Its five
payload hashes and provenance match head `a64895c`, merge checkout `cb8cb904`,
base `b713bff` and GitHub merge parents. After a stopped-state checkpoint,
the verified GUI renders all four new Core strings: “This cache uses <0.1% of
your device storage,” original-message-file retention text, “App cache” in the
usage tooltip, and the limits explanation referring to unused cache files.
The URI opens in the existing GUI process, with one GUI/proxy/Core owner.
No clearing or slider change is performed during this copy replay. Original
drafts/history, recorded request-action states, the pending alternate Single
request and restored device visibility remain intact. Captures are
`/tmp/a648-storage-{copy,tooltip,limits}.png`; the settings URI/PID record is
`settings-fixture/native-a648-storage-copy.json`. Selective clearing and actual
limit enforcement remain unverified. This binary predates the notification fix.

### Core send-permission boundary — 2026-10-26

`22bfd8f` adds an account/chat-scoped FullChat permission observer and gates the
bridge's outgoing text, media, prepared/queued sends, forwarding, donor send and
notification reply paths on Core's `canSend`. Missing/error/malformed metadata
does not authorize a send. Draft persistence, history loading and cleanup remain
separate. A notification reply is persisted first, then its original intent
waits for permission; a denial or failed read leaves a saved Pending reply.
Attempted/ambiguous sends are not retried automatically.

The first isolated compile caught Qt's `slots` macro colliding with a variable.
After a snapshot-only rename, a tiny C++ reproducer confirmed stale allow after
newer denial, old read completing a replacement waiter, and malformed envelope
authorization. The corrected helper compiles without that rename and all three
reproducer flags are zero. The expanded suite then exposed two regressions in
existing lost-storage-ack and unreadable-storage explicit retry cases. Computing
the save result before moving its error string fixes the argument-evaluation
ordering bug. The final serial focused Qt target passed **295 checks**, with no
failures or skips, including those unchanged recovery cases.

This target does not compile the complete native UI. Native compilation and
rendered permission replay remain pending.

A follow-up isolated snapshot removes raw permission application from other
FullChat callers: those callers can request a tracked observer refresh, but
cannot publish `canSend` from their own response. Returning to a cached chat
refreshes its permission, closing history clears the focus, and contact changes
refresh only the focused chat and active waiters. Callback publication no longer
retains a slot reference across reentrant callbacks. Unknown permission and an
explicit denial have separate user messages.

That snapshot passed **300 integrity checks** and the separately wired
**70 contact-request action checks**, with no failures or skips, using serial
`-j1` builds. One new reentrancy test initially destroyed its replacement account
scope before delivering the reply; retaining that scope fixes the test while
preserving stale-account rejection. Logs are in the disposable test container at
`/tmp/round13-recovery-tests.log` and the snapshot build's `LastTest.log`.
These results precede the ongoing request-UI/helper revisions and do not validate
those later edits. The source audit checked 13,228 files with no blocking findings.

### Supported settings surface follow-up — 2026-10-26

The follow-up removes the remaining Telegram-only proxy/DC controls, identity
and paid-message links, sticker/emoji and cloud-theme catalogs, sensitive-content
and archive-account policy controls, FAQ suggestions and unused settings reloads.
Unknown settings factories now default to unavailable on Core; local passcode
Create/Check/Change, Search, LocalStorage and Experimental remain explicitly
available. Experimental omits the MTProto IPv6 option. Main QR opens Core's invite.

Local wallpaper gallery/file selection, font and scale, passcode, media devices,
relays, connectivity, second-device setup, name/bio/avatar, theme editing and
export, current language, English and custom `.strings` import remain. The theme
editor's primary Core button now exports locally instead of offering cloud
publishing. Translation uses a configured custom URL or an available platform
engine; Core has no MTProto fallback. Without either backend, translation controls
are omitted and direct requests return an unavailable result. Platform-engine
availability is cached for Core; a newly installed engine needs an app restart.

On verified native `a64895c`, a local three-key `.strings` fixture imports through
the language picker. After the confirmed restart, its language name, picker
title and OK label render from the fixture. A controlled repeat without capture
during restart passes both custom import/restart and the switch back to English,
each with one new GUI/proxy/Core chain. English is selected afterward; original
drafts/history, recorded request states and restored device visibility match
their baseline. Private evidence is
`settings-fixture/native-custom-language-evidence.json`; captures are
`/tmp/a648-language-{custom-persisted,controlled-custom,controlled-english}.png`.

The first English restart terminated with SIGABRT while capture/xdotool utilities
were blocked across window destruction. After verifying process termination,
stopping the blocked test utilities and relaunching once, English and data were
preserved. The controlled repeat above did not reproduce the abort. Its cause
remains unattributed; this is not a source crash fix or refutation, and final
native replay must repeat the controlled switch. Captures are now time-bounded
and run only after the ownership chain is ready.

The custom-import confirmation also repeats donor “Telegram will restart” text.
The Core-only source correction uses a new app-neutral language key, preserves
current/target custom translations when different and avoids duplicate identical
fallback text. Donor behavior and the local English-switch implementation are
unchanged. New language code generation/native rendering remain pending.

These settings changes are committed at `eafbca5`. Source audit and diff checks
pass, but the focused Qt target does not compile these UI translation units.
Native compilation and rendered replay remain required. Source review of the
reported rich-message translation path found that `switchTranslation` sets its
rich-request flag only for a provider supporting message IDs. Core's URL,
platform and unavailable providers all return false for that capability; the
MTProto provider is donor-only. The reported path therefore has no demonstrated
Core reachability through this tracker.

A small Qt harness compiled the current Linux translation provider source with
local platform stubs and a disposable Crow executable. Normal output completed
once with translated text; forcing process-start failure completed zero times.
This reproduces the missing `FailedToStart` completion without a mailbox or
full native build. The final source correction then passed six isolated harness
cases: normal output, forced start failure, nonzero exit with output, crash,
missing executable, and provider destruction from its completion callback.
Each completed once; only the two successful-output cases returned text. A
seventh case destroys the provider before process completion and confirms no
callback is delivered after teardown. The
harness uses the actual provider source with local platform stubs, so full native
compilation and real Crow integration remain separate gates.

A separate startup-lifetime source audit found no reachable retained MTProto
translation provider on the checked paths. Primary-window setup initially opens
empty history, so it does not construct `HistoryInner` or a tracker before the
Bridge is attached. Profile switching retains the Bridge; secondary-window
restore is queued after attachment. Session teardown removes the UI before the
Bridge lifetime ends. The synchronous session-publication interval exists, but
these call paths do not construct a provider in it. No additional startup mode
flag or speculative guard was added. This is source evidence, separate from
native startup/restore replay.

### Contact-request action gap — 2026-10-26

Source review at `22bfd8f` confirms there is no explicit `accept_chat` action in
the Qt bridge. Core includes incoming requests in the chat list but reports
`isContactRequest: true` and `canSend: false`. Official Desktop presents explicit
Accept and Block/Delete actions; a send-permission lock alone does not provide
that request flow. Group decline uses Core `block_chat`, which deletes the group
chat and therefore requires an explicit deletion confirmation. Native request
action replay remains open.

A profile's explicit Message action is separate: official Desktop 2.62.0
[`ViewProfile`](https://github.com/deltachat/deltachat-desktop/blob/v2.62.0/packages/frontend/src/components/dialogs/ViewProfile/index.tsx#L118)
uses `createChatByContactId` to start the conversation, matching the canonical
sender-routing correction. A plain incoming sender-name click uses
`Element::fromLink` → `showPeerInfo` in this client. It must not be described as
the `PeerData::createOpenLink` path without checking the actual caller.

A fresh disposable profile, **Deltagram integrity request sender**, on the pinned
Core 2.63.0-dev sent one direct probe and one group probe to the existing
disposable onboarding profile. Its isolated Core process stopped after delivery.
The recipient reports a Single request (chat `14`, message `294`) and a Group
request (chat `15`, message `296`), both with `isContactRequest: true` and
`canSend: false`. Neither request was accepted. Each creation/send intent was
journaled before its single RPC, without mutation retries. Existing direct-chat
message IDs, both composer drafts and the selected profile are unchanged.
Private fixture evidence is `/tmp/deltagram-live/request-fixture/ready.json` in
the test container. These fixtures prepare native action replay; Core delivery
alone does not validate the missing UI.

Opening each fixture on the checksum-verified `44a91b5` GUI renders its history
and a composer, with no Accept or Block/Delete controls. No text was entered or
sent. Returning to the original direct conversation and reading Core again
confirms both requests remain requests with `canSend: false`, no request draft
was created, and the original drafts/history are unchanged. Private rendered
captures are `/tmp/request-single-baseline-44a.png` and
`/tmp/request-group-baseline-44a.png`; readback is
`/tmp/deltagram-live/request-fixture/baseline-gui-inspection.json` in the container.

Separate decline fixtures are also ready: another Group request (chat `16`)
from the request sender, and a Single request (chat `17`) from a second fresh
profile, **Deltagram integrity decline sender**. Core confirms all four fixtures
remain requests with sending disabled. Existing drafts, message IDs and the
selected profile are unchanged. Both isolated sender processes exited after
delivery. Single creation and send attempts were journaled without mutation
retries; evidence is `/tmp/deltagram-live/request-decline-fixture/ready.json`.
This leaves separate fixtures for Accept and destructive decline replay.

A later isolated request/permission snapshot passed **305 integrity checks**.
Its separate action suite passed **82 checks** after correcting a new fixture's
scope-generation assumption: `invalidate()` expires old guards and starts a new
generation, rather than permanently disabling new reads. The narrow shared-tree
test correction is now applied; rebuilding both current focused test files
serially passes the same 305 and 82 checks. Compiling the snapshot
parser against reduced FullChat payloads from all four live requests also passes;
this reads metadata only and does not accept, block or delete a request. These
checks do not compile the native widget changes. Logs are
`/tmp/request-review-tests.log` and `/tmp/request-review-recovery-tests.log` in
the disposable test container.

The native source now adds an Accept and Block/Delete overlay driven by the
tracked FullChat request flag, separately from send permission. Actions use
`accept_chat` or `block_chat`, with per-account/chat busy guards and account/UI
lifetime checks. Group decline confirms deletion without blocking members;
acknowledged deletion is distinguished from uncertain readback. The helper no
longer retains a self-referencing peer-verification callback. OpenContact and
the explicit profile Message behavior are preserved. CMake registers the separate
request-action QtTest executable alongside the integrity suite. Full native
compilation and action replay are still pending.

The follow-up confirms every visible non-Single decline with the conversation
name. Group copy explains deletion without blocking members; mailing-list and
incoming-broadcast copy explains blocking and hiding. Single Block remains a
single action, and outgoing broadcasts have no decline control. Confirmation
callbacks recheck the account, history, peer and current type before dispatch.
Verified Accept retains its busy state through a tracked refresh and waiter,
without publishing helper JSON into permissions. Stale-account checks precede
busy-map mutation.

An isolated harness compiled the actual `finishContactRequest` method with the
real permission observer and stubbed UI/timer dependencies. The preceding method
fails the busy-state check; the new method passes delayed pre-mutation read,
post-accept metadata error and accepted-but-read-only cases. It emits reads only
and completes once after fresh metadata, reporting availability separately from
acceptance. Logs are `/tmp/deltagram-accept-handoff-review/*-results.log` in the
test container. The unchanged helpers retain their 305/82 focused results; this
harness does not compile the full Bridge or native widgets. The source audit
again checked 13,228 files with zero blocking findings. Native CMake already
regenerates language code when `lang.strings` changes; new named strings await
that native build and rendered replay.

The verified `14878ed` GUI now renders the Single Accept/Block and Group
Accept/Delete overlays on real disposable incoming requests, without a composer.
Opening a request performs no accept, block, create-chat or send mutation.
Rejected Accept leaves the request intact. Two clicks with a delayed Single
Accept acknowledgement produce one `accept_chat` call; the accepted chat shows
its composer. A Group Accept with a lost acknowledgement also makes one write,
then reconciles by readback without retry. Core events can expose the accepted
composer before the delayed mutation acknowledgement returns.

Single decline rejection preserves the request and displays an error. A later
lost-ack decline issues one `block_chat`, confirms contact blocking by readback,
hides the conversation and unselects it. Group Delete shows the conversation
name and explains that members are not blocked. Cancel makes no mutation;
rejection preserves the request. Confirmed deletion without injected failure
issues one `block_chat`, removes the conversation from Core's list and the
settled GUI, and unselects it. The group sender remains unblocked. The read-only
checker records unreadability/list absence separately from acknowledgement;
a generic FullChat error is not treated as proof of a successful lost-ack
group deletion.

Every action preserves prior direct-chat history, both profile drafts and the
other fixtures' then-current states. Evidence is in
`/tmp/deltagram-live/request-fixture/native-*-action-evidence.json`, with
`native-request-inspection-evidence.json` and `native-group-cancel-evidence.json`.
Private captures `/tmp/14878-request-*.png` and `/tmp/14878-decline-*.png` in the
test container show the rendered controls, confirmation and rejection. These
cases do not cover mailing lists, broadcasts or the newer contact-open helper.

After a normal quit/restart, one GUI/proxy/Core chain is running and the recorded
state matches: chats `14`/`15` stay accepted and sendable, Single `17` stays
blocked/hidden, and Group `16` stays unreadable/unlisted. The original group's
members/history and both profile drafts remain intact. Fault policy is cleared.
The comparison is recorded in `native-request-actions-before-restart.json` and
`native-request-actions-after-restart.json` in the same private fixture directory.

### Opening an existing contact request — 2026-10-26

Source review found that explicit profile Message and contact-picker Chat
called `create_chat_by_contact_id` even when a Single request already existed.
Core clears that request through creation, bypassing the explicit Accept action.
Chat-list inspection and bubble sender-name inspection do not use this path.

The contact-open helper now looks up the conversation first. A matching Single
chat opens without a write, retaining its request flag. Lookup/metadata failures
do not fall back to creation. A null lookup checks the contact's blocked state
before one creation attempt and validates the returned FullChat. Saved Messages
has a separate self-talk validation path; other reserved contacts are refused.
Account switches drop callbacks, and displayed errors contain local text only.

The standalone Qt target compiles serially and passes 64 checks, covering
request/accepted opens without mutation, blocked and malformed results, one-shot
creation, self-talk and account-scope cancellation. The actual parser also
accepts four reduced live FullChat payloads: an accepted conversation, two
pending requests and Saved Messages. These checks read the disposable Core only;
the requests remain pending. Logs are `/tmp/open-contact-tests.log` and
`/tmp/open-contact-live-payloads.json` in the test container. Full Bridge/native
GUI validation remains pending.

Core lookup returns null for both absent and hidden blocked chats. A hidden
blocked chat whose contact is unblocked cannot be distinguished by this API.
Creation also is not atomic with lookup: an incoming request can arrive between
the reads and the write. The helper never retries a creation, but these Core API
limits remain unresolved; this change does not establish atomic request safety.

Fresh follow-up requests are prepared in **Deltagram integrity alternate**:
Single chat `13` and Group chat `14`, both `isContactRequest:true` and
`canSend:false`, from **Deltagram integrity request sender**. They are reserved
for contact-open and lost-ack Group Delete replay. A separate follow-up Group
arrived in the primary profile as chat `18`; Core reports it already non-request
and sendable, so it is not used as a request fixture. No client Accept was
issued. Original drafts/message IDs in both profiles remain unchanged, and the
separate sender Core exited. Private `request-followup-fixture/reconciled.json`
and `preservation.json` record the observed states. The creation script stopped
at its request-only assertion for that primary Group; its intent/ack journals
must not be replayed or retried.

### Sender and chat identity lookups — 2026-10-26

Review of the blocking profile action found that high sender-peer IDs could be
narrowed to an unrelated chat ID during type/status lookup. The blocking action
now resolves known sender contacts before checking chat-only system IDs, and
the group/direct/channel/subtitle caches retain full-width peer IDs. Source
inspection confirms these lookups no longer truncate sender identities; native
compilation and group-member profile replay were initially pending.

The checksum- and provenance-verified `e669e85` GUI now passes the group-member
profile **Contact blocking** action against the disposable reference contact.
A rejected block leaves the contact unblocked and shows a rejection toast.
Lost-ack block and unblock each issue one contact mutation, then confirm the
result by Core readback without retrying. The direct conversation disappears
while blocked and returns after unblock; reopening the action shows the fresh
Unblock confirmation. The audit records no create-chat, send, accept or
chat-block mutation during these actions.

After a normal quit and restart, exactly one GUI/proxy/Core chain is running.
Core confirms the contact remains unblocked and the direct chat is listed.
Both profile drafts, prior message IDs, group membership/history and all four
incoming-request fixtures remain unchanged. Fault policy is cleared. Private
evidence is `/tmp/deltagram-live/blocking-fixture/` (the
`e669-profile-lostblock-profile-evidence.json`,
`e669-profile-lostunblock-profile-evidence.json` and
`e669-profile-restarted.json` files), with request preservation recorded in
`/tmp/deltagram-live/request-fixture/e669-profile-restarted-preservation.json`.
This older artifact does not validate the later permission/request UI or the
contact-status entry point.

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

### Forgotten local passcode with Chatmail data (2026-10-27)

User-selected policy: preserve profiles. Forgotten-passcode recovery must not
log out/reset local storage when Chatmail profiles are present or the locked
account types cannot be confirmed. The UI explains use of the passcode,
previously enabled system unlock, or an existing Chatmail backup restored in a
fresh installation. This does not add a passcode bypass, system-unlock setup,
backup export from a locked profile, or database encryption.

The actual `Domain::resetWithForgottenPasscode` entry rechecks the policy before
any account logout or `startFromScratch`; the confirmation UI also checks it.
The cold-start account list can still be encrypted. Read-only evidence therefore
uses app-owned backend metadata, occupied account namespaces, and the same Core
accounts-path resolver as onboarding. Core data, saved folder selection,
corrupt/unreadable metadata, and untagged legacy accounts preserve the lock.
Positively identified Telegram-only storage retains the donor reset path.
Legacy Telegram storage lacking backend metadata is conservatively unknown
until it can be unlocked/classified. Ordinary logout without a local lock or
passcode retains its previous behavior.

Focused Qt 6.4.2 checks run as UID 65534: seven QtTest cases including lifecycle
hooks, zero failures/skips. They cover loaded/mixed/unknown accounts, cold
Telegram metadata, saved Core folder selection, untagged legacy namespaces,
corrupt metadata, hidden/unreadable Core storage, and unchanged authorization
and profile sentinels. An exact-source reset-method recorder passes nine
scenarios under ASan/UBSan: protected outcomes perform zero logout/reset calls;
Telegram outcomes retain cold initialization or loaded account logout. The
recorder uses account/storage doubles and does not prove native persistence.
Language and subset generation succeeded. All four changed production C++
files pass syntax/type checks with the configured native Qt/Nix compiler; these
checks produce no objects and do not prove linking or GUI behavior.
Full linked-binary locked-start/system-unlock/backup GUI replay remains pending.
