# Experimental compatibility tracker

Last reviewed: 2026-10-07. Deltagram is experimental and is not yet a complete
Delta Chat client. Core provides messaging protocols and encryption; retaining
a Telegram widget does not establish that its action works with Core.

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

## What works in local checks

| Capability | Status | Evidence and limits |
| --- | --- | --- |
| Core account creation and chat-list loading | ✅ Locally checked | Qt/Core RPC self-test creates a disposable account and loads its list without mail IO. |
| Contacts and contact deletion | ✅ Locally checked | Search, profile opening, chat creation and confirmed deletion have isolated checks. |
| Shared contact cards | ✅ Locally checked | Send/import, card photo and profile navigation checked. Core does not import vCard NOTE biographies; available Core contact status is displayed. |
| Group creation | ✅ Locally checked | Name/photo and member selection persist through Core; selection chips and stored members checked. |
| Invitation links | ✅ Locally checked | Contact/group/channel previews, confirmation and navigation checked. SecureJoin completion over the network remains unverified. |
| Channel profiles | ✅ Locally checked | Adaptive page/modal navigation, centered cover, large photo preview, contained action menu, description links, photo/name/description updates, mute control and Core media counts. Media rows open the selected message. |
| Broadcast channels | ✅ Locally checked | Creation and subscriber write restrictions checked. Owner counts are Core read receipts, not public view totals. |
| Message information | ✅ Locally checked | Context action and Core-backed details checked. |
| Reactions | ✅ Locally checked | Picker, stored reactions and rendering checked locally; multi-device delivery remains unverified. |
| Profile sidebar | ✅ Locally checked | Optional experimental toggle replaces the folder strip with profile photos/names. Switching, active colors, immediate toggling and restart persistence checked locally. |
| Profile ordering | ✅ Locally checked | Core RPC ordering and UI feedback recorded. |

## Partial implementation and known gaps

| Capability | Status | Remaining work or validation |
| --- | --- | --- |
| Text, images, files, videos and forwarding | 🟡 Partial | MP4 sending, GUI forwarding and local playback checked with Core, including silent videos and forwarded labels in Saved Messages. Fresh cross-client delivery and download checks are needed. |
| Delivery and read indicators | 🟡 Partial | Pending, server-delivered and read-receipt transitions checked in an offline fixture; Core state controls message and chat-list checks. Live transport and multi-recipient receipt behavior need validation. |
| Quotes and replies | 🟡 Partial | Core quote mapping corrected; complete create/display/jump behavior needs live conversation checks. |
| Message editing, deletion and chat archiving | 🟡 Partial | Core paths exist; received-deletion regressions passed, but two-device UI behavior needs checking. |
| Voice messages | 🟡 Partial | Sending is reported working; playback bubbles remain unfinished. |
| Multiple profiles | 🟡 Partial | Add/delete/order paths exist; replacing the complete chat list after switching remains unfinished. |
| Profile and group photos | 🟡 Partial | Settings image selection/crop/save and account avatar colors checked locally with Core; group checks recorded. Complete camera workflows and live photo distribution need checking. |
| Onboarding and relay settings | 🟡 Partial | Core transport configuration and backup restore paths exist; full live onboarding needs validation. |
| Invite QR scanning | 🟡 Partial | Camera capture, QR image files and clipboard screenshots feed Core invites for an explicit Join. Image decoding is checked locally; physical camera capture needs hardware validation. |
| Second-device setup | 🟡 Partial | Real Core backup QR generation exists; completed transfer to another device is unverified. |
| Contact status and biographies | 🟡 Partial | Available Core contact status displayed; imported vCard biographies are unsupported by the current Core snapshot. |
| Search | 🟡 Partial | Core message search is wired into the bridge; global/in-chat result navigation and help/HTML searches need separate checks. |
| App picker and webxdc | 🟡 Partial | Embedded app store exists; host rendering, draft preparation and synchronized webxdc updates are unverified. |
| Calls | ❌ Unavailable | Delta Chat calling is not implemented. Telegram conference creation/joining is disabled and tde2e is excluded. |
| Service-event presentation | ❌ Unavailable | Recorded as unfinished in the development log. |
| Settings privacy presentation | 🟡 Partial | Hiding the settings email remains unfinished. |

## Compatibility checks still needed

No complete Core-backed Deltagram checks are recorded for these categories.
They are **unverified**, rather than assumed to work because the donor UI has them:

| Feature area | Status | Checks to record |
| --- | --- | --- |
| Media browser | ❓ Unverified | Combined media list, previous/next, delete and return to message. |
| Chat management | ❓ Unverified | Clear history, bulk selection, bulk read state and last-read positioning. |
| Member profiles | ❓ Unverified | Opening a profile from a group member list. |
| Attachments | ❓ Unverified | Staging, multiple files/images/videos and image editing before send. |
| Notifications | ❓ Unverified | Background delivery and unread indicators for inactive profiles. |
| Accessibility and localization | ❓ Unverified | Screen readers, keyboard navigation and RTL layouts. |
| Appearance | 🟡 Partial | General wallpaper: local gallery and custom images in Settings → Chat Settings. Custom-image persistence checked across chats, profiles and restart; per-chat wallpaper is unavailable and removed from the chat menu. Full day/night combinations remain unverified. |
| External integration | ❓ Unverified | Camera capture and sharing into/out of the application. |
| Email presentation | ❓ Unverified | Mailing lists, sender identity and HTML mail display/search. |
| Message content | ❓ Unverified | Stickers, copying links and complete media rendering. |
| Trust information | ❓ Unverified | Core-backed verification details and “verified by” presentation. |
| Group duplication | ❓ Unverified | Cloning a group with correct Core membership semantics. |

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
