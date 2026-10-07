# Native Qt Delta Chat client, from Telegram Desktop

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
