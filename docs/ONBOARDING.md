# Native Qt Delta Chat client, from Telegram Desktop

## Current account requirements (2026-10-26)

The product must support both Core profiles and real Telegram accounts in one
application. The Core-only design below records the original migration plan;
it does not authorize disabling the Telegram backend. Core accounts use
Chatmail Core exclusively. Telegram accounts use the donor's authentication,
MTProto transport, and account persistence.

Backend identity must be persisted per `Main::Account` and available before
transport or session UI starts. An unresolved account must not open a Telegram
connection. Core bridge presence and the focused window cannot classify another
account's requests, cached messages, settings, or UI. Existing Telegram
authorization must survive migration. See `AGENTS.md` for the current boundary.

The backend foundation is still under implementation. Telegram login and mixed
account behavior are not yet validated. Core test or GUI results alone do not
prove Telegram compatibility or production readiness.

## Local backups and forgotten passcodes

For an unlocked Chatmail profile, Advanced Settings → Export backup creates a
backup of that selected profile. Choose a destination folder and optionally
enter the same backup passphrase twice. Each attempt writes to a new
`chatmail-backup-...` subfolder; the success message shows its location. Repeat
for each Chatmail profile that needs a backup. Keep the app open until export
finishes. Closing the dialog does not cancel an export already submitted. Its
RPC stays alive through completion even if the originating session is released. Normal
quit/restart stays blocked until Core replies; try quitting or restarting again
after completion. Profile deletion and export cannot run together; retry after
the pending operation finishes. Force termination or power loss can still
interrupt export.

A backup passphrase protects Core's database, including account credentials.
Attachment files in the archive remain unencrypted. Store the whole backup
securely; this is not whole-archive encryption. The backup passphrase is
independent of the app's local passcode and must be kept for later recovery.

A forgotten local passcode must preserve Chatmail profiles and the lock. Use
the passcode or previously enabled system unlock. Otherwise, restore an
existing backup in a fresh installation with separate application/profile data,
keeping the original files untouched: choose Chatmail, I Already Have a Profile,
then Restore from Backup, and enter its backup passphrase when applicable.
The forgotten-passcode UI does not export locked profiles or automatically
wipe/reset their storage. Native replay of the newest export/recovery dialogs
remains pending; this section describes implemented source behavior.

Telegram Desktop (`tdesktop/`) is a donor of native Qt UX. Chatmail Core
(`context/core/`, https://github.com/chatmail/core) is the backend.
`context/deltachat-desktop/` is a behavior reference only.

The end state is:

```text
Native Qt client
  UI, widgets, platform
  generic messaging model
  Delta backend adapter
    JSON-RPC (stdio JSON Lines)
    deltachat-rpc-server
    Chatmail Core
    IMAP / SMTP
```

This is not an MTProto compatibility layer. Do not emulate Telegram's API on
top of Delta Chat. Do not invent fake `MTP*` objects so old call sites keep
compiling, except as a disposable spike that is deleted in the same slice.

Direction:

```text
Telegram-shaped UI
  extract generic messaging concepts
  frontend / backend boundary
  Delta Chat adapter
```

## Assumptions, checked against this tree

Telegram concepts run through the app. `MTP*`, `ApiWrap`, `Main::Session`,
`Data::Session`, `PeerData`, `History`, and `HistoryItem` are not behind an
adapter. UI folders are not automatically reusable: channels, forums, bots,
stories, premium, stars, payments, and calls are mixed into widgets.

Reusable infrastructure is the Qt shell, `lib_base`, `lib_ui`, `lib_rpl`,
platform code, text and emoji, themes, notifications, tray, and media
playback. See `docs/ARCHITECTURE-RECON.md` for the cut.

Do not reimplement SMTP, IMAP, MIME, Autocrypt, OpenPGP, SecureJoin, or
message persistence. Core owns those.

Prefer `deltachat-rpc-server` over linking the Rust library into the C++
process. `context/core/deltachat-jsonrpc-bindings/qt/` already has a C FFI client
(`CffiTransport` in `cffi_client.hpp`). That contradicts this boundary. The
MVP uses stdio. Leave the FFI header unused.

The server speaks JSON Lines on stdout and logs on stderr
(`context/core/deltachat-rpc-server/src/main.rs`). Accounts live in
`DC_ACCOUNTS_PATH` (default `accounts`). Events are pulled with
`get_next_event` / `get_next_event_batch`, not a side channel.

Delta types and `MTP*` types stay out of widgets. The UI talks to a
`MessagingBackend`. Map core events into `rpl` producers. Do not poll.

## First product slice

Account open, chat list, one chat, plain-text history, send text, incoming
event, unread/read. Defer stories, bots, channels, forums, premium, stars,
payments, calls, stickers, polls, proxies, and sponsored content. Delete or
isolate what blocks that path. A permanent `#ifdef DELTA` forest is not the
design.

Do not merge every future tdesktop commit. Import platform and library fixes
selectively.

## Rules

- Widgets do not start `QProcess` or parse JSON-RPC.
- Keep protocol types out of UI code.
- Reuse Qt, `rpl`, `base`, logging, and the existing build system.
- Each slice names the goal, files, Telegram dependency removed, new
  boundary, how it was verified, and what it does not do.
- Build the affected target, run the narrowest test, and check subprocess
  startup, shutdown, crash, malformed responses, and event delivery before
  calling a slice done.

Implementation order is `docs/MVP-PLAN.md`. What is actually built and how
to run the one file is `docs/PROGRESS.md`. Source wins when it contradicts
this note.
