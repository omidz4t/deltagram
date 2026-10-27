/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/welcome.h"

#include "delta/account_inventory.h"
#include "delta/core_accounts_path.h"
#include "delta/startup_state.h"
#include "delta/profile_setup_actions.h"
#include "delta/profile_setup_avatar.h"
#include "delta/backend_choice.h"
#include "delta/delta_boxes.h"
#include "delta/delta_bridge.h"

#include "base/weak_qptr.h"
#include "boxes/abstract_box.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "main/backend_startup.h"
#include "main/main_account.h"
#include "window/window_controller.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/userpic_button.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/fields/password_input.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_intro.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_userpic_button.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QTimer>

#include <memory>

namespace {

const auto kPrivacyUrl = u"https://delta.chat/gdpr"_q;
const auto kRelaysUrl = u"https://chatmail.at/relays"_q;

[[nodiscard]] QString RpcError(const QJsonObject &reply) {
	const auto error = reply.value(u"error"_q);
	if (error.isObject()) {
		const auto message = error.toObject().value(u"message"_q).toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	if (error.isString()) {
		return error.toString();
	}
	return u"Request failed."_q;
}

[[nodiscard]] bool HasError(const QJsonObject &reply) {
	return reply.contains(u"error"_q);
}

[[nodiscard]] QImage LoadCoverImage() {
	auto embedded = QImage(u":/gui/art/logo_256_no_margin.png"_q);
	if (!embedded.isNull()) {
		return embedded;
	}
	const auto dir = QCoreApplication::applicationDirPath();
	const QStringList candidates = {
		dir + u"/intro1.png"_q,
		dir + u"/../intro1.png"_q,
		QDir::currentPath() + u"/intro1.png"_q,
	};
	for (const auto &path : candidates) {
		if (QFileInfo::exists(path)) {
			auto image = QImage(path);
			if (!image.isNull()) {
				return image;
			}
		}
	}
	return {};
}

using Delta::AskChoice;
using Delta::AskText;

} // namespace

DeltaOnboarding::DeltaOnboarding(
	QWidget *parent,
	not_null<Window::Controller*> controller,
	not_null<Main::Account*> account,
	std::shared_ptr<DeltaRpc> existing)
: RpWidget(parent)
, _controller(controller)
, _account(account)
, _keep(std::move(existing))
, _cover(LoadCoverImage()) {
	if (_keep) {
		_rpc = _keep.get();
		_live = true;
		_anyConfigured = true;
		_startup.markLiveAdd();
	}
	_setupTable = Delta::ProfileSetupTableFor(_rpc);
	createControls();
	applyPage();
	updateSetupLock();
	if (!_live) {
		startBackend();
	}
}

void DeltaOnboarding::setFinishedCallback(Fn<void()> callback) {
	_finished = std::move(callback);
}

void DeltaOnboarding::closeLive() {
	if (_creatingProfile) {
		return;
	}
	const auto done = _finished;
	if (done) {
		crl::on_main(this, done);
	}
}

DeltaOnboarding::~DeltaOnboarding() = default;

void DeltaOnboarding::createControls() {
	_next.create(this, rpl::single(QString()), st::introNextButton);
	_next->setClickedCallback([=] { activatePrimary(); });

	_alternative.create(this, u"I Already Have a Profile"_q, st::introLink);
	_alternative->setClickedCallback([=] { showAlternative(); });

	_privacy.create(this, u"Privacy Policy"_q, st::introLink);
	_privacy->setClickedCallback([=] {
		UrlClickHandler::Open(kPrivacyUrl);
	});

	_back.create(this, st::introBackButton);
	_back->setClickedCallback([=] {
		if (setupBusy()) {
			return;
		}
		if (_page == Page::Welcome) {
			closeLive();
		} else {
			showWelcome();
		}
	});

	_menuButton.create(this, rpl::single(u"Menu"_q), st::defaultBoxButton);
	_menuButton->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	_menuButton->setClickedCallback([=] { showMenu(); });

	_photo.create(
		this,
		_controller,
		Ui::UserpicButton::Role::ChoosePhoto,
		st::defaultUserpicButton);
	_photo->showCustomOnChosen();
	_photo->chosenImages(
	) | rpl::on_next([=](Ui::UserpicButton::ChosenImage &&chosen) {
		_avatar = std::move(chosen.image);
	}, _photo->lifetime());

	_name.create(
		this,
		st::introName,
		rpl::single(u"Your Name"_q));
	_name->submits() | rpl::on_next([=] {
		createProfile();
	}, _name->lifetime());

	_status.create(this, QString(), st::introErrorCentered);
	_status->hide();
}

QString AccountsPathFile() {
	return Delta::CoreAccountsPathFile();
}

QString ResolveAccountsPath() {
	return Delta::ResolveCoreAccountsPath();
}

void DeltaOnboarding::chooseDataFolder() {
	FileDialog::GetFolder(
		this,
		u"Data Folder"_q,
		ResolveAccountsPath(),
		crl::guard(this, [=](QString &&path) {
			if (path.isEmpty()) {
				return;
			}
			QDir().mkpath(QFileInfo(AccountsPathFile()).absolutePath());
			auto file = QFile(AccountsPathFile());
			if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
				file.write(path.toUtf8());
			}
			Ui::show(Ui::MakeInformBox(
				u"Profiles will be stored in:\n%1\n\nRestart the app to use this folder."_q.arg(path)));
		}));
}

void DeltaOnboarding::startBackend() {
	if (_account->backend() != Main::AccountBackend::Core) {
		_startup.failDiscovery(
			tr::lng_delta_account_core_required(tr::now));
		syncStartupUi(_startup.error);
		return;
	}
	const auto program = qEnvironmentVariable("DELTA_TEL_RPC");
	const auto accounts = ResolveAccountsPath();
	QDir().mkpath(accounts);
	if (program.isEmpty()) {
		_startup.failDiscovery(u"DELTA_TEL_RPC is not set."_q);
		syncStartupUi(_startup.error);
		return;
	}
	_rpc = new DeltaRpc(this);
	_setupTable = Delta::ProfileSetupTableFor(_rpc);
	_rpc->start(program, accounts);
	QTimer::singleShot(400, this, [=] {
		bootstrap();
		pumpEvents();
	});
}

void DeltaOnboarding::setInnerFocus() {
	if (_page == Page::Profile) {
		_name->setFocusFast();
	} else {
		_next->setFocus(Qt::OtherFocusReason);
	}
}

void DeltaOnboarding::applyPage() {
	const auto welcome = (_page == Page::Welcome);
	updateTexts();
	_next->setVisible(true);
	_alternative->setVisible(welcome);
	_privacy->setVisible(!welcome);
	_back->setVisible(!welcome || _anyConfigured);
	_back->raise();
	_menuButton->setVisible(!welcome);
	_photo->setVisible(!welcome);
	_name->setVisible(!welcome);
	updateControlsGeometry();
	update();
}

void DeltaOnboarding::updateTexts() {
	const auto welcome = (_page == Page::Welcome);
	QString title;
	QString description;
	if (welcome) {
		title = _anyConfigured
			? u"Add Profile"_q
			: u"Welcome to Delta Chat"_q;
		description = u"Secure Decentralized Chat"_q;
	} else {
		title = _teamProfile
			? u"Create Team Profile"_q
			: u"Your Profile"_q;
		description = _teamProfile
			? u"A \"Team Profile\" is managed collectively by a group of people or an organization. Team Profiles are experimental and subject to change."_q
			: u"Enter your name and add a profile picture."_q;
		_name->setPlaceholder(rpl::single(_teamProfile
			? u"Team Name"_q
			: u"Your Name"_q));
	}
	_next->setText(rpl::single(_startup.nextLabel(welcome)));
	_title.create(
		this,
		title,
		welcome ? st::introCoverTitle : st::introTitle);
	_description.create(
		this,
		description,
		welcome ? st::introCoverDescription : st::introDescription);
	_title->show();
	_description->show();
	_status->raise();
	_next->raise();
}

int DeltaOnboarding::contentLeft() const {
	return (width() - st::introNextButton.width) / 2;
}

int DeltaOnboarding::contentTop() const {
	auto result = std::max(
		(height() - st::introHeight) / 2,
		int(st::introStepTopMin));
	if (_page == Page::Welcome) {
		const auto full = result + st::introNextTop + st::introContentTopAdd;
		const auto added = 1. - std::clamp(
			float64(full - st::windowMinHeight)
				/ (st::introStepHeightFull - st::windowMinHeight),
			0.,
			1.);
		result += int(base::SafeRound(added * st::introContentTopAdd));
	}
	return result;
}

void DeltaOnboarding::updateControlsGeometry() {
	if (!_title || !_description || !_next) {
		return;
	}
	const auto top = contentTop();
	const auto welcome = (_page == Page::Welcome);
	auto statusTop = top + st::introErrorTop;
	_next->moveToLeft((width() - _next->width()) / 2, top + st::introNextTop);
	if (welcome) {
		_title->moveToLeft(
			(width() - _title->width()) / 2,
			top + st::introCoverTitleTop);
		_description->moveToLeft(
			(width() - _description->width()) / 2,
			top + st::introCoverDescriptionTop);
		_alternative->moveToLeft(
			(width() - _alternative->width()) / 2,
			_next->y() + _next->height() + _alternative->height());
		if (_anyConfigured) {
			_back->moveToLeft(0, 0);
		}
	} else {
		const auto left = contentLeft() + st::buttonRadius;
		const auto photoRight = contentLeft() + st::introNextButton.width;
		const auto photoLeft = photoRight - _photo->width();
		_photo->moveToLeft(
			photoLeft,
			top + st::introPhotoTop);
		const auto titleWidth = std::max(
			st::introSettingsSkip,
			photoLeft - left - st::introSettingsSkip);
		_title->resizeToNaturalWidth(titleWidth);
		_title->moveToLeft(left, top + st::introTitleTop);
		// introDescription.minWidth will not wrap below 300px.
		_description->resizeToWidth(st::introDescription.minWidth);
		const auto belowHeader = std::max(
			_title->y() + _title->height(),
			_photo->y() + _photo->height()) + st::introPhoneTop;
		_description->moveToLeft(
			left,
			std::max(top + st::introDescriptionTop, belowHeader));
		_name->moveToLeft(
			contentLeft(),
			std::max(
				top + st::introStepFieldTop,
				_description->y()
					+ _description->height()
					+ st::introPhoneTop));
		statusTop = std::max(
			statusTop,
			_name->y() + _name->height() + st::introPhoneTop);
		_next->moveToLeft(
			(width() - _next->width()) / 2,
			std::max(
				top + st::introNextTop,
				statusTop + (st::introNextTop - st::introErrorTop)));
		_privacy->moveToLeft(
			(width() - _privacy->width()) / 2,
			_next->y() + _next->height() + _privacy->height());
		_back->moveToLeft(0, 0);
		_menuButton->moveToRight(
			st::introSettingsSkip,
			st::introSettingsSkip);
	}
	_status->resizeToWidth(width());
	_status->moveToLeft(0, statusTop);
}

void DeltaOnboarding::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), st::windowBg);
	if (_page == Page::Welcome) {
		paintCover(p);
	}
}

void DeltaOnboarding::paintCover(QPainter &p) {
	const auto coverHeight = st::introCoverHeight;
	auto gradient = QLinearGradient(0, 0, 0, coverHeight);
	gradient.setColorAt(0., st::introCoverTopBg->c);
	gradient.setColorAt(1., st::introCoverBottomBg->c);
	p.fillRect(0, 0, width(), coverHeight, gradient);

	auto left = 0;
	auto right = 0;
	if (width() < st::introCoverMaxWidth) {
		const auto iconsMaxSkip = st::introCoverMaxWidth
			- st::introCoverLeft.width()
			- st::introCoverRight.width();
		const auto iconsSkip = st::introCoverIconsMinSkip
			+ (iconsMaxSkip - st::introCoverIconsMinSkip)
				* (width() - st::introStepWidth)
				/ (st::introCoverMaxWidth - st::introStepWidth);
		const auto outside = iconsSkip
			+ st::introCoverLeft.width()
			+ st::introCoverRight.width()
			- width();
		left = -outside / 2;
		right = -outside - left;
	}
	st::introCoverLeft.paint(
		p,
		left,
		coverHeight - st::introCoverLeft.height(),
		width());
	st::introCoverRight.paint(
		p,
		width() - right - st::introCoverRight.width(),
		coverHeight - st::introCoverRight.height(),
		width());

	if (!_cover.isNull()) {
		const auto target = _cover.scaled(
			QSize(width(), coverHeight - 48) * style::DevicePixelRatio(),
			Qt::KeepAspectRatio,
			Qt::SmoothTransformation);
		const auto size = target.size() / style::DevicePixelRatio();
		p.drawImage(
			QRect(
				QPoint((width() - size.width()) / 2, 24),
				size),
			target);
	} else {
		st::introCoverIcon.paint(
			p,
			(width() - st::introCoverIcon.width()) / 2
				- st::introCoverIconLeft,
			st::introCoverIconTop,
			width());
	}
}

void DeltaOnboarding::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void DeltaOnboarding::openShell(int accountId) {
	if (_handedOver || !_rpc || accountId == 0) {
		return;
	}
	if (_startup.knownAccountId != 0 && accountId != _startup.knownAccountId) {
		return;
	}
	const auto failOpen = [=](const QString &error) {
		_handedOver = false;
		_startup.failOpen(error);
		syncStartupUi(error);
	};
	if (_live) {
		_handedOver = true;
		updateSetupLock();
		Delta::SwitchProfile(accountId, crl::guard(this, [=](QString error) {
			if (!error.isEmpty()) {
				failOpen(error);
				return;
			}
			Delta::ProfileSetupNoteOpened(_setupTable, accountId);
			closeLive();
		}));
		return;
	}
	const auto otherOwns = Delta::Active()
		&& (!_account->sessionExists()
			|| !Delta::OwnsSession(&_account->session()));
	const auto startCheck = Main::EvaluateCoreSessionStart({
		_account->backend(),
		_account->sessionExists(),
		otherOwns,
	});
	if (startCheck != Main::CoreSessionStartResult::Allowed) {
		failOpen(Delta::CoreSessionStartErrorText(startCheck));
		return;
	}
	_handedOver = true;
	updateSetupLock();
	const auto startIo = [=] {
		call(u"start_io"_q, {accountId}, [=](QJsonObject reply) {
			if (HasError(reply)) {
				failOpen(RpcError(reply));
				return;
			}
			call(u"get_config"_q, {accountId, u"displayname"_q}, [=](QJsonObject config) {
				if (HasError(config)) {
					failOpen(RpcError(config));
					return;
				}
				const auto name = config.value(u"result"_q).toString();
				Delta::ProfileSetupNoteOpened(_setupTable, accountId);
				const auto otherOwns = Delta::Active()
					&& (!_account->sessionExists()
						|| !Delta::OwnsSession(&_account->session()));
				const auto startCheck = Main::EvaluateCoreSessionStart({
					_account->backend(),
					_account->sessionExists(),
					otherOwns,
				});
				if (startCheck != Main::CoreSessionStartResult::Allowed) {
					failOpen(Delta::CoreSessionStartErrorText(startCheck));
					return;
				}
				const auto rpc = std::shared_ptr<DeltaRpc>(_rpc);
				_rpc = nullptr;
				rpc->setParent(nullptr);
				Delta::StartSession(
					_account,
					rpc,
					accountId,
					name.isEmpty() ? u"Me"_q : name);
			});
		});
	};
	// WHY: Open retry must select the same known id; a failed select_account
	// cannot fall through to start_io on whatever profile is current.
	if (_startup.knownAccountId == accountId) {
		call(u"select_account"_q, {accountId}, [=](QJsonObject reply) {
			if (HasError(reply)) {
				failOpen(RpcError(reply));
				return;
			}
			startIo();
		});
		return;
	}
	startIo();
}

void DeltaOnboarding::keyPressEvent(QKeyEvent *e) {
	if (setupBusy()) {
		e->accept();
		return;
	}
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (_live && _page == Page::Welcome) {
			closeLive();
		} else if (_page == Page::Profile) {
			showWelcome();
		}
	} else if (e->key() == Qt::Key_Enter
		|| e->key() == Qt::Key_Return
		|| e->key() == Qt::Key_Space) {
		activatePrimary();
	}
}

void DeltaOnboarding::setStatus(const QString &text) {
	_status->setText(text);
	_status->setVisible(!text.isEmpty());
	updateControlsGeometry();
}

void DeltaOnboarding::call(
		const QString &method,
		const QJsonArray &params,
		DeltaRpc::Callback done) {
	if (!_rpc) {
		const auto error = u"Chatmail core is not running."_q;
		setStatus(error);
		if (done) done(QJsonObject{{ u"error"_q, error }});
		return;
	}
	_rpc->call(method, params, crl::guard(this, [done = std::move(done)](QJsonObject reply) {
		if (done) done(std::move(reply));
	}));
}

void DeltaOnboarding::showWelcome() {
	if (setupBusy()) {
		return;
	}
	_teamProfile = false;
	_page = Page::Welcome;
	setStatus(QString());
	applyPage();
}

void DeltaOnboarding::showProfile() {
	if (_startup.primaryAction() == Delta::StartupState::Action::OpenKnown) {
		openShell(_startup.knownAccountId);
		return;
	}
	if (setupBusy() || !_startup.canCreate()) {
		return;
	}
	_page = Page::Profile;
	setStatus(QString());
	applyPage();
	_name->setFocusFast();
	ensureFreshAccount([=](QJsonObject reply) {
		if (HasError(reply)) {
			setStatus(RpcError(reply));
		}
	});
}

void DeltaOnboarding::activatePrimary() {
	if (setupBusy()) {
		return;
	}
	switch (_startup.primaryAction()) {
	case Delta::StartupState::Action::None:
		return;
	case Delta::StartupState::Action::RetryDiscovery:
		bootstrap();
		return;
	case Delta::StartupState::Action::OpenKnown:
		openShell(_startup.knownAccountId);
		return;
	case Delta::StartupState::Action::Create:
		if (_page == Page::Welcome) {
			showProfile();
		} else {
			createProfile();
		}
		return;
	}
}

void DeltaOnboarding::syncStartupUi(const QString &status) {
	setStatus(status);
	updateTexts();
	updateSetupLock();
	updateControlsGeometry();
}

void DeltaOnboarding::showMenu() {
	if (setupBusy() || !_startup.canCreate()) {
		return;
	}
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::defaultPopupMenu);
	_menu->addAction(u"Use Other Server"_q, [=] { showOtherServer(); });
	_menu->addAction(u"Use Proxy"_q, [=] { askProxy(); });
	_menu->addAction(u"Data Folder"_q, [=] { chooseDataFolder(); });
	_menu->addAction(
		_teamProfile
			? u"Create Personal Profile"_q
			: u"Create Team Profile"_q,
		[=] { toggleTeamProfile(); });
	_menu->popup(_menuButton->mapToGlobal(
		QPoint(0, _menuButton->height())));
}

void DeltaOnboarding::toggleTeamProfile() {
	if (_teamProfile) {
		_teamProfile = false;
		updateTexts();
		updateControlsGeometry();
		return;
	}
	const auto confirmed = Fn<void(Fn<void()>)>(crl::guard(this, [=](
			Fn<void()> close) {
		_teamProfile = true;
		updateTexts();
		updateControlsGeometry();
		close();
	}));
	Ui::show(Ui::MakeConfirmBox({
		.text = u"A \"Team Profile\" is managed collectively by a group of people or an organization.\n\nTeam Profiles are experimental and subject to change."_q,
		.confirmed = confirmed,
		.confirmText = u"Continue"_q,
	}));
}

void DeltaOnboarding::askProxy() {
	if (_accountId == 0) {
		return;
	}
	AskText(
		u"Use Proxy"_q,
		u"Proxy URL"_q,
		u"Save"_q,
		crl::guard(this, [=](QString url) {
			call(u"set_config"_q, {_accountId, u"proxy_url"_q, url}, [=](QJsonObject reply) {
				if (HasError(reply)) {
					setStatus(RpcError(reply));
					return;
				}
				call(u"set_config"_q, {_accountId, u"proxy_enabled"_q, u"1"_q}, [=](QJsonObject reply) {
					setStatus(HasError(reply)
						? RpcError(reply)
						: u"Proxy saved."_q);
				});
			});
		}));
}

void DeltaOnboarding::pumpEvents() {
	if (_pumping || !_rpc) {
		return;
	}
	_pumping = true;
	call(u"get_next_event"_q, {}, crl::guard(this, [=](QJsonObject reply) {
		_pumping = false;
		if (HasError(reply)) {
			QTimer::singleShot(1000, this, [=] { pumpEvents(); });
			return;
		}
		const auto envelope = reply.value(u"result"_q).toObject();
		if (const auto text = Delta::ProfileSetupProgressStatus(
				envelope,
				_setupTable,
				_accountId)) {
			setStatus(*text);
		}
		pumpEvents();
	}));
}

void DeltaOnboarding::bootstrap() {
	const auto generation = _startup.beginDiscovery();
	if (!_rpc) {
		_startup.failDiscovery(u"Chatmail core is not running."_q);
		syncStartupUi(_startup.error);
		return;
	}
	syncStartupUi(Delta::kStartupLoading);
	Delta::ReadAccountInventory([=](QString method, QJsonArray params, DeltaRpc::Callback done) {
		call(method, params, std::move(done));
	}, [=](Delta::AccountInventory inventory) {
		if (!_startup.live(generation)) {
			return;
		}
		if (!inventory.error.isEmpty()) {
			_startup.failDiscovery(inventory.error);
			syncStartupUi(_startup.error);
			return;
		}
		_anyConfigured = !inventory.configured.empty();
		if (!_anyConfigured) {
			_startup.readyUnconfigured();
			_accountId = inventory.unconfigured;
			_accountReady = (_accountId != 0);
			syncStartupUi(QString());
			if (qEnvironmentVariableIsSet("DELTA_TEL_FORCE_OPEN") && _accountId != 0) {
				openShell(_accountId);
			}
			return;
		}
		call(u"get_selected_account_id"_q, {}, [=](QJsonObject selected) {
			if (!_startup.live(generation)) {
				return;
			}
			const auto read = Delta::ReadSelectedAccount(selected);
			if (!read.ok) {
				_startup.failDiscovery(HasError(selected)
					? RpcError(selected)
					: read.error);
				syncStartupUi(_startup.error);
				return;
			}
			const auto openId = Delta::ConfiguredOpenIdOrZero(
				inventory.configured,
				read);
			if (openId <= 0) {
				_startup.failDiscovery(Delta::kStartupInvalidSelected);
				syncStartupUi(_startup.error);
				return;
			}
			_startup.readyConfigured(openId);
			syncStartupUi(QString());
			openShell(openId);
		});
	});
}

void DeltaOnboarding::ensureFreshAccount(DeltaRpc::Callback callback) {
	if (!_startup.canCreate()) {
		const auto message = !_startup.error.isEmpty()
			? _startup.error
			: Delta::kStartupLoading;
		if (callback) {
			callback(QJsonObject{{ u"error"_q, message }});
		}
		return;
	}
	if (callback) _accountReadyCallbacks.push_back(crl::guard(this, std::move(callback)));
	if (_preparingAccount) return;
	_preparingAccount = true;
	const auto done = DeltaRpc::Callback([=](QJsonObject reply) {
		_preparingAccount = false;
		auto callbacks = std::move(_accountReadyCallbacks);
		_accountReadyCallbacks.clear();
		for (const auto &callback : callbacks) callback(reply);
	});
	if (_accountReady && _accountId != 0) {
		// Setup may have completed despite a lost acknowledgement. Never apply
		// creation settings again without confirming the account is still fresh.
		call(u"is_configured"_q, { _accountId }, [=](QJsonObject reply) {
			const auto value = reply.value(u"result"_q);
			if (done) done(!HasError(reply) && value.isBool() && !value.toBool()
				? QJsonObject()
				: QJsonObject{{ u"error"_q, u"Could not confirm an unconfigured profile. Reopen profiles before continuing."_q }});
		});
		return;
	}
	Delta::ReadAccountInventory([=](QString method, QJsonArray params, DeltaRpc::Callback callback) {
		call(method, params, std::move(callback));
	}, [=](Delta::AccountInventory inventory) {
		if (!inventory.error.isEmpty()) {
			if (done) done(QJsonObject{{ u"error"_q, inventory.error }});
			return;
		}
		if (inventory.unconfigured != 0) {
			_accountId = inventory.unconfigured;
			_accountReady = true;
			if (done) done(QJsonObject());
			return;
		}
		call(u"add_account"_q, {}, [=](QJsonObject created) {
			const auto value = created.value(u"result"_q);
			const auto id = value.toInt();
			if (!HasError(created)) {
				if (!value.isDouble() || id <= 0 || value.toDouble() != id) {
					if (done) done(QJsonObject{{ u"error"_q, u"Core returned an invalid profile identifier."_q }});
					return;
				}
				_accountId = id;
				_accountReady = true;
			}
			if (done) done(std::move(created));
		});
	});
}

void DeltaOnboarding::showAlternative() {
	if (setupMutationBlocked()) {
		if (_startup.completedOpen()) {
			return;
		}
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	ensureFreshAccount([=](QJsonObject reply) {
		if (setupMutationBlocked()) {
			return;
		}
		if (HasError(reply)) {
			setStatus(RpcError(reply));
			return;
		}
		AskChoice(u"I Already Have a Profile"_q, {
			{ u"Add as Second Device"_q, crl::guard(this, [=] {
				askSecondDevice();
			}) },
			{ u"Restore from Backup"_q, crl::guard(this, [=] {
				chooseBackup();
			}) },
		});
	});
}

void DeltaOnboarding::askSecondDevice() {
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	AskText(
		u"Add as Second Device"_q,
		u"Setup code from the other device"_q,
		u"Continue"_q,
		crl::guard(this, [=](QString code) {
			if (setupMutationBlocked() || code.isEmpty()) {
				return;
			}
			lockSetup();
			ensureFreshAccount([=](QJsonObject ready) {
				if (HasError(ready) || _accountId == 0) {
					unlockSetup();
					setStatus(HasError(ready)
						? RpcError(ready)
						: Delta::kProfileSetupUnavailable);
					return;
				}
				const auto captured = _accountId;
				_setupLease = Delta::TryAcquireProfileSetup(
					_setupTable,
					captured,
					Delta::ProfileSetupKind::Copy);
				if (!_setupLease) {
					unlockSetup();
					setStatus(Delta::kProfileSetupBusy);
					return;
				}
				setStatus(u"Copying the profile from the other device…"_q);
				const auto weak = base::make_weak(this);
				auto lease = std::move(_setupLease);
				Delta::CopyProfileBackup(
					mutationRequest(),
					std::move(lease),
					code,
					[=](Delta::ProfileSetupResult result) {
						const auto self = weak.get();
						if (!self) {
							return;
						}
						self->applySetupResult(result, u"Profile copied."_q);
					});
			});
		}));
}

void DeltaOnboarding::chooseBackup() {
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	lockSetup();
	ensureFreshAccount([=](QJsonObject ready) {
		if (HasError(ready) || _accountId == 0) {
			unlockSetup();
			setStatus(HasError(ready)
				? RpcError(ready)
				: Delta::kProfileSetupUnavailable);
			return;
		}
		const auto captured = _accountId;
		_setupLease = Delta::TryAcquireProfileSetup(
			_setupTable,
			captured,
			Delta::ProfileSetupKind::Import);
		if (!_setupLease) {
			unlockSetup();
			setStatus(Delta::kProfileSetupBusy);
			return;
		}
		FileDialog::GetOpenPath(
			this,
			u"Restore from Backup"_q,
			u"Backup (*.tar *.bak)"_q,
			crl::guard(this, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					unlockSetup();
					return;
				}
				if (_accountId != captured
					|| !_setupLease
					|| _setupLease->accountId != captured) {
					unlockSetup();
					setStatus(Delta::kProfileSetupUnavailable);
					return;
				}
				askBackupPassphrase(captured, result.paths.front());
			}),
			crl::guard(this, [=] {
				unlockSetup();
			}));
	});
}

void DeltaOnboarding::askBackupPassphrase(int accountId, const QString &path) {
	const auto weak = base::make_weak(this);
	const auto consumed = std::make_shared<bool>(false);
	_controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_delta_backup_passphrase_title());
		box->boxClosing() | rpl::on_next([=] {
			if (!*consumed) {
				*consumed = true;
				if (const auto self = weak.get()) {
					self->unlockSetup();
				}
			}
		}, box->lifetime());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_delta_backup_passphrase_about(),
			st::boxLabel));
		const auto row = box->addRow(object_ptr<Ui::RpWidget>(box));
		row->resize(row->width(), st::defaultInputField.heightMin);
		const auto password = Ui::CreateChild<Ui::PasswordInput>(
			row,
			st::defaultInputField,
			tr::lng_delta_backup_passphrase_placeholder());
		password->setInputMethodHints(Qt::ImhSensitiveData
			| Qt::ImhNoPredictiveText | Qt::ImhHiddenText);
		row->widthValue() | rpl::on_next([=](int width) {
			password->resize(width, password->height());
		}, password->lifetime());
		const auto submit = [=] {
			if (*consumed) {
				return;
			}
			*consumed = true;
			const auto secret = password->getLastText();
			box->closeBox();
			const auto self = weak.get();
			if (!self) {
				return;
			}
			if (self->_accountId != accountId || !self->_setupLease
				|| self->_setupLease->accountId != accountId) {
				self->unlockSetup();
				self->setStatus(Delta::kProfileSetupUnavailable);
				return;
			}
			self->setStatus(u"Restoring profile…"_q);
			auto lease = std::move(self->_setupLease);
			Delta::ImportProfileBackup(
				self->mutationRequest(),
				std::move(lease),
				path,
				[weak](Delta::ProfileSetupResult result) {
					if (const auto self = weak.get()) {
						self->applySetupResult(result, u"Profile restored."_q);
					}
				},
				secret);
		};
		QObject::connect(password, &Ui::PasswordInput::submitted,
			password, [=](Qt::KeyboardModifiers) { submit(); });
		box->setFocusCallback([=] { password->setFocusFast(); });
		box->addButton(tr::lng_delta_backup_restore_continue(), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void DeltaOnboarding::showOtherServer() {
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	AskChoice(u"Use Other Server"_q, {
		{ u"List Chatmail Servers"_q, [] {
			UrlClickHandler::Open(kRelaysUrl);
		} },
		{ u"Use Classic Email as Relay"_q, crl::guard(this, [=] {
			showClassicLogin();
		}) },
		{ u"Enter Invitation Code"_q, crl::guard(this, [=] {
			askInvitation();
		}) },
	});
}

void DeltaOnboarding::askInvitation() {
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	AskText(
		u"Enter Invitation Code"_q,
		u"Invitation or server code"_q,
		u"Continue"_q,
		crl::guard(this, [=](QString code) {
			if (setupMutationBlocked() || code.isEmpty()) {
				return;
			}
			lockSetup();
			ensureFreshAccount([=](QJsonObject ready) {
				if (HasError(ready) || _accountId == 0) {
					unlockSetup();
					setStatus(HasError(ready)
						? RpcError(ready)
						: Delta::kProfileSetupUnavailable);
					return;
				}
				const auto captured = _accountId;
				_setupLease = Delta::TryAcquireProfileSetup(
					_setupTable,
					captured,
					Delta::ProfileSetupKind::Create);
				if (!_setupLease) {
					unlockSetup();
					setStatus(Delta::kProfileSetupBusy);
					return;
				}
				setStatus(u"Creating profile…"_q);
				const auto weak = base::make_weak(this);
				auto lease = std::move(_setupLease);
				Delta::CommitProfileSetup(
					mutationRequest(),
					std::move(lease),
					u"init_transports"_q,
					{ captured, code },
					[=](Delta::ProfileSetupResult result) {
						const auto self = weak.get();
						if (!self) {
							return;
						}
						self->applySetupResult(result, u"Profile created."_q);
					});
			});
		}));
}

void DeltaOnboarding::showClassicLogin() {
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	const auto weak = base::make_weak(this);
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Use Classic Email as Relay"_q));
		const auto email = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Email Address"_q)));
		box->addSkip(st::defaultInputField.heightMin / 4);
		const auto passwordRow = box->addRow(object_ptr<Ui::RpWidget>(box));
		passwordRow->resize(
			passwordRow->width(),
			st::defaultInputField.heightMin);
		const auto password = Ui::CreateChild<Ui::PasswordInput>(
			passwordRow,
			st::defaultInputField,
			rpl::single(u"Password"_q));
		passwordRow->widthValue(
		) | rpl::on_next([=](int width) {
			password->resize(width, password->height());
		}, password->lifetime());
		const auto submit = [=] {
			const auto address = email->getLastText().trimmed();
			if (address.isEmpty()) {
				email->showError();
				return;
			}
			if (password->getLastText().isEmpty()) {
				password->showError();
				return;
			}
			const auto secret = password->getLastText();
			box->closeBox();
			const auto strong = weak.get();
			if (!strong || strong->setupMutationBlocked()) {
				return;
			}
			strong->lockSetup();
			strong->ensureFreshAccount([=](QJsonObject ready) {
				const auto self = weak.get();
				if (!self) {
					return;
				}
				if (HasError(ready) || self->_accountId == 0) {
					self->unlockSetup();
					self->setStatus(HasError(ready)
						? RpcError(ready)
						: Delta::kProfileSetupUnavailable);
					return;
				}
				const auto captured = self->_accountId;
				self->_setupLease = Delta::TryAcquireProfileSetup(
					self->_setupTable,
					captured,
					Delta::ProfileSetupKind::Create);
				if (!self->_setupLease) {
					self->unlockSetup();
					self->setStatus(Delta::kProfileSetupBusy);
					return;
				}
				const auto lease = std::move(self->_setupLease);
				const auto request = self->mutationRequest();
				const auto fail = [=](QString error) {
					Delta::ReleaseProfileSetup(lease);
					const auto self = weak.get();
					if (!self) {
						return;
					}
					self->unlockSetup();
					self->setStatus(error);
				};
				const auto start = [=] {
					QJsonObject param;
					param.insert(QStringLiteral("addr"), address);
					param.insert(QStringLiteral("password"), secret);
					const auto self = weak.get();
					if (self) {
						self->setStatus(u"Contacting the mail server…"_q);
					}
					Delta::CommitProfileSetup(
						request,
						lease,
						u"add_or_update_transport"_q,
						{ captured, param },
						[=](Delta::ProfileSetupResult result) {
							const auto self = weak.get();
							if (!self) {
								return;
							}
							self->applySetupResult(result, u"Profile created."_q);
						});
				};
				const auto name = self->_name->getLastText().trimmed();
				if (name.isEmpty()) {
					start();
					return;
				}
				request(
					u"set_config"_q,
					{ captured, u"displayname"_q, name },
					[=](QJsonObject reply) {
						if (HasError(reply)) {
							fail(RpcError(reply));
							return;
						}
						start();
					});
			});
		};
		email->submits() | rpl::on_next([=] {
			password->setFocus();
		}, email->lifetime());
		QObject::connect(
			password,
			&Ui::PasswordInput::submitted,
			password,
			[=](Qt::KeyboardModifiers) { submit(); });
		box->setFocusCallback([=] {
			email->setFocusFast();
		});
		box->addButton(rpl::single(u"Continue"_q), submit);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

std::shared_ptr<Delta::ProfileSetupAvatarFile> DeltaOnboarding::saveAvatar() const {
	if (_avatar.isNull()) {
		return nullptr;
	}
	auto bytes = QByteArray();
	QBuffer buffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly)
		|| !_avatar.save(&buffer, "PNG")
		|| bytes.isEmpty()) {
		return nullptr;
	}
	return Delta::StageProfileSetupAvatar(bytes);
}

void DeltaOnboarding::updateSetupLock() {
	const auto mutating = _creatingProfile || _handedOver;
	const auto allowCreate = _startup.canCreate();
	_next->setDisabled(mutating || !_startup.nextEnabled());
	_alternative->setDisabled(mutating || !allowCreate);
	_back->setDisabled(mutating);
	_menuButton->setDisabled(mutating || !allowCreate);
	_name->setDisabled(mutating || !allowCreate);
	_photo->setDisabled(mutating || !allowCreate);
}

void DeltaOnboarding::setCreatingProfile(bool creating) {
	_creatingProfile = creating;
	updateSetupLock();
}

void DeltaOnboarding::lockSetup() {
	setCreatingProfile(true);
}

void DeltaOnboarding::unlockSetup() {
	Delta::ReleaseProfileSetup(_setupLease);
	_setupLease.reset();
	setCreatingProfile(false);
}

bool DeltaOnboarding::setupBusy() const {
	return _creatingProfile || _handedOver;
}

bool DeltaOnboarding::setupMutationBlocked() const {
	if (!_startup.canCreate() || setupBusy()) {
		return true;
	}
	return _setupTable
		&& _accountId > 0
		&& Delta::ProfileSetupBlocksMutation(_setupTable, _accountId);
}

Delta::AccountInventoryRequest DeltaOnboarding::mutationRequest() const {
	// WHY: import/create writes must finish after this widget dies so a live
	// window cannot drop Core mutation ownership while the shared RPC continues.
	if (_keep) {
		return Delta::MakeOwnedProfileSetupRequest(_keep);
	}
	const auto rpc = _rpc;
	return [rpc](QString method, QJsonArray params, DeltaRpc::Callback done) {
		if (!rpc) {
			if (done) {
				done(QJsonObject{{ u"error"_q, u"Chatmail core is not running."_q }});
			}
			return;
		}
		rpc->call(method, params, std::move(done));
	};
}

void DeltaOnboarding::applySetupResult(
		const Delta::ProfileSetupResult &result,
		const QString &successText) {
	const auto captured = result.accountId;
	if (Delta::ProfileSetupShouldOpen(result, captured)
		&& captured == _accountId) {
		_startup.markCompleted(captured);
		_anyConfigured = true;
		_accountReady = true;
		Delta::ReleaseProfileSetup(_setupLease);
		_setupLease.reset();
		setCreatingProfile(false);
		syncStartupUi(result.outcome == Delta::ProfileSetupOutcome::Succeeded
			? successText
			: result.error);
		openShell(captured);
		return;
	}
	if (Delta::ProfileSetupCanRetryMutation(result, captured)
		&& captured == _accountId) {
		_startup.readyUnconfigured();
		unlockSetup();
		setStatus(result.error);
		updateTexts();
		return;
	}
	Delta::ReleaseProfileSetup(_setupLease);
	_setupLease.reset();
	setCreatingProfile(false);
	if (captured != 0 && captured != _accountId) {
		setStatus(Delta::kProfileSetupUnavailable);
		return;
	}
	setStatus(result.error.isEmpty()
		? Delta::kProfileSetupUnreadable
		: result.error);
}

void DeltaOnboarding::createProfile() {
	if (_startup.primaryAction() == Delta::StartupState::Action::OpenKnown) {
		openShell(_startup.knownAccountId);
		return;
	}
	if (setupMutationBlocked()) {
		setStatus(Delta::kProfileSetupBusy);
		return;
	}
	const auto name = _name->getLastText().trimmed();
	if (name.isEmpty()) {
		_name->showError();
		return;
	}
	lockSetup();
	setStatus(u"Creating profile…"_q);
	ensureFreshAccount([=](QJsonObject ready) {
		if (HasError(ready) || _accountId == 0) {
			unlockSetup();
			setStatus(HasError(ready)
				? RpcError(ready)
				: u"No profile yet."_q);
			return;
		}
		const auto captured = _accountId;
		const auto avatar = saveAvatar();
		const auto team = _teamProfile;
		_setupLease = Delta::TryAcquireProfileSetup(
			_setupTable,
			captured,
			Delta::ProfileSetupKind::Create);
		if (!_setupLease) {
			unlockSetup();
			setStatus(Delta::kProfileSetupBusy);
			return;
		}
		const auto lease = std::move(_setupLease);
		const auto request = mutationRequest();
		const auto weak = base::make_weak(this);
		const auto fail = [=](QString error) {
			Delta::ReleaseProfileSetup(lease);
			const auto self = weak.get();
			if (!self) {
				return;
			}
			self->unlockSetup();
			self->setStatus(error);
		};
		const auto finishCreate = [=] {
			Delta::CommitProfileSetup(
				request,
				lease,
				u"init_transports"_q,
				{ captured, QJsonValue() },
				[=](Delta::ProfileSetupResult result) {
					const auto self = weak.get();
					if (!self) {
						return;
					}
					self->applySetupResult(result, u"Profile created."_q);
				});
		};
		const auto markTeam = [=] {
			if (!team) {
				finishCreate();
				return;
			}
			request(
				u"set_config"_q,
				{ captured, u"team_profile"_q, u"1"_q },
				[=](QJsonObject reply) {
					if (HasError(reply)) {
						fail(RpcError(reply));
						return;
					}
					finishCreate();
				});
		};
		const auto setAvatar = [=] {
			if (!avatar) {
				markTeam();
				return;
			}
			request(
				u"set_config"_q,
				{ captured, u"selfavatar"_q, avatar->path() },
				[avatar = avatar, fail = fail, markTeam = markTeam](
						QJsonObject reply) {
					if (HasError(reply)) {
						fail(RpcError(reply));
						return;
					}
					markTeam();
				});
		};
		request(
			u"set_config"_q,
			{ captured, u"displayname"_q, name },
			[=](QJsonObject reply) {
				if (HasError(reply)) {
					fail(RpcError(reply));
					return;
				}
				setAvatar();
			});
	});
}
