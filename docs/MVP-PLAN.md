# MVP plan

Prove: launch the Qt client, open a Delta account, show the chat list, open a
chat, show plain text, send plain text, apply an incoming event, update
unread. No `MTP*` objects on that path.

Builds use `./nix/develop.sh` and `nix/build-tdesktop.sh` after submodules
are present. Until the full `Telegram` target links, early slices may be a
small CMake target that only compiles `delta/` plus `lib_ui`.

## Slice 0 — submodules and a linking target

- Purpose: the tree configures and links a binary that does not need an
  `api_id`.
- Files: `tdesktop` submodules, `nix/build-tdesktop.sh`, a CMake option that
  can omit MTProto sources later.
- Boundary: none yet.
- Result: Debug binary or `delta_shell` target starts a window.
- Check: configure once, change one comment, second Ninja build does not
  rebuild Qt.
- Risk: high, because the cmake submodule is empty and the full target still
  requires Telegram API credentials (`Telegram/cmake/telegram_options.cmake`).

## Slice 1 — `DeltaRpc` process

- Purpose: start and stop `deltachat-rpc-server`, send one JSON-RPC request,
  read one JSON line.
- Files: `Telegram/SourceFiles/delta/delta_rpc.*`.
- Boundary: the only `QProcess` in the client.
- Result: `get_system_info` returns; killing the child surfaces an error;
  stderr is not parsed as RPC.
- Check: unit or manual driver. Timeout, malformed line, and exit code are
  visible.
- Risk: medium. Framing is JSON Lines (`deltachat-rpc-server` `main.rs`).
  Accounts path is `DC_ACCOUNTS_PATH`.

## Slice 2 — account open

- Purpose: `add_account` or `get_selected_account_id`, `set_config` for the
  address and mail password the user types, `configure`, `start_io`.
- Files: `delta_backend.*`, a single setup page on `shell_widget`.
- Boundary: no `Main::Account`.
- Result: `is_configured` becomes true or `ConfigureProgress` / `Error` shows
  why it did not.
- Check: against a chatmail test domain the user supplies. No network in the
  widget.
- Risk: medium. Transport QR (`add_transport_from_qr`) can wait.

## Slice 3 — chat list

- Purpose: `get_chatlist_entries` then `get_chatlist_items_by_entries`.
  Render `name`, `summary_text2`, `last_updated`, `fresh_message_counter`.
- Files: `shell_widget`, row model. Do not construct `Dialogs::Row`.
- Boundary: list does not include `History*`.
- Result: real chats from the configured account.
- Check: compare titles with `deltachat-repl` or the reference desktop client.
- Risk: low once slice 2 works.

## Slice 4 — history and send

- Purpose: `get_message_ids` / `get_message_list_items` for the selected id.
  Composer calls `send_msg` with text `MessageData`.
- Files: `shell_widget`, `delta_backend`.
- Boundary: `ApiWrap::sendMessage` is not on the stack.
- Result: the new text appears in the open chat.
- Check: send to a second account or Saved Messages equivalent
  (`is_self_talk` on the list item) and see the row.
- Risk: medium. `MessageData` has many optional fields; set only text.

## Slice 5 — live events and read state

- Purpose: loop `get_next_event`. On `IncomingMsg` or `MsgsChanged`, refresh
  the open chat and the list row. On visible chat, `markseen_msgs` and
  `marknoticed_chat`.
- Files: `delta_events.*`.
- Boundary: producers are `rpl::producer` of generic ids, not `PeerData`.
- Result: a message sent from another client shows up without a timer. Unread
  count drops after the chat is open.
- Check: subprocess kill mid-loop restarts or shows a disconnected state via
  `ConnectivityChanged` / process exit.
- Risk: medium. `ChatlistItemChanged` should patch one row; a full reload is
  acceptable if it stays correct.

## Explicitly out of this plan

Groups, reactions, files, voice, webxdc, SecureJoin, multi-account order,
and deleting Telegram translation units. Those start only after slice 5.

Each later slice still records the Telegram dependency removed. The expected
removal for slices 1–5 is "none linked", not "HistoryWidget edited".
