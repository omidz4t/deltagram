/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/welcome.h"

#include "delta/delta_boxes.h"
#include "delta/delta_bridge.h"
#include "boxes/abstract_box.h"
#include "core/click_handler_types.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
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

const auto kAccountsDefault = u"/.local/share/delta-tel/accounts"_q;
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
	}
	createControls();
	applyPage();
	if (!_live) {
		startBackend();
	}
}

void DeltaOnboarding::setFinishedCallback(Fn<void()> callback) {
	_finished = std::move(callback);
}

void DeltaOnboarding::closeLive() {
	const auto done = _finished;
	if (done) {
		crl::on_main(this, done);
	}
}

DeltaOnboarding::~DeltaOnboarding() = default;

void DeltaOnboarding::createControls() {
	_next.create(this, rpl::single(QString()), st::introNextButton);
	_next->setClickedCallback([=] {
		if (_page == Page::Welcome) {
			showProfile();
		} else {
			createProfile();
		}
	});

	_alternative.create(this, u"I Already Have a Profile"_q, st::introLink);
	_alternative->setClickedCallback([=] { showAlternative(); });

	_privacy.create(this, u"Privacy Policy"_q, st::introLink);
	_privacy->setClickedCallback([=] {
		UrlClickHandler::Open(kPrivacyUrl);
	});

	_back.create(this, st::introBackButton);
	_back->setClickedCallback([=] {
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
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
		+ u"/delta-tel/accounts_path"_q;
}

QString ResolveAccountsPath() {
	const auto args = QCoreApplication::arguments();
	for (auto i = 0; i + 1 < args.size(); ++i) {
		if (args[i] == u"-delta-accounts"_q
			|| args[i] == u"--delta-accounts"_q) {
			return args[i + 1];
		}
	}
	if (const auto env = qEnvironmentVariable("DELTA_TEL_ACCOUNTS");
		!env.isEmpty()) {
		return env;
	}
	auto file = QFile(AccountsPathFile());
	if (file.open(QIODevice::ReadOnly)) {
		const auto saved = QString::fromUtf8(file.readAll()).trimmed();
		if (!saved.isEmpty()) {
			return saved;
		}
	}
	return qEnvironmentVariable(
		"DC_ACCOUNTS_PATH",
		QDir::homePath() + kAccountsDefault);
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
	const auto program = qEnvironmentVariable("DELTA_TEL_RPC");
	const auto accounts = ResolveAccountsPath();
	QDir().mkpath(accounts);
	if (program.isEmpty()) {
		setStatus(u"DELTA_TEL_RPC is not set."_q);
		return;
	}
	_rpc = new DeltaRpc(this);
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
		_next->setText(rpl::single(u"Create New Profile"_q));
	} else {
		title = _teamProfile
			? u"Create Team Profile"_q
			: u"Your Profile"_q;
		description = _teamProfile
			? u"A \"Team Profile\" is managed collectively by a group of people or an organization. Team Profiles are experimental and subject to change."_q
			: u"Enter your name and add a profile picture."_q;
		_next->setText(rpl::single(u"Agree & Create Profile"_q));
		_name->setPlaceholder(rpl::single(_teamProfile
			? u"Team Name"_q
			: u"Your Name"_q));
	}
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
		_title->moveToLeft(left, top + st::introTitleTop);
		_description->resizeToWidth(st::introDescription.minWidth);
		_description->moveToLeft(left, top + st::introDescriptionTop);
		const auto photoRight = contentLeft() + st::introNextButton.width;
		_photo->moveToLeft(
			photoRight - _photo->width(),
			top + st::introPhotoTop);
		_name->moveToLeft(contentLeft(), top + st::introStepFieldTop);
		_privacy->moveToLeft(
			(width() - _privacy->width()) / 2,
			_next->y() + _next->height() + _privacy->height());
		_back->moveToLeft(0, 0);
		_menuButton->moveToRight(
			st::introSettingsSkip,
			st::introSettingsSkip);
	}
	_status->resizeToWidth(width());
	_status->moveToLeft(0, top + st::introErrorTop);
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
	if (_live) {
		_handedOver = true;
		Delta::SwitchProfile(accountId, crl::guard(this, [=](QString error) {
			if (!error.isEmpty()) {
				_handedOver = false;
				setStatus(error);
				return;
			}
			closeLive();
		}));
		return;
	}
	_handedOver = true;
	call(u"start_io"_q, {accountId}, [=](QJsonObject reply) {
		if (HasError(reply)) {
			_handedOver = false;
			setStatus(RpcError(reply));
			return;
		}
		call(u"get_config"_q, {accountId, u"displayname"_q}, [=](QJsonObject config) {
			const auto name = config.value(u"result"_q).toString();
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
}

void DeltaOnboarding::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
		if (_live && _page == Page::Welcome) {
			closeLive();
		} else if (_page == Page::Profile) {
			showWelcome();
		}
	} else if (e->key() == Qt::Key_Enter
		|| e->key() == Qt::Key_Return
		|| e->key() == Qt::Key_Space) {
		if (_page == Page::Welcome) {
			showProfile();
		}
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
		setStatus(u"Chatmail core is not running."_q);
		return;
	}
	_rpc->call(method, params, std::move(done));
}

void DeltaOnboarding::showWelcome() {
	_teamProfile = false;
	_page = Page::Welcome;
	setStatus(QString());
	applyPage();
}

void DeltaOnboarding::showProfile() {
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

void DeltaOnboarding::showMenu() {
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
		const auto event = envelope.value(u"event"_q).toObject();
		const auto kind = event.value(u"kind"_q).toString();
		if (kind == u"ConfigureProgress"_q || kind == u"ImexProgress"_q) {
			const auto comment = event.value(u"comment"_q).toString();
			const auto progress = event.value(u"progress"_q).toInt();
			if (!comment.isEmpty()) {
				setStatus(comment);
			} else if (progress > 0) {
				setStatus(u"%1%"_q.arg(progress / 10));
			}
		}
		pumpEvents();
	}));
}

void DeltaOnboarding::bootstrap() {
	call(u"get_all_account_ids"_q, {}, [=](QJsonObject reply) {
		if (HasError(reply)) {
			const auto message = RpcError(reply);
			if (message.contains(u"not running"_q)) {
				QTimer::singleShot(400, this, [=] { bootstrap(); });
				return;
			}
			setStatus(message);
			return;
		}
		const auto ids = reply.value(u"result"_q).toArray();
		if (ids.isEmpty()) {
			_accountId = 0;
			_accountReady = false;
			_anyConfigured = false;
			updateTexts();
			updateControlsGeometry();
			return;
		}
		call(u"get_selected_account_id"_q, {}, [=](QJsonObject selected) {
			const auto selectedId = (!HasError(selected)
				&& !selected.value(u"result"_q).isNull())
				? selected.value(u"result"_q).toInt()
				: 0;
			auto remaining = std::make_shared<int>(ids.size());
			auto configured = std::make_shared<QList<int>>();
			auto unconfigured = std::make_shared<int>(0);
			for (const auto &idValue : ids) {
				const auto id = idValue.toInt();
				call(u"is_configured"_q, {id}, [=](QJsonObject configuredReply) {
					if (configuredReply.value(u"result"_q).toBool()) {
						configured->push_back(id);
						_anyConfigured = true;
					} else if (*unconfigured == 0) {
						*unconfigured = id;
					}
					if (--(*remaining) != 0) {
						return;
					}
					if (!configured->isEmpty()) {
						auto openId = configured->contains(selectedId)
							? selectedId
							: configured->front();
						updateTexts();
						updateControlsGeometry();
						call(u"select_account"_q, {openId}, [=](QJsonObject) {
							openShell(openId);
						});
						return;
					}
					if (*unconfigured != 0) {
						_accountId = *unconfigured;
						_accountReady = true;
					}
					updateTexts();
					updateControlsGeometry();
					if (qEnvironmentVariableIsSet("DELTA_TEL_FORCE_OPEN")
						&& _accountId != 0) {
						openShell(_accountId);
					}
				});
			}
		});
	});
}

void DeltaOnboarding::ensureFreshAccount(DeltaRpc::Callback done) {
	if (_accountReady && _accountId != 0) {
		if (done) {
			done(QJsonObject());
		}
		return;
	}
	call(u"get_all_account_ids"_q, {}, [=](QJsonObject reply) {
		if (HasError(reply)) {
			if (done) {
				done(reply);
			}
			return;
		}
		const auto ids = reply.value(u"result"_q).toArray();
		if (ids.isEmpty()) {
			call(u"add_account"_q, {}, [=](QJsonObject created) {
				if (!HasError(created)) {
					_accountId = created.value(u"result"_q).toInt();
					_accountReady = true;
				}
				if (done) {
					done(created);
				}
			});
			return;
		}
		auto remaining = std::make_shared<int>(ids.size());
		auto unconfigured = std::make_shared<int>(0);
		for (const auto &idValue : ids) {
			const auto id = idValue.toInt();
			call(u"is_configured"_q, {id}, [=](QJsonObject configured) {
				if (!configured.value(u"result"_q).toBool() && *unconfigured == 0) {
					*unconfigured = id;
				}
				if (--(*remaining) != 0) {
					return;
				}
				if (*unconfigured != 0) {
					_accountId = *unconfigured;
					_accountReady = true;
					if (done) {
						done(QJsonObject());
					}
					return;
				}
				call(u"add_account"_q, {}, [=](QJsonObject created) {
					if (!HasError(created)) {
						_accountId = created.value(u"result"_q).toInt();
						_accountReady = true;
					}
					if (done) {
						done(created);
					}
				});
			});
		}
	});
}

void DeltaOnboarding::finishSetup(const QString &text) {
	_anyConfigured = true;
	_accountReady = false;
	setStatus(text);
	openShell(_accountId);
}

void DeltaOnboarding::showAlternative() {
	ensureFreshAccount([=](QJsonObject reply) {
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
	AskText(
		u"Add as Second Device"_q,
		u"Setup code from the other device"_q,
		u"Continue"_q,
		crl::guard(this, [=](QString code) {
			if (_accountId == 0) {
				return;
			}
			setStatus(u"Copying the profile from the other device…"_q);
			call(u"get_backup"_q, {_accountId, code}, [=](QJsonObject reply) {
				if (HasError(reply)) {
					setStatus(RpcError(reply));
					return;
				}
				finishSetup(u"Profile copied."_q);
			});
		}));
}

void DeltaOnboarding::chooseBackup() {
	FileDialog::GetOpenPath(
		this,
		u"Restore from Backup"_q,
		u"Backup (*.tar *.bak)"_q,
		crl::guard(this, [=](FileDialog::OpenResult &&result) {
			if (result.paths.isEmpty() || _accountId == 0) {
				return;
			}
			setStatus(u"Restoring profile…"_q);
			call(
				u"import_backup"_q,
				{_accountId, result.paths.front(), QJsonValue()},
				[=](QJsonObject reply) {
					if (HasError(reply)) {
						setStatus(RpcError(reply));
						return;
					}
					finishSetup(u"Profile restored."_q);
				});
		}));
}

void DeltaOnboarding::showOtherServer() {
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
	AskText(
		u"Enter Invitation Code"_q,
		u"Invitation or server code"_q,
		u"Continue"_q,
		crl::guard(this, [=](QString code) {
			ensureFreshAccount([=](QJsonObject ready) {
				if (HasError(ready)) {
					setStatus(RpcError(ready));
					return;
				}
				setStatus(u"Creating profile…"_q);
				call(u"init_transports"_q, {_accountId, code}, [=](QJsonObject reply) {
					if (HasError(reply)) {
						setStatus(RpcError(reply));
						return;
					}
					finishSetup(u"Profile created."_q);
				});
			});
		}));
}

void DeltaOnboarding::showClassicLogin() {
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
			if (!strong) {
				return;
			}
			strong->ensureFreshAccount([=](QJsonObject ready) {
				const auto self = weak.get();
				if (!self) {
					return;
				}
				if (HasError(ready)) {
					self->setStatus(RpcError(ready));
					return;
				}
				const auto name = self->_name->getLastText().trimmed();
				const auto start = [=] {
					const auto self = weak.get();
					if (!self) {
						return;
					}
					QJsonObject param;
					param.insert(QStringLiteral("addr"), address);
					param.insert(QStringLiteral("password"), secret);
					self->setStatus(u"Contacting the mail server…"_q);
					self->call(
						u"add_or_update_transport"_q,
						{self->_accountId, param},
						[=](QJsonObject reply) {
							const auto self = weak.get();
							if (!self) {
								return;
							}
							if (HasError(reply)) {
								self->setStatus(RpcError(reply));
								return;
							}
							self->finishSetup(u"Profile created."_q);
						});
				};
				if (name.isEmpty()) {
					start();
					return;
				}
				self->call(
					u"set_config"_q,
					{self->_accountId, u"displayname"_q, name},
					[=](QJsonObject reply) {
						const auto self = weak.get();
						if (!self) {
							return;
						}
						if (HasError(reply)) {
							self->setStatus(RpcError(reply));
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

QString DeltaOnboarding::saveAvatar() const {
	if (_avatar.isNull()) {
		return QString();
	}
	const auto path = QDir::tempPath() + u"/delta-tel-avatar.png"_q;
	return _avatar.save(path, "PNG") ? path : QString();
}

void DeltaOnboarding::createProfile() {
	const auto name = _name->getLastText().trimmed();
	if (name.isEmpty()) {
		_name->showError();
		return;
	}
	setStatus(u"Creating profile…"_q);
	ensureFreshAccount([=](QJsonObject ready) {
		if (HasError(ready) || _accountId == 0) {
			setStatus(HasError(ready)
				? RpcError(ready)
				: u"No profile yet."_q);
			return;
		}
		const auto initTransports = [=] {
			call(u"init_transports"_q, {_accountId, QJsonValue()}, [=](QJsonObject reply) {
				if (HasError(reply)) {
					setStatus(RpcError(reply));
					return;
				}
				finishSetup(u"Profile created."_q);
			});
		};
		const auto markTeam = [=] {
			if (!_teamProfile) {
				initTransports();
				return;
			}
			call(u"set_config"_q, {_accountId, u"team_profile"_q, u"1"_q}, [=](QJsonObject reply) {
				if (HasError(reply)) {
					setStatus(RpcError(reply));
					return;
				}
				initTransports();
			});
		};
		const auto setAvatar = [=] {
			const auto path = saveAvatar();
			if (path.isEmpty()) {
				markTeam();
				return;
			}
			call(u"set_config"_q, {_accountId, u"selfavatar"_q, path}, [=](QJsonObject reply) {
				if (HasError(reply)) {
					setStatus(RpcError(reply));
					return;
				}
				markTeam();
			});
		};
		call(u"set_config"_q, {_accountId, u"displayname"_q, name}, [=](QJsonObject reply) {
			if (HasError(reply)) {
				setStatus(RpcError(reply));
				return;
			}
			setAvatar();
		});
	});
}
