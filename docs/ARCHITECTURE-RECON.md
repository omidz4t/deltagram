# Architecture reconnaissance

Source of truth: this checkout, October 2026. `tdesktop/cmake` and the other
submodules in `tdesktop/.gitmodules` are empty, so the app does not configure
yet. The C++ sources that are present are enough to map the coupling.

## 1. Startup path

```text
Telegram/SourceFiles/main.cpp
  Core::Launcher::Create()->exec()
    Core::Application::run()          core/application.cpp
      local storage, fonts, emoji, audio
      Window::Controller (first window)
      Application::startDomain()
        Main::Domain::start()         main/main_domain.cpp
          Storage::Domain::start()    storage/storage_domain.cpp
            accounts from tdata, or startFromScratch()
      Window::Controller::firstShow()
        MainWidget                    mainwidget.cpp
          dialogs list + history column
```

`Main::Account` (`main/main_account.h`) owns one `Main::Session`. The session
owns `ApiWrap` (`apiwrap.h`) and `MTP::Instance`. There is no separate
"network service" the UI can swap. `Application::run` also calls
`MTP::details::pause()` while short animations play, so MTProto is in the
shell, not only in the session.

A passcode lock can stop startup in `Application::startDomain` before any
session exists (`Storage::StartResult`).

## 2. Protocol coupling

| Area | Types | Where |
| --- | --- | --- |
| Transport | `MTP::Instance`, `mtpRequestId` | `Telegram/SourceFiles/mtproto/`, `Main::Session::mtp()` |
| Schema | generated `MTP*` from `mtproto/scheme/api.tl` | `lib_tl`, `mtproto/scheme/` |
| Facade | `ApiWrap`, `Api::SendAction` | `apiwrap.h`, `api/` |
| Account | `Main::Account`, `Main::Session` | `main/main_account.*`, `main/main_session.*` |
| Domain | `Data::Session`, `PeerData`, `History`, `HistoryItem` | `data/` |
| Updates | `Api::Updates` | `api/api_updates.*` |
| Send | `ApiWrap::sendMessage(MessageToSend)` | `apiwrap.h` around the `sendMessage` declaration |
| Read | `ApiWrap` history read / `api_unread_things` | `api/api_unread_things.*` |
| Notify | session notifications fed by new `HistoryItem`s | `window/notifications_*`, `history/` |

`api/` is a directory of Telegram methods (premium, stars, polls, bots,
stories-adjacent statistics). It is not a generic service layer.

## 3. Chat list

```text
Data::Session histories
  Dialogs::IndexedList / Row          dialogs/dialogs_row.*
    Dialogs::InnerWidget              dialogs/dialogs_inner_widget.*
      Dialogs::Widget                 dialogs/dialogs_widget.*
        MainWidget left column
```

A row paints title, preview, time, and unread from `History` and `PeerData`
(name, userpic, last message, `unreadCount`). Filters, folders, forums, and
archive are extra list modes on the same widget. The minimum fields for the
MVP already exist on the core side as `ChatListItem` in
`context/core/deltachat-jsonrpc/src/api/types/chat_list.rs`: `name`, `avatar_path`,
`summary_text1`, `summary_text2`, `last_updated`, `fresh_message_counter`,
`is_muted`, `is_pinned`, `is_contact_request`, `is_archived`.

The current row cannot show that struct. It requires a `History*`. Wrapping a
fake history is the approach this project rejects. The list widget's paint and
click handling has to take a small row model, or a new list widget reuses
`Ui::RpWidget` and the existing row metrics in `dialogs.style`.

## 4. Message history

```text
MTP messages applied in Data::Session
  History (per peer) + HistoryItem
    HistoryView::Element              history/view/
      HistoryInner / HistoryWidget    history/history_inner_widget.*, history_widget.*
```

`HistoryView::Element` subclasses cover text, photo, sticker, poll, invoice,
game, and service messages, and they read Telegram fields (views, forwards,
replies as `FullMsgId`, reactions as Telegram reaction ids). Plain text is the
smallest piece (`history/view/history_view_text` and the text element). It
still takes a `HistoryItem` for out/in alignment, time, and selection.

Keep the text layout helpers (`Ui::Text`, emoji). Replace the element factory
for the MVP with one text bubble fed by the generic `Message` model. Leave
the other element subclasses unreferenced rather than stubbing each media
type.

## 5. Composer and send

```text
HistoryWidget compose controls     history/history_widget.cpp
  HistoryView::ComposeControls
    ApiWrap::sendMessage(MessageToSend)
      local HistoryItem insert
      messages.sendMessage MTP request
```

`MessageToSend` carries a peer, reply, webpage preview flags, and send-as.
The narrow redirect is not inside `ApiWrap::sendMessage`: that function's
contract is a Telegram peer and a local `HistoryItem`. The narrow cut is the
compose submit handler in `HistoryWidget`, which should call
`MessagingBackend::sendText(ChatId, QString)` and ignore `MessageToSend`.

## 6. Events

Today:

```text
MTP update
  Api::Updates
    Data::Session mutation
      rpl producers on History / PeerData / Dialogs::Entry
        widgets
```

Delta equivalent, from `context/core/src/events/payload.rs` and the RPC server:

```text
Core emits EventType
  deltachat-rpc-server writes a JSON-RPC response/notification line
  client pulls with get_next_event or get_next_event_batch
    DeltaEvents maps to rpl::producer
      generic chat/message updates
        list and history widgets
```

Events the MVP must handle: `IncomingMsg`, `MsgsChanged`, `MsgDelivered`,
`MsgFailed`, `MsgRead`, `MsgsNoticed`, `ChatModified`, `ChatDeleted`,
`ChatlistChanged`, `ChatlistItemChanged`, `IncomingMsgBunch`,
`ConnectivityChanged`, `ConfigureProgress`, `Error`. `Error` is a user-visible
bubble, not a dialog, per the enum docs in `payload.rs`. Log-only:
`Info`, `Warning`, `SmtpConnected`, `ImapConnected`.

## 7. RPC methods for the MVP and the next slices

Names are `async fn` methods on `CommandApi` in
`context/core/deltachat-jsonrpc/src/api.rs`. The wire format is JSON-RPC 2.0, one
object per line.

Account and process:

- `add_account`, `get_all_account_ids`, `select_account`, `get_selected_account_id`
- `remove_account`, `get_account_info`, `is_configured`
- `set_config`, `batch_set_config`, `get_config`, `batch_get_config`
- `configure`, `stop_ongoing_process`
- `add_transport`, `add_transport_from_qr`, `list_transports`, `delete_transport`
- `start_io`, `stop_io`, `start_io_for_all_accounts`, `stop_io_for_all_accounts`
- `get_connectivity`, `get_connectivity_html`, `maybe_network`
- `get_next_event`, `get_next_event_batch`
- `get_system_info`

Chat list and chats:

- `get_chatlist_entries`, `get_chatlist_items_by_entries`
- `get_basic_chat_info`, `get_full_chat_by_id`
- `accept_chat`, `block_chat`, `delete_chat`
- `set_chat_visibility`, `set_chat_mute_duration`, `is_chat_muted`
- `marknoticed_chat`, `markfresh_chat`, `get_fresh_msg_cnt`, `get_fresh_msgs`
- `create_chat_by_contact_id`, `create_group_chat`, `set_chat_name`

Messages:

- `get_message_ids`, `get_message_list_items`, `get_message`, `get_messages`
- `send_msg` with `MessageData` (`api/types/message.rs`)
- `misc_send_text_message` (helper; prefer `send_msg` so one send path exists)
- `markseen_msgs`
- `delete_messages`, `delete_messages_for_all`
- `send_edit_request`
- `get_draft`, `remove_draft`, `misc_set_draft`, `misc_send_draft`
- `can_send`

Contacts, files, reactions, QR (later slices):

- `get_contact`, `get_contacts`, `create_contact`, `get_chat_contacts`
- `send_msg` file fields on `MessageData` (no separate `send_file` method)
- `save_msg_file`, `get_chat_media`
- `send_reaction`, `get_message_reactions`
- `check_qr`, `secure_join`, `get_chat_securejoin_qr_code`, `get_chat_securejoin_qr_code_svg`

## 8. Minimum cut

Show a real chat list, open a chat, show text, send text, without fake
`MTP` objects.

Replace:

- `Main::Session` as the source of chats and messages, for this window
- `ApiWrap::sendMessage` on the compose submit path
- `Dialogs::Row`'s dependency on `History*`
- `HistoryView` element construction for the open chat

Retain, behind the new window, not inside the Telegram session:

- `Core::Launcher`, `Application` parts that are fonts, scale, tray, theme
- `lib_ui`, `lib_base`, `lib_rpl`, platform integration
- emoji and `Ui::Text`

Wrap:

- nothing in MTProto. The wrapper is `DeltaRpc` around `QProcess`.

Delete when it blocks the link, not before:

- calls, payments, premium, stories, passport, inline bots, as compile units
  once the new target stops linking them

Ignore until a later slice:

- the existing `MainWidget` history column
- `storage/` tdata account format (Delta accounts are `DC_ACCOUNTS_PATH`)
- the FFI client under `context/core/deltachat-jsonrpc-bindings/qt/`

The smallest cut is a **second top-level page** owned by `Application`, not a
fork of `HistoryWidget`. `Application::run` already builds the first
`Window::Controller`. A Delta shell widget in that window, fed only by
`MessagingBackend`, avoids touching `PeerData`. The Telegram main widget stays
unbuilt or unwired. That is more deletion than adaptation, and it is the cut
with the least debt.

## Classification

| Component | Class | Note |
| --- | --- | --- |
| Process startup (`main`, launcher) | KEEP | |
| `Application` fonts, scale, tray, theme | KEEP | Strip the `MTP::details::pause` hook when the session is gone |
| Account model (`Main::Account`) | REPLACE | Core `account_id` via `add_account` / `select_account` |
| `Main::Session` | REPLACE | Do not grow a parallel session inside it |
| `Data::Session` | REPLACE | |
| Dialogs list | REFACTOR | New row model, reuse paint metrics if cheap; else new widget |
| History / `HistoryItem` | REPLACE | |
| Message rendering (text layout) | KEEP WITH ADAPTER | `Ui::Text` only |
| Other history elements | DELETE | when unreferenced |
| Composer widget | KEEP WITH ADAPTER | submit calls the backend |
| `ApiWrap::sendMessage` | REPLACE | leave uncalled |
| Media loaders | DEFER | files are paths from core, not Telegram file references |
| Notifications | KEEP WITH ADAPTER | fire from `IncomingMsg`, not `HistoryItem` |
| Contacts | DEFER | `get_contact` when titles need it |
| Avatars | KEEP WITH ADAPTER | `avatar_path` is a filesystem path |
| tdata storage | DELETE | for account state; UI settings may stay |
| Settings screens | DEFER | most are Telegram account settings |
| `mtproto/`, `api.tl` | DELETE | once nothing links them |
| Platform integration | KEEP | |
| Theme system | KEEP | |
| Emoji | KEEP | |
| Audio / video playback | DEFER | players stay; wire-up waits |
| QR widgets | DEFER | core already returns SVG from `get_chat_securejoin_qr_code_svg` |
| `rpl` | KEEP | |

## Target layout

Fits the existing tree as new sources, not a rewrite of `history/`:

```text
tdesktop/Telegram/SourceFiles/delta/
  messaging_backend.h
  delta_rpc.h / .cpp          QProcess, JSON Lines, ids
  delta_backend.h / .cpp      MessagingBackend
  delta_events.h / .cpp       get_next_event loop to rpl
  shell_widget.h / .cpp       list + history + composer
```

Generic structs live next to the backend header. Do not add
`delta_account.cpp`, `delta_chat.cpp`, and `delta_message.cpp` until a type
needs behavior. The prompt's longer split is premature for the first slice.

## Contradiction with the prompt

The prompt's sample `DeltaRpc::call` returns an unspecified async type. This
codebase would use `rpl::producer` or a `crl::on_main` callback, not
`QFuture`, because that is what widgets already consume. The FFI Qt binding
in core is real and should not be the integration path.
