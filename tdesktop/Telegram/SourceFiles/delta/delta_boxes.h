/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "ui/rp_widget.h"

#include <QString>

#include <utility>
#include <vector>

namespace Main {
class Session;
} // namespace Main

namespace Window {
class SessionController;
} // namespace Window

namespace Delta {

void ShowMessageInfo(not_null<Window::SessionController*> controller, int messageId);
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

void ShowInviteQr();
void ShowSecondDevice();
void ShowRelays();
void ShowApps(not_null<Main::Session*> session);
[[nodiscard]] QString AppPickerUrl();
void SetAppPickerUrl(const QString &url);
void ShowAppPickerUrl();

void ShowCreateGroup(Fn<void(int chatId)> done);
void ChooseGroupMembers(not_null<Main::Session*> session,
	Fn<void(std::vector<int>, Fn<void(QString)>)> create);
void ShowContacts(not_null<Main::Session*> session, Fn<void(int chatId)> done);
void ShowChannelSubscribers(not_null<Main::Session*> session, int userId, Fn<void()> done = nullptr);
void ShowChannelInfo(not_null<Main::Session*> session, int userId);
void ShowChannelInvite(not_null<Main::Session*> session, int userId);

} // namespace Delta
