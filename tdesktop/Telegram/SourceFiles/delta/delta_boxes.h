/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "ui/rp_widget.h"

#include <QString>

#include <memory>
#include <utility>
#include <vector>

class PeerData;

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
class SectionMemento;
struct SectionShow;
} // namespace Window

namespace Delta {

void ShowMessageInfo(not_null<Window::SessionController*> controller, int messageId);
void ShowSavedReplies(not_null<Window::SessionController*> controller);
void ConfirmContactBlocking(not_null<Window::SessionController*> controller,
	int contactId, QString name, bool blocked, Fn<void()> changed = nullptr);
void ChooseContactToBlock(not_null<Window::SessionController*> controller, Fn<void()> changed);
[[nodiscard]] bool CanManageContactBlocking(uint64 userId);
void ManageContactBlocking(not_null<Window::SessionController*> controller, uint64 userId);
void ShowSharedContact(not_null<Window::SessionController*> controller, int messageId);
void ShowJoinInvite(not_null<Window::SessionController*> controller, const QString &url);
void ChooseContactToSend(not_null<Main::Session*> session, int userId);

void AskText(
	const QString &title,
	const QString &placeholder,
	const QString &accept,
	Fn<void(QString)> done);

void AskChoice(
	const QString &title,
	std::vector<std::pair<QString, Fn<void()>>> options);

void ShowInviteQr(not_null<Window::SessionController*> controller);
void ShowSecondDevice(not_null<Window::SessionController*> controller);
void ShowBackupExport(not_null<Window::SessionController*> controller);
void ShowRelays(not_null<Window::SessionController*> controller);
void ShowApps(not_null<Main::Session*> session);
[[nodiscard]] QString AppPickerUrl();
void SetAppPickerUrl(const QString &url);
void ShowAppPickerUrl(not_null<Window::SessionController*> controller);

void ShowCreateGroup(Fn<void(int chatId)> done);
void ChooseGroupMembers(not_null<Main::Session*> session,
	Fn<void(std::vector<int>, Fn<void(QString)>)> create);
void ShowContacts(
	not_null<Window::SessionController*> controller,
	Fn<void(int chatId)> done);
void ShowChannelSubscribers(not_null<Main::Session*> session, int userId, Fn<void()> done = nullptr);
void ShowChannelInfo(not_null<Window::SessionController*> controller, int userId,
	const Window::SectionShow &params);
void ShowGroupInfo(not_null<Window::SessionController*> controller, int userId,
	const Window::SectionShow &params);
[[nodiscard]] std::shared_ptr<Window::SectionMemento> MakeGroupInfoMemento(
	int chatId);
[[nodiscard]] std::shared_ptr<Window::SectionMemento> GroupInfoMementoForPeer(
	not_null<PeerData*> peer);
[[nodiscard]] bool UnresolvedCoreGroupPeer(not_null<PeerData*> peer);
[[nodiscard]] bool TryShowGroupInfo(
	not_null<Window::SessionController*> controller,
	not_null<PeerData*> peer,
	const Window::SectionShow &params);
void ShowChannelInvite(not_null<Main::Session*> session, int userId);

} // namespace Delta
