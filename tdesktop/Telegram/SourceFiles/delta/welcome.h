/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "delta/delta_rpc.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"
#include "ui/rp_widget.h"

#include <QImage>
#include <QJsonArray>

#include <memory>

namespace Window {
class Controller;
} // namespace Window

namespace Main {
class Account;
} // namespace Main

namespace Ui {
class FlatLabel;
class InputField;
class IconButton;
class LinkButton;
class PopupMenu;
class RoundButton;
class UserpicButton;
} // namespace Ui

class DeltaOnboarding final : public Ui::RpWidget {
public:
	DeltaOnboarding(
		QWidget *parent,
		not_null<Window::Controller*> controller,
		not_null<Main::Account*> account,
		std::shared_ptr<DeltaRpc> existing = nullptr);
	~DeltaOnboarding();

	void setInnerFocus();
	void setFinishedCallback(Fn<void()> callback);

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	enum class Page {
		Welcome,
		Profile,
	};

	void createControls();
	void startBackend();
	void bootstrap();
	void pumpEvents();
	void applyPage();
	void updateControlsGeometry();
	void updateTexts();
	void paintCover(QPainter &p);

	[[nodiscard]] int contentLeft() const;
	[[nodiscard]] int contentTop() const;

	void showWelcome();
	void showProfile();
	void showMenu();
	void showAlternative();
	void showOtherServer();
	void showClassicLogin();
	void toggleTeamProfile();
	void askProxy();
	void chooseDataFolder();
	void createProfile();
	void finishSetup(const QString &text);
	void openShell(int accountId);
	void chooseBackup();
	void askSecondDevice();
	void askInvitation();
	void ensureFreshAccount(DeltaRpc::Callback done);
	void setStatus(const QString &text);
	void closeLive();
	void call(
		const QString &method,
		const QJsonArray &params,
		DeltaRpc::Callback done);
	[[nodiscard]] QString saveAvatar() const;

	const not_null<Window::Controller*> _controller;
	const not_null<Main::Account*> _account;
	DeltaRpc *_rpc = nullptr;
	std::shared_ptr<DeltaRpc> _keep;
	bool _live = false;
	Fn<void()> _finished;
	int _accountId = 0;
	bool _anyConfigured = false;
	bool _accountReady = false;
	bool _teamProfile = false;
	bool _pumping = false;
	Page _page = Page::Welcome;
	QImage _cover;
	QImage _avatar;

	object_ptr<Ui::FlatLabel> _title = { nullptr };
	object_ptr<Ui::FlatLabel> _description = { nullptr };
	object_ptr<Ui::FlatLabel> _status = { nullptr };
	object_ptr<Ui::RoundButton> _next = { nullptr };
	object_ptr<Ui::LinkButton> _alternative = { nullptr };
	object_ptr<Ui::LinkButton> _privacy = { nullptr };
	object_ptr<Ui::IconButton> _back = { nullptr };
	object_ptr<Ui::RoundButton> _menuButton = { nullptr };
	object_ptr<Ui::UserpicButton> _photo = { nullptr };
	object_ptr<Ui::InputField> _name = { nullptr };
	base::unique_qptr<Ui::PopupMenu> _menu;
	bool _handedOver = false;
};
