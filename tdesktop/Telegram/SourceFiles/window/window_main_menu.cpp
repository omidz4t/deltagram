/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/window_main_menu.h"

#include "apiwrap.h"
#include "delta/delta_boxes.h"
#include "delta/delta_bridge.h"
#include "ui/image/image_prepare.h"
#include "data/data_peer_id.h"
#include "base/event_filter.h"
#include "base/qt_signal_producer.h"
#include "boxes/about_box.h"
#include "boxes/add_contact_box.h"
#include "core/update_channel.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/premium_preview_box.h"
#include "calls/group/calls_group_common.h"
#include "calls/calls_box_controller.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "data/data_changes.h"
#include "data/data_document_media.h"
#include "data/data_folder.h"
#include "data/data_group_call.h"
#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_user.h"
#include "info/info_memento.h"
#include "info/profile/info_profile_badge.h"
#include "settings/settings_common.h"
#include "info/profile/info_profile_emoji_status_panel.h"
#include "info/profile/info_profile_icon.h"
#include "info/stories/info_stories_widget.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/mtproto_config.h"
#include "settings/sections/settings_advanced.h"
#include "settings/sections/settings_calls.h"
#include "settings/sections/settings_information.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "support/support_templates.h"
#include "ui/boxes/confirm_box.h"
#include "ui/chat/chat_theme.h"
#include "ui/controls/swipe_handler.h"
#include "ui/controls/userpic_button.h"
#include "ui/effects/snowflakes.h"
#include "ui/effects/toggle_arrow.h"
#include "ui/abstract_button.h"
#include "ui/painter.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/unread_badge_paint.h"
#include "ui/vertical_list.h"
#include "ui/widgets/menu/menu_add_action_callback_factory.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/slide_wrap.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_main_menu_helpers.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h" // popupMenuExpandedSeparator
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "styles/style_window_main_menu.h"

#include <QtGui/QPainterPath>
#include <QtGui/QWindow>
#include <QFileInfo>
#include <QtGui/QScreen>

#include <QtGui/QGuiApplication>
#include <QtGui/QClipboard>

namespace Window {
namespace {

constexpr auto kPlayStatusLimit = 2;
constexpr auto kProfilePhoto = 36;

[[nodiscard]] QImage RoundPhoto(const QString &path) {
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		return {};
	}
	auto image = Images::Read({
		.path = path,
		.maxSize = QSize(
			kProfilePhoto * style::DevicePixelRatio(),
			kProfilePhoto * style::DevicePixelRatio()),
	}).image;
	if (image.isNull()) {
		return {};
	}
	image = image.scaled(
		kProfilePhoto * style::DevicePixelRatio(),
		kProfilePhoto * style::DevicePixelRatio(),
		Qt::KeepAspectRatioByExpanding,
		Qt::SmoothTransformation);
	auto out = QImage(
		image.size(),
		QImage::Format_ARGB32_Premultiplied);
	out.fill(Qt::transparent);
	auto p = QPainter(&out);
	p.setRenderHint(QPainter::Antialiasing);
	auto clip = QPainterPath();
	clip.addEllipse(QRectF(QPointF(0, 0), QSizeF(out.size())));
	p.setClipPath(clip);
	p.drawImage(
		QPoint(
			(out.width() - image.width()) / 2,
			(out.height() - image.height()) / 2),
		image);
	out.setDevicePixelRatio(style::DevicePixelRatio());
	return out;
}

class ProfileRow final : public Ui::AbstractButton {
public:
	ProfileRow(
		QWidget *parent,
		const QString &title,
		const QString &subtitle,
		QImage photo,
		QColor color,
		QString initial)
	: AbstractButton(parent)
	, _title(title)
	, _subtitle(subtitle)
	, _photo(std::move(photo))
	, _color(color)
	, _initial(initial)
	, _height(_subtitle.isEmpty() ? 48 : 62) {
		setPointerCursor(true);
		setContextMenuPolicy(Qt::DefaultContextMenu);
	}

	void setMenuActions(Fn<void()> moveToTop, Fn<void()> remove) {
		_moveToTop = std::move(moveToTop);
		_remove = std::move(remove);
	}

protected:
	void contextMenuEvent(QContextMenuEvent *e) override {
		_popup = base::make_unique_q<Ui::PopupMenu>(this);
		_popup->addAction(u"Move to top"_q, _moveToTop);
		_popup->addAction(u"Delete profile"_q, _remove);
		_popup->popup(e->globalPos());
		e->accept();
	}

	void paintEvent(QPaintEvent *) override {
		auto p = QPainter(this);
		p.setRenderHint(QPainter::Antialiasing);
		const auto x = st::mainMenuButton.iconLeft;
		const auto y = (height() - kProfilePhoto) / 2;
		if (!_photo.isNull()) {
			p.drawImage(QRect(x, y, kProfilePhoto, kProfilePhoto), _photo);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(_color);
			p.drawEllipse(x, y, kProfilePhoto, kProfilePhoto);
			p.setPen(Qt::white);
			p.setFont(st::semiboldFont);
			p.drawText(
				QRect(x, y, kProfilePhoto, kProfilePhoto),
				Qt::AlignCenter,
				_initial);
		}
		const auto textLeft = x + kProfilePhoto + 12;
		const auto textWidth = width() - textLeft - 12;
		p.setPen(st::windowFg);
		p.setFont(st::semiboldFont);
		if (_subtitle.isEmpty()) {
			p.drawText(
				QRect(textLeft, 0, textWidth, height()),
				Qt::AlignVCenter | Qt::AlignLeft,
				_title);
		} else {
			p.drawText(
				QRect(textLeft, 10, textWidth, 22),
				Qt::AlignLeft | Qt::AlignVCenter,
				_title);
			p.setPen(st::windowSubTextFg);
			p.setFont(st::normalFont);
			p.drawText(
				QRect(textLeft, 32, textWidth, 18),
				Qt::AlignLeft | Qt::AlignVCenter,
				_subtitle);
		}
	}

	int resizeGetHeight(int) override {
		return _height;
	}

private:
	QString _title;
	QString _subtitle;
	QImage _photo;
	QColor _color;
	QString _initial;
	int _height = 0;
	Fn<void()> _moveToTop;
	Fn<void()> _remove;
	base::unique_qptr<Ui::PopupMenu> _popup;
};

[[nodiscard]] bool CanCheckSpecialEvent() {
	static const auto result = [] {
		const auto now = QDate::currentDate();
		return (now.month() == 12) || (now.month() == 1 && now.day() == 1);
	}();
	return result;
}

[[nodiscard]] bool CheckSpecialEvent() {
	const auto now = QDate::currentDate();
	return (now.month() == 12 && now.day() >= 24)
		|| (now.month() == 1 && now.day() == 1);
}

[[nodiscard]] rpl::producer<TextWithEntities> SetStatusLabel(
		not_null<Main::Session*> session) {
	const auto self = session->user();
	return session->changes().peerFlagsValue(
		self,
		Data::PeerUpdate::Flag::EmojiStatus
	) | rpl::map([=] {
		return !!self->emojiStatusId();
	}) | rpl::distinct_until_changed() | rpl::map([](bool has) {
		const auto makeLink = [](const QString &text) {
			return tr::link(text);
		};
		return (has
			? tr::lng_menu_change_status
			: tr::lng_menu_set_status)(makeLink);
	}) | rpl::flatten_latest();
}

} // namespace

class MainMenu::ToggleAccountsButton final : public Ui::AbstractButton {
public:
	ToggleAccountsButton(QWidget *parent, not_null<Main::Account*> current);

	[[nodiscard]] int rightSkip() const {
		return _rightSkip.current();
	}
	[[nodiscard]] rpl::producer<int> rightSkipValue() const {
		return _rightSkip.value();
	}

private:
	void paintEvent(QPaintEvent *e) override;
	void paintUnreadBadge(Painter &p);

	void validateUnreadBadge();
	[[nodiscard]] QString computeUnreadBadge() const;

	const not_null<Main::Account*> _current;
	rpl::variable<int> _rightSkip = 0;
	Ui::Animations::Simple _toggledAnimation;
	bool _toggled = false;

	QString _unreadBadge;
	bool _unreadBadgeStale = false;

};

class MainMenu::ResetScaleButton final : public Ui::AbstractButton {
public:
	ResetScaleButton(QWidget *parent);

protected:
	void paintEvent(QPaintEvent *e) override;

	static constexpr auto kText = "100%";

};

MainMenu::ToggleAccountsButton::ToggleAccountsButton(
	QWidget *parent,
	not_null<Main::Account*> current)
: AbstractButton(parent)
, _current(current) {
	rpl::single(rpl::empty) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::on_next([=] {
		_unreadBadgeStale = true;
		if (!_toggled) {
			validateUnreadBadge();
			update();
		}
	}, lifetime());

	auto &settings = Core::App().settings();
	if (Core::App().domain().accounts().size() < 2
		&& settings.mainMenuAccountsShown()) {
		settings.setMainMenuAccountsShown(false);
	}
	settings.mainMenuAccountsShownValue(
	) | rpl::filter([=](bool value) {
		return (_toggled != value);
	}) | rpl::on_next([=](bool value) {
		_toggled = value;
		_toggledAnimation.start(
			[=] { update(); },
			_toggled ? 0. : 1.,
			_toggled ? 1. : 0.,
			st::slideWrapDuration);
		validateUnreadBadge();
	}, lifetime());
	_toggledAnimation.stop();
}

void MainMenu::ToggleAccountsButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	const auto path = Ui::ToggleUpDownArrowPath(
		0. + width() - st::mainMenuTogglePosition.x(),
		0. + height() - st::mainMenuTogglePosition.y(),
		st::mainMenuToggleSize,
		st::mainMenuToggleFourStrokes,
		_toggledAnimation.value(_toggled ? 1. : 0.));

	auto hq = PainterHighQualityEnabler(p);
	p.fillPath(path, st::windowSubTextFg);

	paintUnreadBadge(p);
}

void MainMenu::ToggleAccountsButton::paintUnreadBadge(Painter &p) {
	const auto progress = 1. - _toggledAnimation.value(_toggled ? 1. : 0.);
	if (!progress) {
		return;
	}
	validateUnreadBadge();
	if (_unreadBadge.isEmpty()) {
		return;
	}

	auto st = Settings::Badge::Style();
	const auto right = width()
		- st::mainMenuTogglePosition.x()
		- st::mainMenuToggleSize * 3;
	const auto top = height()
		- st::mainMenuTogglePosition.y()
		- st::mainMenuBadgeSize / 2;
	p.setOpacity(progress);
	Ui::PaintUnreadBadge(p, _unreadBadge, right, top, st);
}

void MainMenu::ToggleAccountsButton::validateUnreadBadge() {
	const auto base = st::mainMenuTogglePosition.x()
		+ 2 * st::mainMenuToggleSize;
	if (_toggled) {
		_rightSkip = base;
		return;
	} else if (!_unreadBadgeStale) {
		return;
	}
	_unreadBadge = computeUnreadBadge();

	auto skip = base;
	if (!_unreadBadge.isEmpty()) {
		const auto st = Settings::Badge::Style();
		skip += 2 * st::mainMenuToggleSize
			+ Ui::CountUnreadBadgeSize(_unreadBadge, st).width();
	}
	_rightSkip = skip;
}

QString MainMenu::ToggleAccountsButton::computeUnreadBadge() const {
	const auto state = OtherAccountsUnreadStateCurrent(_current);
	return state.allMuted
		? QString()
		: (state.count > 0)
		? Lang::FormatCountToShort(state.count).string
		: QString();
}

MainMenu::ResetScaleButton::ResetScaleButton(QWidget *parent)
: AbstractButton(parent) {
	const auto margin = st::mainMenuCloudButton.height
		- st::mainMenuCloudSize;
	const auto textWidth = st::mainMenuResetScaleFont->width(kText);
	const auto innerWidth = st::mainMenuResetScaleLeft
		+ textWidth
		+ st::mainMenuResetScaleRight;
	const auto width = margin + innerWidth;
	resize(width, st::mainMenuCloudButton.height);
}

void MainMenu::ResetScaleButton::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto innerHeight = st::mainMenuCloudSize;
	const auto radius = innerHeight / 2;
	const auto margin = st::mainMenuCloudButton.height
		- st::mainMenuCloudSize;
	const auto textWidth = st::mainMenuResetScaleFont->width(kText);
	const auto innerWidth = st::mainMenuResetScaleLeft
		+ textWidth
		+ st::mainMenuResetScaleRight;
	const auto left = margin / 2;
	const auto top = margin / 2;
	p.setPen(Qt::NoPen);
	p.setBrush(st::mainMenuCloudBg);
	p.drawRoundedRect(left, top, innerWidth, innerHeight, radius, radius);

	st::settingsIconInterfaceScale.paint(
		p,
		left + st::mainMenuResetScaleIconLeft,
		top + ((innerHeight - st::settingsIconInterfaceScale.height()) / 2),
		width(),
		st::mainMenuCloudFg->c);

	p.setFont(st::mainMenuResetScaleFont);
	p.setPen(st::mainMenuCloudFg);
	p.drawText(
		left + st::mainMenuResetScaleLeft,
		top + st::mainMenuResetScaleTop + st::mainMenuResetScaleFont->ascent,
		kText);
}

MainMenu::MainMenu(
	QWidget *parent,
	not_null<SessionController*> controller)
: LayerWidget(parent)
, _controller(controller)
, _userpicButton(
	this,
	_controller->session().user(),
	st::mainMenuUserpic)
, _toggleAccounts(this, &controller->session().account())
, _setEmojiStatus(this, SetStatusLabel(&controller->session()))
, _emojiStatusPanel(std::make_unique<Info::Profile::EmojiStatusPanel>())
, _badge(std::make_unique<Info::Profile::Badge>(
	this,
	st::settingsInfoPeerBadge,
	&controller->session(),
	Info::Profile::BadgeContentForPeer(controller->session().user()),
	_emojiStatusPanel.get(),
	[=] { return controller->isGifPausedAtLeastFor(GifPauseReason::Layer); },
	kPlayStatusLimit,
	Info::Profile::BadgeType::Premium))
, _scroll(this, st::defaultSolidScroll)
, _inner(_scroll->setOwnedWidget(
	object_ptr<Ui::VerticalLayout>(_scroll.data())))
, _topShadowSkip(_inner->add(
	object_ptr<Ui::FixedHeightWidget>(_inner.get(), st::lineWidth)))
, _accounts(_inner->add(object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
	_inner.get(),
	object_ptr<Ui::VerticalLayout>(_inner.get()))))
, _shadow(_inner->add(object_ptr<Ui::SlideWrap<Ui::PlainShadow>>(
	_inner.get(),
	object_ptr<Ui::PlainShadow>(_inner.get()))))
, _menu(_inner->add(
	object_ptr<Ui::VerticalLayout>(_inner.get()),
	{ 0, st::mainMenuSkip, 0, 0 }))
, _footer(_inner->add(object_ptr<Ui::RpWidget>(_inner.get())))
, _telegram(
	Ui::CreateChild<Ui::FlatLabel>(_footer.get(), st::mainMenuTelegramLabel))
, _version(AddVersionLabel(_footer)) {
	setAttribute(Qt::WA_OpaquePaintEvent);

	setupUserpicButton();
	setupAccountsToggle();
	setupSetEmojiStatus();
	setupAccounts();
	setupArchive();
	setupMenu();

	const auto shadow = Ui::CreateChild<Ui::PlainShadow>(this);
	widthValue(
	) | rpl::on_next([=](int width) {
		const auto line = st::lineWidth;
		shadow->setGeometry(0, st::mainMenuCoverHeight - line, width, line);
	}, shadow->lifetime());

	_nightThemeSwitch.setCallback([this] {
		Expects(_nightThemeToggle != nullptr);

		const auto nightMode = Window::Theme::IsNightMode();
		if (_nightThemeToggle->toggled() != nightMode) {
			Window::Theme::ToggleNightMode();
			Window::Theme::KeepApplied();
		}
	});

	_footer->heightValue(
	) | rpl::on_next([=] {
		_telegram->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuTelegramBottom - _telegram->height());
		_version->moveToLeft(st::mainMenuFooterLeft, _footer->height() - st::mainMenuVersionBottom - _version->height());
	}, _footer->lifetime());

	rpl::combine(
		heightValue(),
		_inner->heightValue()
	) | rpl::on_next([=] {
		updateInnerControlsGeometry();
	}, _inner->lifetime());

	parentResized();

	_telegram->setMarkedText(tr::link(
		u"Deltagram"_q,
		u"https://delta.chat"_q));
	_telegram->setLinksTrusted();
	// The canary version is too long for the "Version {version}" form.
	_version->setMarkedText(
		tr::link(
			Core::BuildIsCanary
				? currentVersionShortText()
				: tr::lng_settings_current_version(
					tr::now,
					lt_version,
					currentVersionShortText()),
			1) // Link 1.
		.append(QChar(' '))
		.append(QChar(8211))
		.append(QChar(' '))
		.append(tr::link(tr::lng_menu_about(tr::now), 2))); // Link 2.
	_version->setLink(
		1,
		std::make_shared<UrlClickHandler>(Core::App().changelogLink()));
	_version->setLink(
		2,
		std::make_shared<LambdaClickHandler>([=] {
			controller->show(Box(AboutBox));
		}));

	rpl::combine(
		_toggleAccounts->rightSkipValue(),
		rpl::single(rpl::empty) | rpl::then(_badge->updated())
	) | rpl::on_next([=] {
		moveBadge();
	}, lifetime());
	_badge->setPremiumClickCallback([=] {
		chooseEmojiStatus();
	});

	_controller->session().downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());

	initResetScaleButton();

	if (CanCheckSpecialEvent() && CheckSpecialEvent()) {
		const auto snowLifetime = lifetime().make_state<rpl::lifetime>();
		const auto rebuild = [=] {
			const auto snowRaw = Ui::CreateChild<Ui::RpWidget>(this);
			const auto snow = snowLifetime->make_state<Ui::Snowflakes>(
				[=](const QRect &r) { snowRaw->update(r); });
			snow->setBrush(QColor(230, 230, 230));
			_showFinished.value(
			) | rpl::on_next([=](bool shown) {
				snow->setPaused(!shown);
			}, snowRaw->lifetime());
			snowRaw->paintRequest(
			) | rpl::on_next([=](const QRect &r) {
				auto p = Painter(snowRaw);
				p.fillRect(r, st::mainMenuBg);
				drawName(p);
				snow->paint(p, snowRaw->rect());
			}, snowRaw->lifetime());
			widthValue(
			) | rpl::on_next([=](int width) {
				snowRaw->setGeometry(0, 0, width, st::mainMenuCoverHeight);
			}, snowRaw->lifetime());
			snowRaw->show();
			snowRaw->lower();
			snowRaw->setAttribute(Qt::WA_TransparentForMouseEvents);
			snowLifetime->add([=] { base::unique_qptr{ snowRaw }; });
		};
		Window::Theme::IsNightModeValue(
		) | rpl::on_next([=](bool isNightMode) {
			snowLifetime->destroy();
			if (isNightMode) {
				rebuild();
			}
		}, lifetime());
	}

	setupSwipe();
}

MainMenu::~MainMenu() = default;

void MainMenu::moveBadge() {
	if (!_badge->widget()) {
		return;
	}
	const auto available = width()
		- st::mainMenuCoverNameLeft
		- _toggleAccounts->rightSkip()
		- _badge->widget()->width();
	const auto left = st::mainMenuCoverNameLeft
		+ std::min(_name.maxWidth() + st::semiboldFont->spacew, available);
	_badge->move(
		left,
		st::mainMenuCoverNameTop,
		st::mainMenuCoverNameTop + st::semiboldFont->height);
}

void MainMenu::setupArchive() {
	using namespace Settings;

	const auto controller = _controller;
	const auto folder = [=] {
		return controller->session().data().folderLoaded(Data::Folder::kId);
	};
	const auto showArchive = [=](Qt::KeyboardModifiers modifiers) {
		if (const auto f = folder()) {
			if (modifiers & Qt::ControlModifier) {
				controller->showInNewWindow(Window::SeparateId(
					Window::SeparateType::Archive,
					&controller->session()));
			} else {
				controller->openFolder(f);
			}
			controller->window().hideSettingsAndLayer();
			controller->removeLayerBlackout();
		}
	};
	const auto checkArchive = [=] {
		const auto f = folder();
		return f
			&& (!f->chatsList()->empty() || f->storiesCount() > 0)
			&& controller->session().settings().archiveInMainMenu();
	};

	const auto wrap = _menu->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			_menu,
			object_ptr<Ui::VerticalLayout>(_menu)));
	const auto inner = wrap->entity();
	wrap->toggle(checkArchive(), anim::type::instant);

	const auto button = AddButtonWithIcon(
		inner,
		tr::lng_archived_name(),
		st::mainMenuButton,
		{ &st::menuIconArchiveOpen });
	inner->add(
		object_ptr<Ui::PlainShadow>(inner),
		{ 0, st::mainMenuSkip, 0, st::mainMenuSkip });
	button->setAcceptBoth(true);
	button->clicks(
	) | rpl::on_next([=](Qt::MouseButton which) {
		if (which == Qt::LeftButton) {
			showArchive(button->clickModifiers());
			return;
		} else if (which != Qt::RightButton) {
			return;
		}
		_contextMenu = base::make_unique_q<Ui::PopupMenu>(
			this,
			st::popupMenuExpandedSeparator);
		Window::FillDialogsEntryMenu(
			_controller,
			Dialogs::EntryState{
				.key = folder(),
				.section = Dialogs::EntryState::Section::ContextMenu,
			},
			Ui::Menu::CreateAddActionCallback(_contextMenu));
		_contextMenu->popup(QCursor::pos());
	}, button->lifetime());

	const auto now = folder();
	auto folderValue = now
		? (rpl::single(now) | rpl::type_erased)
		: controller->session().data().chatsListChanges(
		) | rpl::filter([](Data::Folder *folder) {
			return folder && (folder->id() == Data::Folder::kId);
		}) | rpl::take(1);

	using namespace Settings;
	Badge::AddUnread(button, rpl::single(rpl::empty) | rpl::then(std::move(
		folderValue
	) | rpl::map([=](not_null<Data::Folder*> folder) {
		return folder->owner().chatsList(folder)->unreadStateChanges();
	}) | rpl::flatten_latest() | rpl::to_empty) | rpl::map([=] {
		const auto loaded = folder();
		const auto state = loaded
			? loaded->chatListBadgesState()
			: Dialogs::BadgesState();
		return Badge::UnreadBadge{ state.unreadCounter, true };
	}));

	rpl::merge(
		controller->session().data().chatsListChanges(
		) | rpl::filter([](Data::Folder *folder) {
			return folder && (folder->id() == Data::Folder::kId);
		}) | rpl::to_empty,
		controller->session().data().stories().sourcesChanged(
			Data::StorySourcesList::Hidden
		)
	) | rpl::on_next([=] {
		const auto isArchiveVisible = checkArchive();
		wrap->toggle(isArchiveVisible, anim::type::normal);
		if (!isArchiveVisible) {
			_contextMenu = nullptr;
		}
		update();
	}, lifetime());
}

void MainMenu::setupUserpicButton() {
	_userpicButton->setClickedCallback([=] { toggleAccounts(); });
	_userpicButton->show();
}

void MainMenu::toggleAccounts() {
	auto &settings = Core::App().settings();
	const auto shown = !settings.mainMenuAccountsShown();
	settings.setMainMenuAccountsShown(shown);
	Core::App().saveSettingsDelayed();
	if (shown) {
		fillProfiles();
	}
}

void MainMenu::fillProfiles() {
	const auto inner = _accounts->entity();
	if (_profileList) {
		delete _profileList;
		_profileList = nullptr;
	}
	_profileList = inner->add(object_ptr<Ui::VerticalLayout>(inner));
	const auto list = _profileList;
	Delta::ListProfiles(crl::guard(list, [=](QJsonArray accounts) {
		for (const auto &value : accounts) {
			const auto account = value.toObject();
			const auto id = account.value(u"id"_q).toInt();
			const auto configured = account.value(u"kind"_q).toString()
				== u"Configured"_q;
			if (!configured) {
				continue;
			}
			auto name = account.value(u"displayName"_q).toString().trimmed();
			if (name.isEmpty()) {
				name = u"Profile"_q;
			}
			const auto tag = configured
				? account.value(u"privateTag"_q).toString().trimmed()
				: QString();
			auto color = QColor(account.value(u"color"_q).toString());
			if (!color.isValid()) {
				color = QColor(0x5A, 0x8F, 0xC7);
			}
			const auto initial = name.isEmpty()
				? QString(u"?"_q)
				: name.left(1).toUpper();
			const auto row = list->add(object_ptr<ProfileRow>(
				list,
				name,
				tag,
				configured
					? RoundPhoto(account.value(u"profileImage"_q).toString())
					: QImage(),
				color,
				initial));
			row->setClickedCallback([=] {
				const auto controller = _controller;
				closeLayer();
				Delta::SwitchProfile(id, [](QString) {});
			});
			row->setMenuActions(
				[=] {
					const auto controller = _controller;
					const auto menu = base::make_weak(this);
					Delta::MoveProfileToTop(id, crl::guard(controller, [=](QString error) {
						if (!error.isEmpty()) {
							controller->show(Ui::MakeInformBox(error));
						} else if (menu) {
							menu->fillProfiles();
						}
					}));
				},
				[=] {
					const auto controller = _controller;
					const auto menu = base::make_weak(this);
					controller->show(Ui::MakeConfirmBox({
						.text = u"Delete profile %1?"_q.arg(name),
						.confirmed = crl::guard(controller, [=](Fn<void()> close) {
							close();
							Delta::RemoveProfile(id, crl::guard(controller, [=](QString error) {
								if (!error.isEmpty()) {
									controller->show(Ui::MakeInformBox(error));
									return;
								}
								if (menu) {
									menu->fillProfiles();
								}
							}));
						}),
						.confirmText = u"Delete"_q,
					}));
				});
		}
		list->add(Settings::CreateButtonWithIcon(
			list,
			rpl::single(u"Add profile"_q),
			st::mainMenuButton,
			{ &st::menuIconAdd }
		))->setClickedCallback([=] {
			const auto controller = _controller;
			closeLayer();
			controller->widget()->showDeltaAddProfile();
		});
		inner->resizeToWidth(inner->width());
	}));
}

void MainMenu::setupAccounts() {
	const auto inner = _accounts->entity();

	inner->add(object_ptr<Ui::FixedHeightWidget>(inner, st::mainMenuSkip));

	_accounts->toggleOn(Core::App().settings().mainMenuAccountsShownValue());
	_accounts->finishAnimating();
	if (Core::App().settings().mainMenuAccountsShown()) {
		fillProfiles();
	}

	_shadow->setDuration(0)->toggleOn(_accounts->shownValue());
}

void MainMenu::setupAccountsToggle() {
	_toggleAccounts->show();
	_toggleAccounts->setAcceptBoth();
	_toggleAccounts->addClickHandler([=](Qt::MouseButton button) {
		if (button == Qt::LeftButton) {
			toggleAccounts();
		}
	});
}

void MainMenu::setupSetEmojiStatus() {
	_setEmojiStatus->hide();
}

void MainMenu::parentResized() {
	resize(st::mainMenuWidth, parentWidget()->height());
}

void MainMenu::showFinished() {
	_showFinished = true;

	_controller->checkHighlightControl(
		u"main-menu/emoji-status"_q,
		_setEmojiStatus,
		Settings::SubsectionTitleHighlight());
	_controller->checkHighlightControl(
		u"main-menu/night-mode"_q,
		_nightThemeToggle);
}

void MainMenu::setupMenu() {
	using namespace Settings;

	const auto controller = _controller;
	const auto addAction = [&](
			rpl::producer<QString> text,
			IconDescriptor &&descriptor) {
		return AddButtonWithIcon(
			_menu,
			std::move(text),
			st::mainMenuButton,
			std::move(descriptor));
	};
	addAction(
		rpl::single(u"Contacts"_q),
		{ &st::menuIconGroups }
	)->setClickedCallback([=] {
		controller->hideLayer();
		Delta::ShowContacts(&controller->session(), crl::guard(controller, [=](int chatId) {
			if (chatId > 0) {
				controller->showPeerHistory(peerFromUser(UserId(chatId)));
			}
		}));
	});
	if (!_controller->session().supportMode()) {
		addAction(
			rpl::single(u"New Contact"_q),
			{ &st::menuIconQrCode }
		)->setClickedCallback([=] {
			controller->showAddContact();
		});
		addAction(
			rpl::single(u"New Group"_q),
			{ &st::menuIconGroups }
		)->setClickedCallback([=] {
			controller->hideLayer();
			controller->show(Box<GroupInfoBox>(controller, GroupInfoBox::Type::Group));
		});
		addAction(
			rpl::single(u"New Channel"_q),
			{ &st::menuIconChannel }
		)->setClickedCallback([=] {
			controller->hideLayer();
			controller->show(Box<GroupInfoBox>(controller, GroupInfoBox::Type::Channel));
		});
		addAction(
			tr::lng_saved_messages(),
			{ &st::menuIconSavedMessages }
		)->setClickedCallback([=] {
			controller->showPeerHistory(controller->session().user());
		});
	} else {
		addAction(
			rpl::single(u"New Contact"_q),
			{ &st::menuIconQrCode }
		)->setClickedCallback([=] {
			controller->showAddContact();
		});
		addAction(
			rpl::single(u"Fix chats order"_q),
			{ &st::menuIconPin }
		)->toggleOn(rpl::single(
			_controller->session().settings().supportFixChatsOrder()
		))->toggledChanges(
		) | rpl::on_next([=](bool fix) {
			_controller->session().settings().setSupportFixChatsOrder(fix);
			_controller->session().saveSettings();
		}, _menu->lifetime());
		addAction(
			rpl::single(u"Reload templates"_q),
			{ &st::menuIconRestore }
		)->setClickedCallback([=] {
			_controller->session().supportTemplates().reload();
		});
	}
	addAction(
		tr::lng_menu_settings(),
		{ &st::menuIconSettings }
	)->setClickedCallback([=] {
		controller->showSettings();
	});

	_nightThemeToggle = addAction(
		tr::lng_menu_night_mode(),
		{ &st::menuIconNightMode }
	)->toggleOn(_nightThemeSwitches.events_starting_with(
		Window::Theme::IsNightMode()
	));
	_nightThemeToggle->toggledChanges(
	) | rpl::filter([=](bool night) {
		return (night != Window::Theme::IsNightMode());
	}) | rpl::on_next([=](bool night) {
		if (Window::Theme::Background()->editingTheme()) {
			_nightThemeSwitches.fire(!night);
			controller->show(Ui::MakeInformBox(
				tr::lng_theme_editor_cant_change_theme()));
			return;
		}
		const auto weak = base::make_weak(this);
		const auto toggle = [=] {
			if (!weak) {
				Window::Theme::ToggleNightMode();
				Window::Theme::KeepApplied();
			} else {
				_nightThemeSwitch.callOnce(st::mainMenu.itemToggle.duration);
			}
		};
		Window::Theme::ToggleNightModeWithConfirmation(
			&_controller->window(),
			toggle);
	}, _nightThemeToggle->lifetime());

	Core::App().settings().systemDarkModeValue(
	) | rpl::on_next([=](std::optional<bool> darkMode) {
		const auto darkModeEnabled
			= Core::App().settings().systemDarkModeEnabled();
		if (darkModeEnabled && darkMode.has_value()) {
			_nightThemeSwitches.fire_copy(*darkMode);
		}
	}, _nightThemeToggle->lifetime());
}

void MainMenu::resizeEvent(QResizeEvent *e) {
	_inner->resizeToWidth(width());
	updateControlsGeometry();
}

void MainMenu::updateControlsGeometry() {
	_userpicButton->moveToLeft(
		st::mainMenuUserpicLeft,
		st::mainMenuUserpicTop);
	if (_resetScaleButton) {
		_resetScaleButton->moveToRight(0, 0);
	}
	_setEmojiStatus->moveToLeft(
		st::mainMenuCoverStatusLeft,
		st::mainMenuCoverStatusTop,
		width());
	_toggleAccounts->setGeometry(
		0,
		st::mainMenuCoverNameTop,
		width(),
		st::mainMenuCoverHeight - st::mainMenuCoverNameTop);
	// Allow cover shadow over the scrolled content.
	const auto top = st::mainMenuCoverHeight - st::lineWidth;
	_scroll->setGeometry(0, top, width(), height() - top);
	updateInnerControlsGeometry();
}

void MainMenu::updateInnerControlsGeometry() {
	const auto contentHeight = _accounts->height()
		+ _shadow->height()
		+ st::mainMenuSkip
		+ _menu->height();
	const auto available = height() - st::mainMenuCoverHeight - contentHeight;
	const auto footerHeight = std::max(
		available,
		st::mainMenuFooterHeightMin);
	if (_footer->height() != footerHeight) {
		_footer->resize(_footer->width(), footerHeight);
	}
}

void MainMenu::chooseEmojiStatus() {
	if (_controller->showFrozenError()) {
		return;
	} else if (const auto widget = _badge->widget()) {
		setupEmojiStatusDismiss();
		_emojiStatusPanel->show(_controller, widget, _badge->sizeTag());
	} else {
		ShowPremiumPreviewBox(_controller, PremiumFeature::EmojiStatus);
	}
}

void MainMenu::setupEmojiStatusDismiss() {
	if (_emojiStatusDismissSetup) {
		return;
	}
	_emojiStatusDismissSetup = true;

	base::install_event_filter(this, parentWidget(), [=](
			not_null<QEvent*> e) {
		if (e->type() != QEvent::MouseButtonPress
			|| !_emojiStatusPanel->shown()) {
			return base::EventFilterResult::Continue;
		}
		_emojiStatusPanel->hideAnimated();
		return base::EventFilterResult::Cancel;
	});
}

bool MainMenu::eventHook(QEvent *event) {
	const auto type = event->type();
	if (type == QEvent::TouchBegin
		|| type == QEvent::TouchUpdate
		|| type == QEvent::TouchEnd
		|| type == QEvent::TouchCancel) {
		QGuiApplication::sendEvent(_inner, event);
	}
	return RpWidget::eventHook(event);
}

void MainMenu::hideEvent(QHideEvent *e) {
	_emojiStatusPanel->hideFast();
}

void MainMenu::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	const auto cover = QRect(0, 0, width(), st::mainMenuCoverHeight);

	p.fillRect(clip, st::mainMenuBg);
	if (cover.intersects(clip)) {
		drawName(p);
	}
}

void MainMenu::drawName(Painter &p) {
	const auto widthText = width()
		- st::mainMenuCoverNameLeft
		- _toggleAccounts->rightSkip();

	const auto user = _controller->session().user();
	if (_nameVersion < user->nameVersion()) {
		_nameVersion = user->nameVersion();
		_name.setText(
			st::semiboldTextStyle,
			user->name(),
			Ui::NameTextOptions());
		moveBadge();
	}
	p.setFont(st::semiboldFont);
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(
		p,
		st::mainMenuCoverNameLeft,
		st::mainMenuCoverNameTop,
		(widthText
			- (_badge->widget()
				? (st::semiboldFont->spacew + _badge->widget()->width())
				: 0)),
		width());
}

void MainMenu::initResetScaleButton() {
	_controller->widget()->screenValue(
	) | rpl::map([](not_null<QScreen*> screen) {
		return rpl::single(
			screen->availableGeometry()
		) | rpl::then(
			base::qt_signal_producer(
				screen.get(),
				&QScreen::availableGeometryChanged
			)
		);
	}) | rpl::flatten_latest(
	) | rpl::map([](QRect available) {
		return (available.width() >= st::windowMinWidth)
			&& (available.height() >= st::windowMinHeight);
	}) | rpl::distinct_until_changed(
	) | rpl::on_next([=](bool good) {
		if (good) {
			_resetScaleButton.destroy();
		} else {
			_resetScaleButton.create(this);
			_resetScaleButton->addClickHandler([] {
				cSetConfigScale(style::kScaleDefault);
				Local::writeSettings();
				Core::Restart();
			});
			_resetScaleButton->show();
			updateControlsGeometry();
		}
	}, lifetime());
}

OthersUnreadState OtherAccountsUnreadStateCurrent(
		not_null<Main::Account*> current) {
	const auto &domain = Core::App().domain();
	auto counter = 0;
	auto allMuted = true;
	for (const auto &[index, account] : domain.accounts()) {
		if (account.get() == current) {
			continue;
		} else if (const auto session = account->maybeSession()) {
			counter += session->data().unreadWithMentionsBadge();
			if (!session->data().unreadWithMentionsBadgeMuted()) {
				allMuted = false;
			}
		}
	}
	return {
		.count = counter,
		.allMuted = allMuted,
	};
}

rpl::producer<OthersUnreadState> OtherAccountsUnreadState(
		not_null<Main::Account*> current) {
	return rpl::single(rpl::empty) | rpl::then(
		Core::App().unreadBadgeChanges()
	) | rpl::map([=] {
		return OtherAccountsUnreadStateCurrent(current);
	});
}

base::EventFilterResult MainMenu::redirectToInnerChecked(not_null<QEvent*> e) {
	if (_insideEventRedirect) {
		return base::EventFilterResult::Continue;
	}
	const auto weak = base::make_weak(this);
	_insideEventRedirect = true;
	QGuiApplication::sendEvent(_inner, e);
	if (weak) {
		_insideEventRedirect = false;
	}
	return base::EventFilterResult::Cancel;
}

void MainMenu::setupSwipe() {
	const auto outer = _controller->widget()->body();
	base::install_event_filter(this, outer, [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::TouchBegin
			|| type == QEvent::TouchUpdate
			|| type == QEvent::TouchEnd
			|| type == QEvent::TouchCancel) {
			return redirectToInnerChecked(e);
		} else if (type == QEvent::Wheel) {
			const auto w = static_cast<QWheelEvent*>(e.get());
			const auto d = Ui::ScrollDeltaF(w);
			if (std::abs(d.x()) > std::abs(d.y())) {
				return redirectToInnerChecked(e);
			}
		}
		return base::EventFilterResult::Continue;
	});
	const auto handles = outer->testAttribute(Qt::WA_AcceptTouchEvents);
	if (!handles) {
		outer->setAttribute(Qt::WA_AcceptTouchEvents);
		lifetime().add([=] {
			outer->setAttribute(Qt::WA_AcceptTouchEvents, false);
		});
	}

	auto update = [=](Ui::Controls::SwipeContextData data) {
		if (data.translation < 0) {
			if (!_swipeBackData.callback) {
				_swipeBackData = Ui::Controls::SetupSwipeBack(
					this,
					[=]() -> std::pair<QColor, QColor> {
						return {
							st::historyForwardChooseBg->c,
							st::historyForwardChooseFg->c,
						};
					});
			}
			_swipeBackData.callback(data);
			return;
		} else if (_swipeBackData.lifetime) {
			_swipeBackData = {};
		}
	};

	auto init = [=](Ui::Controls::SwipeHandlerInitData data) {
		if (data.direction != Qt::LeftToRight) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		if (_emojiStatusPanel && _emojiStatusPanel->hasFocus()) {
			return Ui::Controls::SwipeHandlerFinishData();
		}
		return Ui::Controls::DefaultSwipeBackHandlerFinishData([=] {
			closeLayer();
		});
	};

	Ui::Controls::SetupSwipeHandler({
		.widget = _inner,
		.scroll = _scroll.data(),
		.update = std::move(update),
		.init = std::move(init),
	});
}

} // namespace Window
