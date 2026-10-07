/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/delta_boxes.h"

#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "data/data_thread.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_session.h"
#include "ui/controls/userpic_button.h"
#include "styles/style_info.h"
#include "ui/widgets/popup_menu.h"
#include "ui/boxes/confirm_box.h"
#include "styles/style_menu_icons.h"
#include "ui/empty_userpic.h"
#include "styles/style_dialogs.h"
#include <QtGui/QPainterPath>
#include "delta/delta_bridge.h"
#include "rpl/variable.h"
#include "core/ui_integration.h"
#include "main/main_session.h"
#include "window/window_session_controller.h"
#include "storage/storage_account.h"
#include "ui/chat/attach/attach_bot_downloads.h"
#include "ui/chat/attach/attach_bot_webview.h"
#include "window/themes/window_theme.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "styles/style_layers.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_boxes.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"
#include "settings/settings_common.h"
#include "ui/widgets/box_content_divider.h"
#include "ui/widgets/scroll_area.h"
#include "window/section_memento.h"
#include "window/section_widget.h"
#include <QtGui/QKeyEvent>

#include <QtCore/QDir>
#include <QtCore/QHash>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <memory>
#include <set>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QGuiApplication>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QFileInfo>
#include <QtWidgets/QFileDialog>

#include <algorithm>

namespace Delta {
namespace {

constexpr auto kDefaultAppPickerUrl = "https://apps.testrun.org/";

[[nodiscard]] QString AppPickerUrlFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
		+ u"/delta-tel/app_picker_url"_q;
}

} // namespace

namespace {

class ContactListRow final : public PeerListRow {
public:
	explicit ContactListRow(ContactRow contact)
	: PeerListRow(contact.id)
	, data(std::move(contact))
	, _empty(Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(data.id)), data.name)
	, _photo(data.avatar) {
		if (data.name == data.address) data.name = u"Contact"_q;
		setCustomStatus(QString());
		setSkipPeerBadge(true);
		for (const auto &word : (data.name + ' ' + data.address).toLower().split(' ', Qt::SkipEmptyParts)) {
			_words.emplace(word);
			_letters.emplace(word.front());
		}
	}
	QString generateName() override { return data.name; }
	QString generateShortName() override { return data.name; }
	const base::flat_set<QChar> &generateNameFirstLetters() const override { return _letters; }
	const base::flat_set<QString> &generateNameWords() const override { return _words; }
	PaintRoundImageCallback generatePaintUserpicCallback(bool) override {
		return [=](Painter &p, int x, int y, int outerWidth, int size) {
			if (_photo.isNull()) {
				_empty.paintCircle(p, x, y, outerWidth, size);
				return;
			}
			p.save();
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			auto clip = QPainterPath();
			clip.addEllipse(QRectF(x, y, size, size));
			p.setClipPath(clip);
			p.drawImage(QRect(x, y, size, size), _photo);
			p.restore();
		};
	}
	ContactRow data;
private:
	Ui::EmptyUserpic _empty;
	QImage _photo;
	base::flat_set<QChar> _letters;
	base::flat_set<QString> _words;
};

class ContactListController final : public PeerListController {
public:
	ContactListController(not_null<Main::Session*> session, Fn<void(int)> done,
			bool selectMembers = false, Fn<void(ContactRow)> pick = nullptr)
	: _selectMembers(selectMembers), _session(session), _done(std::move(done)), _pick(std::move(pick)) {
		setStyleOverrides(&st::contactsWithStories);
	}
	Main::Session &session() const override { return *_session; }
	bool trackSelectedList() override { return _selectMembers; }
	std::vector<int> selectedMembers() const {
		auto ids = std::vector<int>();
		for (auto i = 0; i != delegate()->peerListFullRowsCount(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			if (row->checked()) ids.push_back(int(row->id()));
		}
		return ids;
	}
	void showError(QString error) {
		setDescriptionText(error);
		delegate()->peerListRefreshRows();
	}
	bool isForeignRow(PeerListRowId) override { return _selectMembers; }
	bool handleDeselectForeignRow(PeerListRowId id) override {
		if (!_selectMembers) return false;
		if (const auto row = delegate()->peerListFindRow(id)) {
			_selectMembers = false;
			delegate()->peerListSetRowChecked(row, false);
			_selectMembers = true;
		}
		return true;
	}
	void prepare() override {
		delegate()->peerListSetTitle(tr::lng_contacts_header());
		delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
		setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
		setDescriptionText(tr::lng_contacts_loading(tr::now));
		const auto guard = std::weak_ptr<int>(_alive);
		const auto receive = [=](std::vector<ContactRow> contacts, QString error) {
			if (guard.expired()) return;
			if (!error.isEmpty()) {
				setDescriptionText(error);
				return;
			}
			for (auto &contact : contacts) {
				if (contact.id > 9) delegate()->peerListAppendRow(std::make_unique<ContactListRow>(std::move(contact)));
			}
			setDescriptionText(delegate()->peerListFullRowsCount() ? QString() : tr::lng_contacts_not_found(tr::now));
			sort();
		};
		ListContacts(receive);
		ListContacts(receive, true);
	}
	bool toggleSort() { _alphabet = !_alphabet; sort(); return _alphabet; }
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(QWidget *parent, not_null<PeerListRow*> row) override {
		if (_selectMembers || _pick) return nullptr;
		const auto contact = static_cast<ContactListRow*>(row.get())->data;
		auto menu = base::make_unique_q<Ui::PopupMenu>(parent, st::popupMenuWithIcons);
		const auto session = _session;
		const auto guard = std::weak_ptr<int>(_alive);
		menu->addAction(u"Open Chat"_q, [=] {
			if (guard.expired()) return;
			if (const auto found = delegate()->peerListFindRow(contact.id)) rowClicked(found);
		}, &st::menuIconChatBubble);
		menu->addAction(u"Share Contact"_q, [=] {
			const auto holder = std::make_shared<QPointer<PeerListBox>>();
			const auto busy = std::make_shared<bool>(false);
			auto controller = std::make_unique<ChooseRecipientBoxController>(session,
				[=](not_null<Data::Thread*> thread) {
					if (*busy) return;
					*busy = true;
					const auto userId = int(peerToUser(thread->peer()->id).bare);
					ShareContact(contact.id, userId, [=](QString error) {
						*busy = false;
						if (!*holder) return;
						if (!error.isEmpty()) { Ui::show(Ui::MakeInformBox(error)); return; }
						(*holder)->closeBox();
					});
				}, [](not_null<Data::Thread*> thread) { return thread->peer()->isUser(); });
			Ui::show(Box<PeerListBox>(std::move(controller), [=](not_null<PeerListBox*> box) {
				*holder = box.get();
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}), Ui::LayerOption::KeepOther);
		}, &st::menuIconShare);
		menu->addAction(u"Delete Contact"_q, [=] {
			if (guard.expired()) return;
			Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
				box->setTitle(rpl::single(u"Delete contact"_q));
				box->addRow(object_ptr<Ui::FlatLabel>(box, u"Delete %1 from your contacts?"_q.arg(contact.name), st::aboutLabel), st::boxRowPadding);
				const auto check = box->addRow(object_ptr<Ui::Checkbox>(box,
					u"Also delete chat and its messages"_q, false, st::defaultCheckbox), st::boxRowPadding);
				const auto status = box->addRow(object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel), st::boxRowPadding);
				const auto weak = base::make_weak(box);
				const auto busy = std::make_shared<bool>(false);
				box->addButton(tr::lng_box_delete(), [=] {
					if (*busy || guard.expired()) return;
					*busy = true;
					DeleteContact(contact.id, check->checked(), [=](QString error) {
						if (!weak || guard.expired()) return;
						*busy = false;
						if (!error.isEmpty()) { status->setText(error); return; }
						if (const auto found = delegate()->peerListFindRow(contact.id)) delegate()->peerListRemoveRow(found);
						delegate()->peerListRefreshRows();
						box->closeBox();
					});
				});
				box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
			}), Ui::LayerOption::KeepOther);
		}, &st::menuIconDelete);
		return menu;
	}
	void rowClicked(not_null<PeerListRow*> row) override {
		if (_pick) { _pick(static_cast<ContactListRow*>(row.get())->data); return; }
		if (_selectMembers) {
			const auto checked = !row->checked();
			delegate()->peerListSetRowChecked(row, checked);
			return;
		}
		if (_busy) return;
		_busy = true;
		const auto guard = std::weak_ptr<int>(_alive);
		OpenContact(static_cast<ContactListRow*>(row.get())->data, [=](int id, QString error) {
			if (guard.expired()) return;
			_busy = false;
			if (!error.isEmpty()) { setDescriptionText(error); return; }
			// Closing the list destroys this controller and its callback.
			auto done = _done;
			QTimer::singleShot(0, [done = std::move(done), id] { done(id); });
		});
	}
private:
	bool _selectMembers = false;
	void sort() {
		for (auto i = 0; i != delegate()->peerListFullRowsCount(); ++i) {
			const auto row = delegate()->peerListRowAt(i);
			const auto name = static_cast<ContactListRow*>(row.get())->data.name.trimmed();
			row->setSection(!name.isEmpty() && name.front().isLetter()
				? QString(name.front().toUpper()) : u"#"_q);
		}
		delegate()->peerListSetShowSectionHeaders(_alphabet);
		delegate()->peerListSortRows([=](const PeerListRow &a, const PeerListRow &b) {
			const auto &one = static_cast<const ContactListRow&>(a).data;
			const auto &two = static_cast<const ContactListRow&>(b).data;
			if (!_alphabet && one.lastSeen != two.lastSeen) return one.lastSeen > two.lastSeen;
			return one.name.compare(two.name, Qt::CaseInsensitive) < 0;
		});
		delegate()->peerListRefreshRows();
	}
	const not_null<Main::Session*> _session;
	Fn<void(int)> _done;
	Fn<void(ContactRow)> _pick;
	std::shared_ptr<int> _alive = std::make_shared<int>(0);
	bool _alphabet = false;
	bool _busy = false;
};

class ChannelSubscribersController final : public PeerListController {
public:
	ChannelSubscribersController(not_null<Main::Session*> session, int userId)
	: _session(session), _userId(userId) {
		setStyleOverrides(&st::contactsWithStories);
	}
	Main::Session &session() const override { return *_session; }
	bool trackSelectedList() override { return false; }
	void prepare() override {
		delegate()->peerListSetTitle(rpl::single(u"Subscribers"_q));
		delegate()->peerListSetSearchMode(PeerListSearchMode::Enabled);
		setSearchNoResultsText(tr::lng_blocked_list_not_found(tr::now));
		setDescriptionText(u"Loading…"_q);
		const auto guard = std::weak_ptr<int>(_alive);
		ChannelRequest(_userId, u"get_full_chat_by_id"_q, {}, [=](QJsonValue value, QString error) {
			if (guard.expired()) return;
			const auto chat = value.toObject();
			if (!error.isEmpty() || chat.value(u"chatType"_q).toString() != u"OutBroadcast"_q) {
				setDescriptionText(error.isEmpty() ? u"Only the channel owner can manage subscribers."_q : error);
				return;
			}
			for (const auto id : chat.value(u"contactIds"_q).toArray()) {
				if (id.toInt() > 9) _initial.insert(id.toInt());
			}
			_ready = true;
			const auto receive = [=](std::vector<ContactRow> contacts, QString failure) {
				if (guard.expired()) return;
				if (!failure.isEmpty()) { setDescriptionText(failure); return; }
				for (auto &contact : contacts) {
					if (!_initial.contains(contact.id) || delegate()->peerListFindRow(contact.id)) continue;
					auto row = std::make_unique<ContactListRow>(std::move(contact));
					delegate()->peerListAppendRow(std::move(row));
				}
				setDescriptionText(_initial.empty()
					? u"No subscribers yet. Share your channel invitation to add subscribers."_q
					: u"Only you can see the subscriber list."_q);
				delegate()->peerListRefreshRows();
			};
			ListContacts(receive);
			ListContacts(receive, true);
		});
	}
	void rowClicked(not_null<PeerListRow*> row) override {
		if (!_ready || _busy) return;
		const auto guard = std::weak_ptr<int>(_alive);
		OpenContact(static_cast<ContactListRow*>(row.get())->data, [=](int id, QString error) {
			if (guard.expired()) return;
			if (!error.isEmpty()) { setDescriptionText(error); return; }
			if (const auto window = _session->tryResolveWindow()) {
				QTimer::singleShot(0, crl::guard(window, [=] {
					if (guard.expired()) return;
					window->hideLayer();
					window->showPeerHistory(peerFromUser(UserId(id)));
				}));
			}
		});
	}
	base::unique_qptr<Ui::PopupMenu> rowContextMenu(QWidget *parent, not_null<PeerListRow*> row) override {
		auto menu = base::make_unique_q<Ui::PopupMenu>(parent, st::popupMenuWithIcons);
		const auto contact = static_cast<ContactListRow*>(row.get())->data;
		const auto guard = std::weak_ptr<int>(_alive);
		menu->addAction(u"Remove Subscriber"_q, [=] {
			Ui::show(Ui::MakeConfirmBox({
				.text = u"Remove %1 from this channel?"_q.arg(contact.name),
				.confirmed = [=](Fn<void()> close) {
					close();
					if (guard.expired() || _busy) return;
					_busy = true;
					ChannelRequest(_userId, u"remove_contact_from_chat"_q, { contact.id }, [=](QJsonValue, QString error) {
						if (guard.expired()) return;
						_busy = false;
						if (!error.isEmpty()) { setDescriptionText(error); return; }
						_initial.erase(contact.id);
						if (const auto found = delegate()->peerListFindRow(contact.id)) delegate()->peerListRemoveRow(found);
						delegate()->peerListRefreshRows();
					});
				},
			}), Ui::LayerOption::KeepOther);
		}, &st::menuIconDelete);
		return menu;
	}
private:
	const not_null<Main::Session*> _session;
	const int _userId;
	std::set<int> _initial;
	std::shared_ptr<int> _alive = std::make_shared<int>(0);
	bool _ready = false, _busy = false;
};

} // namespace

void ShowChannelSubscribers(not_null<Main::Session*> session, int userId, Fn<void()> done) {
	auto controller = std::make_unique<ChannelSubscribersController>(session, userId);
	Ui::show(Box<PeerListBox>(std::move(controller), [=](not_null<PeerListBox*> box) {
		const auto weak = QPointer<PeerListBox>(box.get());
		const auto finish = [=] {
			if (weak) weak->closeBox();
			if (done) done();
		};
		box->addButton(tr::lng_close(), finish);
		box->addLeftButton(rpl::single(u"Add Subscribers"_q), [=] { ShowChannelInvite(session, userId); });
	}), Ui::LayerOption::KeepOther);
}

namespace {

class ChannelActionButton final : public Ui::RippleButton {
public:
	ChannelActionButton(QWidget *parent, QString text, const style::icon &icon)
	: Ui::RippleButton(parent, st::defaultRippleAnimation)
	, _text(std::move(text))
	, _icon(&icon) {
		show();
	}
	void setContent(QString text, const style::icon &icon) {
		_text = std::move(text);
		_icon = &icon;
		update();
	}
	QString accessibilityName() override { return _text; }
protected:
	void paintEvent(QPaintEvent *e) override {
		Painter p(this);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(rect(), 8, 8);
		paintRipple(p, 0, 0);
		_icon->paint(p, (width() - _icon->width()) / 2, 6, width());
		p.setFont(st::infoProfileTopBarActionButtonFont);
		p.setPen(st::windowFg);
		p.drawText(QRect(0, 32, width(), 20), Qt::AlignCenter, _text);
	}
private:
	QString _text;
	const style::icon *_icon;
};

void FillChannelInfo(
		not_null<Ui::VerticalLayout*> box,
		not_null<Window::SessionController*> controller,
		int userId,
		Fn<void()> close) {
	const auto session = &controller->session();
	const auto status = box->add(object_ptr<Ui::FlatLabel>(box, u"Loading…"_q, st::aboutLabel), st::boxRowPadding);
	const auto weak = QPointer<Ui::VerticalLayout>(box.get());
	ChannelRequest(userId, u"get_full_chat_by_id"_q, {}, [=](QJsonValue value, QString error) {
		if (!weak) return;
		if (!error.isEmpty()) { status->setText(error); return; }
		const auto info = value.toObject();
		const auto nameValue = box->lifetime().make_state<QString>(info.value(u"name"_q).toString());
		const auto descriptionValue = box->lifetime().make_state<QString>();
		const auto owner = info.value(u"chatType"_q).toString() == u"OutBroadcast"_q;
		status->setText(QString());
		const auto header = box->add(object_ptr<Ui::FixedHeightWidget>(box, 190), style::margins());
		const auto peer = session->data().peer(peerFromUser(UserId(userId)));
		const auto photo = Ui::CreateChild<Ui::UserpicButton>(header, peer, st::infoProfileCover.photo);
		const auto nameStyle = header->lifetime().make_state<style::FlatLabel>(st::infoProfileCover.name);
		nameStyle->align = style::al_center;
		const auto statusStyle = header->lifetime().make_state<style::FlatLabel>(st::infoProfileCover.status);
		statusStyle->align = style::al_center;
		const auto name = Ui::CreateChild<Ui::FlatLabel>(header, *nameValue, *nameStyle);
		const auto role = Ui::CreateChild<Ui::FlatLabel>(header, ChannelStatusValue(userId), *statusStyle);
		header->widthValue() | rpl::on_next([=](int width) {
			photo->moveToLeft((width - photo->width()) / 2, 18);
			name->resizeToWidth(std::max(0, width - 40));
			role->resizeToWidth(std::max(0, width - 40));
			name->moveToLeft(20, 112);
			role->moveToLeft(20, 140);
		}, header->lifetime());
		const auto actions = box->add(object_ptr<Ui::FixedHeightWidget>(box, 76), style::margins());
		const auto buttons = actions->lifetime().make_state<std::vector<ChannelActionButton*>>();
		const auto layoutActions = [=] {
			if (buttons->empty()) return;
			const auto gap = 8;
			const auto available = std::max(0, actions->width() - 36);
			const auto width = std::max(0, (available - gap * (int(buttons->size()) - 1)) / int(buttons->size()));
			for (auto i = 0; i != int(buttons->size()); ++i) {
				(*buttons)[i]->setGeometry(18 + i * (width + gap), 0, width, 56);
			}
		};
		actions->widthValue() | rpl::on_next([=] { layoutActions(); }, actions->lifetime());
		const auto addQuickAction = [=](QString text, const style::icon &icon, Fn<void()> action, bool first = false) {
			const auto button = Ui::CreateChild<ChannelActionButton>(actions, std::move(text), icon);
			button->setClickedCallback(std::move(action));
			if (first) buttons->insert(buttons->begin(), button); else buttons->push_back(button);
			layoutActions();
			return button;
		};
		box->add(object_ptr<Ui::BoxContentDivider>(box), style::margins());
		const auto about = box->add(object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel), style::margins(23, 18, 23, 4));
		about->setSelectable(true);
		box->add(object_ptr<Ui::FlatLabel>(box, u"Description"_q, st::infoProfileStatus), style::margins(23, 0, 23, 18));
		const auto view = Settings::AddButtonWithIcon(box, rpl::single(u"VIEW CHANNEL"_q), st::infoMainButton);
		view->setClickedCallback([=] { controller->showPeerHistory(peer->id); });
		box->add(object_ptr<Ui::BoxContentDivider>(box), style::margins());
		ChannelRequest(userId, u"get_chat_description"_q, {}, [=](QJsonValue text, QString failure) {
			if (weak) {
				*descriptionValue = text.toString();
				if (failure.isEmpty()) {
					about->setMarkedText(TextUtilities::ParseEntities(*descriptionValue, TextParseLinks));
				} else {
					about->setText(failure);
				}
			}
		});
		for (const auto &[type, label, icon] : std::vector<std::tuple<QString, QString, const style::icon*>>{
			{ u"Image"_q, u"Photos"_q, &st::infoIconMediaPhoto },
			{ u"Video"_q, u"Videos"_q, &st::infoIconMediaVideo },
			{ u"File"_q, u"Files"_q, &st::infoIconMediaFile },
			{ u"Gif"_q, u"GIFs"_q, &st::infoIconMediaGif },
		}) {
			const auto ids = box->lifetime().make_state<QJsonArray>();
			const auto text = box->lifetime().make_state<rpl::variable<QString>>(label);
			const auto wrap = box->add(object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(box,
				Settings::CreateButtonWithIcon(box, text->value(), st::infoSharedMediaButton,
					{ .icon = icon, .type = Settings::IconType::Simple })), style::margins());
			const auto button = wrap->entity();
			wrap->toggle(false, anim::type::instant);
			ChannelRequest(userId, u"get_chat_media"_q, { type, QJsonValue(), QJsonValue() },
				[=](QJsonValue value, QString failure) {
					if (!weak || !failure.isEmpty()) return;
					*ids = value.toArray();
					const auto count = int(ids->size());
					*text = (type == u"Image"_q) ? tr::lng_profile_photos(tr::now, lt_count, count)
						: (type == u"Video"_q) ? tr::lng_profile_videos(tr::now, lt_count, count)
						: (type == u"File"_q) ? tr::lng_profile_files(tr::now, lt_count, count)
						: tr::lng_profile_gifs(tr::now, lt_count, count);
					wrap->toggle(!ids->empty(), anim::type::instant);
				});
			button->setClickedCallback([=] {
				Ui::show(Box([=](not_null<Ui::GenericBox*> media) {
					media->setTitle(rpl::single(label));
					media->addRow(object_ptr<Ui::FlatLabel>(media, u"Select an item to view its message."_q, st::aboutLabel), st::boxRowPadding);
					for (auto i = ids->size(); i > std::max(0, int(ids->size()) - 100); --i) {
						const auto messageId = (*ids)[i - 1].toInt();
						const auto item = media->addRow(object_ptr<Ui::SettingsButton>(media,
							rpl::single(u"%1 %2"_q.arg(label).arg(i)), st::settingsButtonNoIcon), style::margins());
						item->setClickedCallback([=] {
							media->closeBox();
							controller->showPeerHistory(peer->id, Window::SectionShow(), MsgId(messageId));
						});
					}
					media->addButton(tr::lng_close(), [=] { media->closeBox(); });
				}), Ui::LayerOption::KeepOther);
			});
		}
		box->add(object_ptr<Ui::BoxContentDivider>(box), style::margins());
		const auto menuActions = box->lifetime().make_state<std::vector<std::pair<QString, Fn<void()>>>>();
		const auto popup = box->lifetime().make_state<std::unique_ptr<Ui::PopupMenu>>();
		const auto addAction = [=](QString text, Fn<void()> action) {
			menuActions->emplace_back(text, action);
			const auto icon = (text == u"Subscribers"_q) ? &st::menuIconGroups
				: (text == u"Change channel photo"_q) ? &st::menuIconPhotoSet
				: (text == u"Leave channel"_q) ? &st::menuIconLeave : &st::menuIconEdit;
			if (text == u"Edit channel"_q) {
				addQuickAction(u"Edit"_q, *icon, std::move(action));
			} else {
				const auto button = Settings::AddButtonWithIcon(box, rpl::single(text), st::infoProfileButton, { .icon = icon, .type = Settings::IconType::Simple });
				button->setClickedCallback(std::move(action));
			}
		};
		if (owner) {
			addQuickAction(u"Invite"_q, st::menuIconInvite, [=] { ShowChannelInvite(session, userId); });
			addAction(u"Subscribers"_q, [=] { ShowChannelSubscribers(session, userId, [=] {
				if (weak) ChannelRequest(userId, u"get_full_chat_by_id"_q, {}, [=](QJsonValue, QString) {
					if (weak) role->setText(StatusSubtitle(userId).value_or(QString()));
				});
			}); });
			addAction(u"Edit channel"_q, [=] {
				Ui::show(Box([=](not_null<Ui::GenericBox*> edit) {
					edit->setTitle(rpl::single(u"Edit Channel"_q));
					const auto title = edit->addRow(object_ptr<Ui::InputField>(edit, st::defaultInputField, rpl::single(u"Channel name"_q), *nameValue));
					const auto description = edit->addRow(object_ptr<Ui::InputField>(edit, st::newGroupDescription, Ui::InputField::Mode::MultiLine, rpl::single(u"Description"_q), *descriptionValue));
					const auto busy = edit->lifetime().make_state<bool>(false);
					const auto editWeak = QPointer<Ui::GenericBox>(edit.get());
					edit->addButton(tr::lng_settings_save(), [=] {
						if (*busy) return;
						const auto text = title->getLastText().trimmed();
						if (text.isEmpty()) { title->showError(); return; }
						*busy = true;
						ChannelRequest(userId, u"set_chat_name"_q, { text }, [=](QJsonValue, QString failure) {
							if (!editWeak) return;
							if (!failure.isEmpty()) { *busy = false; Ui::show(Ui::MakeInformBox(failure)); return; }
							const auto desc = description->getLastText();
							ChannelRequest(userId, u"set_chat_description"_q, { desc }, [=](QJsonValue, QString failure) {
								if (!editWeak) return;
								*busy = false;
								if (!failure.isEmpty()) { Ui::show(Ui::MakeInformBox(failure)); return; }
								if (weak) { *nameValue = text; *descriptionValue = desc; name->setText(text); about->setMarkedText(TextUtilities::ParseEntities(desc, TextParseLinks)); }
								edit->closeBox();
							});
						});
					});
					edit->addButton(tr::lng_cancel(), [=] { edit->closeBox(); });
				}), Ui::LayerOption::KeepOther);
			});
			addAction(u"Change channel photo"_q, [=] {
				const auto path = QFileDialog::getOpenFileName(box, u"Channel photo"_q, QString(), u"Images (*.png *.jpg *.jpeg *.webp)"_q);
				if (path.isEmpty() || !weak) return;
				ChannelRequest(userId, u"set_chat_profile_image"_q, { path }, [=](QJsonValue, QString failure) {
					if (!weak) return;
					if (!failure.isEmpty()) { Ui::show(Ui::MakeInformBox(failure)); return; }
					photo->showCustom(QImage(path));
				});
			});
		}
		addQuickAction(u"More"_q, st::infoTopBarMenu.icon, [=] {
			*popup = std::make_unique<Ui::PopupMenu>(actions, st::popupMenuWithIcons);
			(*popup)->deleteOnHide(false);
			for (const auto &[text, action] : *menuActions) {
				(*popup)->addAction(text, action);
			}
			(*popup)->popup(actions->mapToGlobal(QPoint(actions->width(), actions->height())));
		});
		const auto muted = box->lifetime().make_state<bool>(info.value(u"isMuted"_q).toBool());
		const auto busy = box->lifetime().make_state<bool>(false);
		const auto mute = addQuickAction(*muted ? u"Unmute"_q : u"Mute"_q,
			*muted ? st::menuIconUnmute : st::menuIconMute, nullptr, true);
		mute->setClickedCallback([=] {
			if (*busy) return;
			*busy = true;
			ChannelRequest(userId, u"set_chat_mute_duration"_q, { QJsonObject{ { u"kind"_q, *muted ? u"NotMuted"_q : u"Forever"_q } } }, [=](QJsonValue, QString failure) {
				if (!weak) return;
				*busy = false;
				if (!failure.isEmpty()) { Ui::show(Ui::MakeInformBox(failure)); return; }
				*muted = !*muted;
				mute->setContent(*muted ? u"Unmute"_q : u"Mute"_q,
					*muted ? st::menuIconUnmute : st::menuIconMute);
			});
		});
		if (!owner && info.value(u"contactIds"_q).toArray().contains(1)) {
			addAction(u"Leave channel"_q, [=] {
				Ui::show(Ui::MakeConfirmBox({
					.text = u"Leave this channel?"_q,
					.confirmed = [=](Fn<void()> close) {
						close();
						if (!weak) return;
						ChannelRequest(userId, u"remove_contact_from_chat"_q, { 1 }, [=](QJsonValue, QString error) {
							if (!weak) return;
							if (!error.isEmpty()) { Ui::show(Ui::MakeInformBox(error)); return; }
							close();
						});
					},
				}), Ui::LayerOption::KeepOther);
			});
		}
	});
}

class ChannelInfoMemento;

class ChannelInfoSection final : public Window::SectionWidget {
public:
	ChannelInfoSection(QWidget *parent, not_null<Window::SessionController*> controller, int userId)
	: Window::SectionWidget(parent, controller)
	, _userId(userId)
	, _scroll(this, st::defaultScrollArea) {
		const auto back = Ui::CreateChild<Ui::IconButton>(this, st::infoTopBarBack);
		back->setClickedCallback([=] { controller->showBackFromStack(); });
		back->moveToLeft(0, 0);
		const auto title = Ui::CreateChild<Ui::FlatLabel>(this, u"Channel Info"_q, st::boxTitle);
		title->moveToLeft(64, 17);
		const auto content = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(this));
		FillChannelInfo(content.data(), controller, userId, crl::guard(this, [=] {
			controller->showBackFromStack();
		}));
		sizeValue() | rpl::on_next([=](QSize size) {
			_scroll->setGeometry(0, st::infoTopBarHeight, size.width(), std::max(0, size.height() - st::infoTopBarHeight));
			content->resizeToWidth(size.width());
		}, lifetime());
	}
	bool showInternal(not_null<Window::SectionMemento*> memento, const Window::SectionShow &params) override;
	std::shared_ptr<Window::SectionMemento> createMemento() override;
	bool floatPlayerHandleWheelEvent(QEvent *e) override { return false; }
	QRect floatPlayerAvailableRect() override { return QRect(mapToGlobal(QPoint()), size()); }
protected:
	void paintEvent(QPaintEvent *e) override {
		if (animatingShow()) {
			Window::SectionWidget::paintEvent(e);
			return;
		}
		Painter p(this);
		p.fillRect(e->rect(), st::windowBg);
	}
	void keyPressEvent(QKeyEvent *e) override {
		if (e->key() == Qt::Key_Escape) {
			controller()->showBackFromStack();
		} else {
			Window::SectionWidget::keyPressEvent(e);
		}
	}
private:
	int _userId;
	object_ptr<Ui::ScrollArea> _scroll;
};

class ChannelInfoMemento final : public Window::SectionMemento {
public:
	explicit ChannelInfoMemento(int userId) : userId(userId) {}
	object_ptr<Window::SectionWidget> createWidget(QWidget *parent,
			not_null<Window::SessionController*> controller,
			Window::Column column, const QRect &geometry) override {
		auto result = object_ptr<ChannelInfoSection>(parent, controller, userId);
		result->setGeometry(geometry);
		return result;
	}
	int userId;
};

bool ChannelInfoSection::showInternal(not_null<Window::SectionMemento*> memento,
		const Window::SectionShow &params) {
	const auto channel = dynamic_cast<ChannelInfoMemento*>(memento.get());
	return channel && channel->userId == _userId;
}

std::shared_ptr<Window::SectionMemento> ChannelInfoSection::createMemento() {
	return std::make_shared<ChannelInfoMemento>(_userId);
}

} // namespace

void ShowChannelInfo(not_null<Window::SessionController*> controller, int userId,
		const Window::SectionShow &params) {
	controller->showSection(std::make_shared<ChannelInfoMemento>(userId), params);
}

void ShowChannelInvite(not_null<Main::Session*> session, int userId) {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Invite to Channel"_q));
		const auto canvas = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, 280), st::boxRowPadding);
		const auto image = canvas->lifetime().make_state<QImage>();
		const auto link = canvas->lifetime().make_state<QString>();
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(box, u"Loading…"_q, st::aboutLabel), st::boxRowPadding);
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel), st::boxRowPadding);
		label->setSelectable(true);
		canvas->paintRequest() | rpl::on_next([=] {
			if (image->isNull()) return;
			auto painter = QPainter(canvas);
			const auto size = std::min({ canvas->width(), canvas->height(), 260 });
			painter.drawImage(QRect((canvas->width() - size) / 2, (canvas->height() - size) / 2, size, size), *image);
		}, canvas->lifetime());
		const auto weak = QPointer<Ui::GenericBox>(box.get());
		LoadChannelInvite(userId, [=](QString text, QString svg, QString error) {
			if (!weak) return;
			if (!error.isEmpty()) { status->setText(error); return; }
			*link = text;
			label->setText(text);
			*image = QImage(520, 520, QImage::Format_ARGB32_Premultiplied);
			image->fill(Qt::white);
			auto renderer = QSvgRenderer(svg.toUtf8());
			{ auto painter = QPainter(image); renderer.render(&painter); }
			canvas->update();
			status->setText(u"Scan the QR code or share this link to subscribe to the channel."_q);
		});
		box->addButton(rpl::single(u"Copy Link"_q), [=] {
			if (link->isEmpty()) return;
			QGuiApplication::clipboard()->setText(*link);
			status->setText(u"Link copied."_q);
		});
		box->addLeftButton(rpl::single(u"Share"_q), [=] {
			if (link->isEmpty()) return;
			const auto holder = std::make_shared<QPointer<PeerListBox>>();
			const auto busy = std::make_shared<bool>(false);
			auto picker = std::make_unique<ChooseRecipientBoxController>(session, [=](not_null<Data::Thread*> thread) {
				if (*busy) return;
				*busy = true;
				SendChannelInvite(userId, int(peerToUser(thread->peer()->id).bare), [=](QString error) {
					*busy = false;
					if (!*holder) return;
					if (!error.isEmpty()) { Ui::show(Ui::MakeInformBox(error)); return; }
					(*holder)->closeBox();
					if (weak) status->setText(u"Invitation sent."_q);
				});
			}, [](not_null<Data::Thread*> thread) {
				return thread->peer()->isUser() && !IsReadOnlyChannel(peerToUser(thread->peer()->id).bare);
			});
			Ui::show(Box<PeerListBox>(std::move(picker), [=](not_null<PeerListBox*> pickerBox) {
				*holder = pickerBox.get();
				pickerBox->addButton(tr::lng_cancel(), [=] { pickerBox->closeBox(); });
			}), Ui::LayerOption::KeepOther);
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void ChooseContactToSend(not_null<Main::Session*> session, int userId) {
	const auto holder = std::make_shared<QPointer<PeerListBox>>();
	const auto busy = std::make_shared<bool>(false);
	auto controller = std::make_unique<ContactListController>(session, Fn<void(int)>(), false,
		[=](ContactRow contact) {
			if (*busy || !*holder) return;
			*busy = true;
			ShareContact(contact.id, userId, [=](QString error) {
				*busy = false;
				if (!*holder) return;
				if (!error.isEmpty()) { Ui::show(Ui::MakeInformBox(error)); return; }
				(*holder)->closeBox();
			});
		});
	Ui::show(Box<PeerListBox>(std::move(controller), [=](not_null<PeerListBox*> box) {
		*holder = box.get();
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void ShowJoinInvite(not_null<Window::SessionController*> controller, const QString &url) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Invitation"_q));
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(box, u"Checking invitation…"_q, st::aboutLabel), st::boxRowPadding);
		const auto weak = base::make_weak(box);
		const auto window = base::make_weak(controller);
		const auto ready = std::make_shared<bool>(false);
		const auto busy = std::make_shared<bool>(false);
		const auto action = box->lifetime().make_state<rpl::variable<QString>>(u"Join"_q);
		AccountRequest(u"check_qr"_q, { url }, [=](QJsonValue value, QString error) {
			if (!weak) return;
			if (!error.isEmpty()) { status->setText(error); return; }
			const auto invite = value.toObject();
			const auto kind = invite.value(u"kind"_q).toString();
			if (kind == u"askJoinBroadcast"_q || kind == u"askVerifyGroup"_q) {
				const auto channel = kind == u"askJoinBroadcast"_q;
				const auto name = invite.value(channel ? u"name"_q : u"grpname"_q).toString();
				status->setText((channel ? u"Join channel “%1”?"_q : u"Join group “%1”?"_q).arg(name));
				*ready = true;
			} else if (kind == u"askVerifyContact"_q) {
				*action = u"Chat"_q;
				AccountRequest(u"get_contact"_q, { invite.value(u"contact_id"_q) }, [=](QJsonValue contact, QString failure) {
					if (!weak) return;
					if (!failure.isEmpty()) { status->setText(failure); return; }
					status->setText(u"Chat with %1?"_q.arg(contact.toObject().value(u"displayName"_q).toString()));
					*ready = true;
				});
			} else {
				status->setText(kind.startsWith(u"withdraw"_q) || kind.startsWith(u"revive"_q)
					? u"This is your own invitation."_q : u"This link is not a contact, group or channel invitation."_q);
			}
		});
		box->addButton(action->value(), [=] {
			if (!*ready || *busy) return;
			*busy = true;
			status->setText(u"Joining…"_q);
			JoinInviteChat(url, [=](int chatId, QString error) {
				if (!weak || !window) return;
				*busy = false;
				if (!error.isEmpty()) { status->setText(error); return; }
				box->closeBox();
				QTimer::singleShot(0, crl::guard(controller, [=] { controller->showPeerHistory(peerFromUser(UserId(chatId))); }));
			});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowSharedContact(not_null<Window::SessionController*> controller, int messageId) {
	const auto card = SharedContactData(messageId);
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::lng_contact_details_title());
		const auto name = card.value(u"displayName"_q).toString();
		const auto image = std::make_shared<QImage>(QImage::fromData(QByteArray::fromBase64(card.value(u"profileImage"_q).toString().toLatin1())));
		const auto header = box->addRow(object_ptr<Ui::FixedHeightWidget>(box, st::infoProfileCover.height), style::margins());
		const auto empty = std::make_shared<Ui::EmptyUserpic>(Ui::EmptyUserpic::UserpicColor(Ui::EmptyUserpic::ColorIndex(messageId)), name);
		header->paintRequest() | rpl::on_next([=] {
			Painter p(header);
			const auto size = st::infoProfileCover.photo.photoSize;
			const auto x = (header->width() - size) / 2;
			const auto y = st::infoProfileCover.photoTop;
			if (image->isNull()) { empty->paintCircle(p, x, y, header->width(), size); return; }
			p.setRenderHint(QPainter::Antialiasing);
			p.setRenderHint(QPainter::SmoothPixmapTransform);
			auto clip = QPainterPath();
			clip.addEllipse(QRectF(x, y, size, size));
			p.setClipPath(clip);
			const auto side = std::min(image->width(), image->height());
			p.drawImage(QRect(x, y, size, size), *image, QRect((image->width() - side) / 2, (image->height() - side) / 2, side, side));
		}, header->lifetime());
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box, name.isEmpty() ? u"Contact"_q : name, st::deltaContactName), st::boxRowPadding, style::al_top);
		const auto bio = box->addRow(object_ptr<Ui::FlatLabel>(box, QString(), st::deltaContactBio), st::boxRowPadding, style::al_top);
		const auto status = box->addRow(object_ptr<Ui::FlatLabel>(box, u"Loading contact…"_q, st::aboutLabel), st::boxRowPadding);
		const auto weak = base::make_weak(box);
		const auto window = base::make_weak(controller);
		const auto contact = std::make_shared<ContactRow>();
		const auto busy = std::make_shared<bool>(false);
		// Core imports the identity/key from the card. A chat is created only on Chat.
		AccountRequest(u"import_vcard"_q, { card.value(u"file"_q) }, [=](QJsonValue ids, QString error) {
			if (!weak) return;
			if (!error.isEmpty() || ids.toArray().isEmpty()) { status->setText(error.isEmpty() ? u"Could not load this contact."_q : error); return; }
			AccountRequest(u"get_contact"_q, { ids.toArray().at(0) }, [=](QJsonValue value, QString failure) {
				if (!weak) return;
				if (!failure.isEmpty()) { status->setText(failure); return; }
				const auto data = value.toObject();
				*contact = ContactRow{ data.value(u"id"_q).toInt(), data.value(u"displayName"_q).toString(), data.value(u"address"_q).toString(), data.value(u"profileImage"_q).toString() };
				label->setText(contact->name);
				bio->setText(data.value(u"status"_q).toString());
				const auto photo = QImage(contact->avatar);
				if (image->isNull() && !photo.isNull()) { *image = photo; header->update(); }
				status->setText(QString());
			});
		});
		box->addButton(rpl::single(u"Chat"_q), [=] {
			if (*busy || contact->id <= 0) return;
			*busy = true;
			OpenContact(*contact, [=](int chatId, QString error) {
				if (!weak || !window) return;
				*busy = false;
				if (!error.isEmpty()) { status->setText(error); return; }
				box->closeBox();
				QTimer::singleShot(0, crl::guard(controller, [=] { controller->showPeerHistory(peerFromUser(UserId(chatId))); }));
			});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ChooseGroupMembers(not_null<Main::Session*> session,
		Fn<void(std::vector<int>, Fn<void(QString)>)> create) {
	auto controller = std::make_unique<ContactListController>(session, Fn<void(int)>(), true);
	const auto raw = controller.get();
	Ui::show(Box<PeerListBox>(std::move(controller), [=](not_null<PeerListBox*> box) {
		const auto weak = base::make_weak(box);
		const auto busy = std::make_shared<bool>(false);
		box->addButton(tr::lng_create_group_create(), [=] {
			if (*busy) return;
			if (raw->selectedMembers().empty()) { raw->showError(u"Select at least one contact."_q); return; }
			*busy = true;
			create(raw->selectedMembers(), [=](QString error) {
				if (!weak) return;
				*busy = false;
				if (!error.isEmpty()) raw->showError(error);
				else box->closeBox();
			});
		});
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void ShowContacts(not_null<Main::Session*> session, Fn<void(int)> done) {
	const auto holder = std::make_shared<QPointer<PeerListBox>>();
	auto controller = std::make_unique<ContactListController>(session, [=](int id) {
		if (*holder) (*holder)->closeBox();
		done(id);
	});
	const auto raw = controller.get();
	Ui::show(Box<PeerListBox>(std::move(controller), [=](not_null<PeerListBox*> box) {
		*holder = box.get();
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		box->addLeftButton(tr::lng_profile_add_contact(), [=] { ShowInviteQr(); });
		const auto sort = box->lifetime().make_state<QPointer<Ui::IconButton>>();
		*sort = box->addTopButton(st::contactsSortButton, [=] {
			const auto alphabet = raw->toggleSort();
			(*sort)->setIconOverride(alphabet ? &st::contactsSortOnlineIcon : nullptr,
				alphabet ? &st::contactsSortOnlineIconOver : nullptr);
		});
	}));
}

void AskText(
		const QString &title,
		const QString &placeholder,
		const QString &accept,
		Fn<void(QString)> done) {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(placeholder)));
		const auto submit = [=] {
			const auto text = field->getLastText().trimmed();
			if (text.isEmpty()) {
				field->showError();
				return;
			}
			box->closeBox();
			done(text);
		};
		field->submits() | rpl::on_next(submit, field->lifetime());
		box->setFocusCallback([=] {
			field->setFocusFast();
		});
		box->addButton(rpl::single(accept), submit);
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void AskChoice(
		const QString &title,
		std::vector<std::pair<QString, Fn<void()>>> options) {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(title));
		for (const auto &[text, callback] : options) {
			const auto button = box->addRow(
				object_ptr<Ui::SettingsButton>(
					box,
					rpl::single(text),
					st::settingsButtonNoIcon),
				style::margins());
			button->setClickedCallback([=] {
				box->closeBox();
				callback();
			});
		}
		box->addButton(tr::lng_cancel(), [=] {
			box->closeBox();
		});
	}));
}

void ShowInviteQr() {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"QR Invite Code"_q));
		const auto tabs = box->addRow(
			object_ptr<Ui::FixedHeightWidget>(box, 48),
			style::margins());
		const auto inviteTab = Ui::CreateChild<Ui::SettingsButton>(
			tabs,
			rpl::single(u"QR Invite Code"_q),
			st::settingsButtonNoIcon);
		const auto scanTab = Ui::CreateChild<Ui::SettingsButton>(
			tabs,
			rpl::single(u"Scan QR Code"_q),
			st::settingsButtonNoIcon);
		tabs->widthValue() | rpl::on_next([=](int width) {
			const auto half = width / 2;
			inviteTab->setGeometry(0, 0, half, tabs->height());
			scanTab->setGeometry(half, 0, width - half, tabs->height());
		}, tabs->lifetime());
		const auto canvas = box->addRow(
			object_ptr<Ui::RpWidget>(box),
			st::boxRowPadding);
		canvas->resize(canvas->width(), 280);
		const auto image = canvas->lifetime().make_state<QImage>();
		const auto status = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"Loading…"_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		status->setSelectable(true);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Paste an invite code"_q)));
		field->hide();
		canvas->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(canvas);
			p.fillRect(canvas->rect(), Qt::white);
			if (!image->isNull()) {
				const auto side = std::min(canvas->width(), canvas->height()) - 24;
				p.drawImage(
					QRect(
						(canvas->width() - side) / 2,
						(canvas->height() - side) / 2,
						side,
						side),
					*image);
			}
		}, canvas->lifetime());
		const auto text = canvas->lifetime().make_state<QString>();
		const auto showCode = [=](bool code) {
			canvas->setVisible(code);
			status->setVisible(true);
			field->setVisible(!code);
			box->setTitle(rpl::single(code
				? u"QR Invite Code"_q
				: u"Scan QR Code"_q));
		};
		inviteTab->setClickedCallback([=] { showCode(true); });
		scanTab->setClickedCallback([=] { showCode(false); });
		box->addButton(rpl::single(u"Copy Link"_q), [=] {
			if (!text->isEmpty()) {
				QGuiApplication::clipboard()->setText(*text);
				status->setText(u"Link copied."_q);
			}
		});
		box->addButton(rpl::single(u"Join"_q), [=] {
			const auto qr = field->getLastText().trimmed();
			if (qr.isEmpty()) {
				field->showError();
				return;
			}
			status->setText(u"Joining…"_q);
			JoinInviteQr(qr, [=](QString error) {
				if (!error.isEmpty()) {
					status->setText(error);
					return;
				}
				box->closeBox();
			});
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		LoadInviteQr([=](
				QString name,
				QString code,
				QString svg,
				QString error) {
			if (!error.isEmpty()) {
				status->setText(error);
				return;
			}
			*text = code;
			auto renderer = QSvgRenderer(svg.toUtf8());
			if (renderer.isValid()) {
				*image = QImage(512, 512, QImage::Format_ARGB32_Premultiplied);
				image->fill(Qt::white);
				auto p = QPainter(image);
				renderer.render(&p);
			}
			status->setText(u"Scan to chat with %1"_q.arg(
				name.isEmpty() ? u"me"_q : name));
			canvas->update();
		});
	}));
}

void ShowSecondDevice() {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Add Second Device"_q));
		const auto status = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"This creates a QR code that another device can scan to copy this profile."_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		status->setSelectable(true);
		const auto qrWrap = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::RpWidget>>(box,
				object_ptr<Ui::RpWidget>(box)),
			st::boxRowPadding);
		const auto canvas = qrWrap->entity();
		canvas->resize(canvas->width(), 280);
		qrWrap->toggle(false, anim::type::instant);
		const auto image = canvas->lifetime().make_state<QImage>();
		canvas->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(canvas);
			p.fillRect(canvas->rect(), Qt::white);
			if (!image->isNull()) {
				const auto side = std::min(canvas->width(), canvas->height()) - 24;
				p.drawImage(
					QRect(
						(canvas->width() - side) / 2,
						(canvas->height() - side) / 2,
						side,
						side),
					*image);
			}
		}, canvas->lifetime());
		const auto weak = base::make_weak(box);
		box->addButton(rpl::single(u"Continue"_q), [=] {
			status->setText(u"Preparing account…"_q);
			StartSecondDevice(
				[=](QString svg, QString error) {
					if (!weak) {
						return;
					}
					if (!error.isEmpty() || svg.isEmpty()) {
						status->setText(error.isEmpty()
							? u"Could not create the QR code."_q
							: error);
						return;
					}
					auto renderer = QSvgRenderer(svg.toUtf8());
					if (!renderer.isValid()) {
						status->setText(u"Could not draw the QR code."_q);
						return;
					}
					*image = QImage(520, 520, QImage::Format_ARGB32_Premultiplied);
					image->fill(Qt::white);
					auto p = QPainter(image);
					renderer.render(&p);
					qrWrap->toggle(true, anim::type::normal);
					canvas->update();
					status->setText(
						u"Install Delta Chat on the other device, use the same network, then tap Scan QR Code."_q);
				},
				[=](QString error) {
					if (!weak) {
						return;
					}
					if (error.isEmpty()) {
						box->closeBox();
						return;
					}
					status->setText(error);
				});
		});
		box->addButton(rpl::single(u"Cancel"_q), [=] {
			StopSecondDevice();
			box->closeBox();
		});
		box->lifetime().add([] { StopSecondDevice(); });
	}));
}

void ShowRelayEditor(QJsonObject transport, Fn<void()> refreshed) {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto addr = transport.value(u"addr"_q).toString();
		box->setTitle(rpl::single(u"Edit Relay"_q));
		const auto weak = base::make_weak(box);
		const auto asText = [](const QJsonValue &value) {
			return value.isDouble()
				? QString::number(value.toInt())
				: value.toString();
		};
		const auto email = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Email Address"_q),
			addr));
		const auto password = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Existing Password"_q),
			asText(transport.value(u"password"_q))));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"This login is for advanced users."_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"Do not use an address you're using in another app."_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		const auto more = box->addRow(object_ptr<Ui::SettingsButton>(
			box,
			rpl::single(u"+  More Options"_q),
			st::settingsButtonNoIcon));
		const auto advanced = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)),
			style::margins());
		advanced->hide(anim::type::instant);
		const auto inner = advanced->entity();
		const auto field = [=](const QString &placeholder, const QString &key) {
			return inner->add(object_ptr<Ui::InputField>(
				inner,
				st::defaultInputField,
				rpl::single(placeholder),
				asText(transport.value(key))));
		};
		inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			u"Inbox"_q,
			st::aboutLabel));
		const auto imapUser = field(u"IMAP login"_q, u"imapUser"_q);
		const auto imapServer = field(u"IMAP server"_q, u"imapServer"_q);
		const auto imapPort = field(u"IMAP port"_q, u"imapPort"_q);
		const auto imapSecurity = field(u"IMAP security"_q, u"imapSecurity"_q);
		inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			u"Outbox"_q,
			st::aboutLabel));
		const auto smtpUser = field(u"SMTP login"_q, u"smtpUser"_q);
		const auto smtpPassword = field(u"SMTP password"_q, u"smtpPassword"_q);
		const auto smtpServer = field(u"SMTP server"_q, u"smtpServer"_q);
		const auto smtpPort = field(u"SMTP port"_q, u"smtpPort"_q);
		const auto smtpSecurity = field(u"SMTP security"_q, u"smtpSecurity"_q);
		more->setClickedCallback([=] {
			advanced->toggle(!advanced->toggled(), anim::type::normal);
		});
		const auto status = box->addRow(
			object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		box->addLeftButton(
			rpl::single(u"Delete"_q),
			[=] {
				DeleteTransport(addr, [=](QString error) {
					if (!weak) {
						return;
					}
					if (!error.isEmpty()) {
						status->setText(error);
						return;
					}
					if (refreshed) {
						refreshed();
					}
					box->closeBox();
				});
			},
			st::attentionBoxButton);
		box->addButton(rpl::single(u"Log In"_q), [=] {
			auto next = transport;
			const auto put = [&](const QString &text, const QString &key, bool port) {
				const auto trimmed = text.trimmed();
				if (trimmed.isEmpty()) {
					next.remove(key);
					return;
				}
				if (port) {
					next.insert(key, trimmed.toInt());
				} else {
					next.insert(key, trimmed);
				}
			};
			const auto typed = email->getLastText().trimmed();
			next.insert(u"addr"_q, typed.isEmpty() ? addr : typed);
			put(password->getLastText(), u"password"_q, false);
			put(imapServer->getLastText(), u"imapServer"_q, false);
			put(imapPort->getLastText(), u"imapPort"_q, true);
			put(imapUser->getLastText(), u"imapUser"_q, false);
			put(imapSecurity->getLastText(), u"imapSecurity"_q, false);
			put(smtpServer->getLastText(), u"smtpServer"_q, false);
			put(smtpPort->getLastText(), u"smtpPort"_q, true);
			put(smtpUser->getLastText(), u"smtpUser"_q, false);
			put(smtpPassword->getLastText(), u"smtpPassword"_q, false);
			put(smtpSecurity->getLastText(), u"smtpSecurity"_q, false);
			status->setText(u"Saving…"_q);
			UpdateTransport(next, [=](QString error) {
				if (!weak) {
					return;
				}
				if (!error.isEmpty()) {
					status->setText(error);
					return;
				}
				if (refreshed) {
					refreshed();
				}
				box->closeBox();
			});
		});
		box->addButton(rpl::single(u"Cancel"_q), [=] { box->closeBox(); });
	}));
}

void ShowRelays() {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Relays"_q));
		const auto list = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto status = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"Messages are sent through these relays."_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		status->setSelectable(true);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Paste a relay QR code"_q)));
		const auto rows = list->lifetime().make_state<QVector<QPointer<Ui::RpWidget>>>();
		const auto weak = base::make_weak(box);
		const auto reload = std::make_shared<Fn<void()>>();
		*reload = [=] {
			ListTransports([=](QJsonArray transports, QString error) {
				if (!weak) {
					return;
				}
				for (const auto &row : *rows) {
					if (row) {
						delete row.data();
					}
				}
				rows->clear();
				if (!error.isEmpty()) {
					status->setText(error);
					return;
				}
				status->setText(transports.isEmpty()
					? u"No relays yet. Paste a relay QR code to add one."_q
					: u"Messages are sent through these relays."_q);
				for (const auto &value : transports) {
					const auto transport = value.toObject();
					const auto addr = transport.value(u"addr"_q).toString();
					const auto button = list->add(object_ptr<Ui::SettingsButton>(
						list,
						rpl::single(addr),
						st::settingsButtonNoIcon));
					rows->push_back(button);
					button->setClickedCallback([=] {
						ShowRelayEditor(transport, [=] {
							if (weak) {
								(*reload)();
							}
						});
					});
				}
				list->resizeToWidth(list->width());
			});
		};
		box->addButton(rpl::single(u"Add"_q), [=] {
			const auto qr = field->getLastText().trimmed();
			if (qr.isEmpty()) {
				field->showError();
				return;
			}
			status->setText(u"Adding relay…"_q);
			AddTransportFromQr(qr, [=](QString error) {
				if (!weak) {
					return;
				}
				if (!error.isEmpty()) {
					status->setText(error);
					return;
				}
				field->setText(QString());
				(*reload)();
			});
		});
		box->addButton(rpl::single(u"Close"_q), [=] { box->closeBox(); });
		(*reload)();
	}));
}

QString AppPickerUrl() {
	auto file = QFile(AppPickerUrlFile());
	if (file.open(QIODevice::ReadOnly)) {
		const auto saved = QString::fromUtf8(file.readAll()).trimmed();
		if (!saved.isEmpty()) {
			return saved;
		}
	}
	return QString::fromUtf8(kDefaultAppPickerUrl);
}

void SetAppPickerUrl(const QString &url) {
	const auto path = AppPickerUrlFile();
	QDir().mkpath(QFileInfo(path).absolutePath());
	auto file = QFile(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return;
	}
	file.write(url.trimmed().toUtf8());
}

namespace {

class AppsBrowser final : public Ui::BotWebView::Delegate {
public:
	explicit AppsBrowser(not_null<Main::Session*> session)
	: _session(session) {
	}

	void open(const QString &url) {
		_panel = Ui::BotWebView::Show({
			.url = url,
			.storageId = _session->local().resolveStorageIdBots(),
			.title = rpl::single(u"Apps"_q),
			.bottom = rpl::single(QString()),
			.delegate = static_cast<Ui::BotWebView::Delegate*>(this),
		});
	}

	Webview::ThemeParams botThemeParams() override {
		return Window::Theme::WebViewParams();
	}
	Ui::Text::MarkedContext botTextContext() override {
		return Core::TextContext({ .session = _session });
	}
	auto botDownloads(bool)
	-> const std::vector<Ui::BotWebView::DownloadsEntry> & override {
		return _downloads;
	}
	void botDownloadsAction(uint32, Ui::BotWebView::DownloadsAction) override {
	}
	bool botHandleLocalUri(QString, bool) override {
		return false;
	}
	void botHandleInvoice(QString) override {
	}
	void botHandleMenuButton(Ui::BotWebView::MenuButton) override {
	}
	bool botValidateExternalLink(QString) override {
		return true;
	}
	void botOpenIvLink(QString uri) override {
		QDesktopServices::openUrl(QUrl(uri));
	}
	void botSendData(QByteArray) override {
	}
	void botSwitchInlineQuery(std::vector<QString>, QString) override {
	}
	void botCheckWriteAccess(Fn<void(bool)> callback) override {
		if (callback) {
			callback(false);
		}
	}
	void botAllowWriteAccess(Fn<void(bool)> callback) override {
		if (callback) {
			callback(false);
		}
	}
	bool botStorageWrite(QString key, std::optional<QString> value) override {
		if (value) {
			_storage.insert(key, *value);
		} else {
			_storage.remove(key);
		}
		return true;
	}
	std::optional<QString> botStorageRead(QString key) override {
		const auto found = _storage.constFind(key);
		if (found == _storage.cend()) {
			return std::nullopt;
		}
		return *found;
	}
	void botStorageClear() override {
		_storage.clear();
	}
	void botRequestEmojiStatusAccess(Fn<void(bool)> callback) override {
		if (callback) {
			callback(false);
		}
	}
	void botSharePhone(Fn<void(bool)> callback) override {
		if (callback) {
			callback(false);
		}
	}
	void botInvokeCustomMethod(
			Ui::BotWebView::CustomMethodRequest request) override {
		if (request.callback) {
			request.callback(base::make_unexpected(u"Unsupported."_q));
		}
	}
	void botSetEmojiStatus(
			Ui::BotWebView::SetEmojiStatusRequest request) override {
		if (request.callback) {
			request.callback(u"Unsupported."_q);
		}
	}
	void botDownloadFile(Ui::BotWebView::DownloadFileRequest) override {
	}
	void botResolveButtonEmoji(
			Ui::BotWebView::ResolveButtonEmojiRequest) override {
	}
	void botSendPreparedMessage(
			Ui::BotWebView::SendPreparedMessageRequest) override {
	}
	void botRequestChat(Ui::BotWebView::RequestChatRequest) override {
	}
	void botVerifyAge(int) override {
	}
	void botOpenPrivacyPolicy() override {
	}
	void botClose() override {
		_panel = nullptr;
	}

private:
	const not_null<Main::Session*> _session;
	std::unique_ptr<Ui::BotWebView::Panel> _panel;
	std::vector<Ui::BotWebView::DownloadsEntry> _downloads;
	QHash<QString, QString> _storage;

};

std::unique_ptr<AppsBrowser> AppsWindow;

} // namespace

void ShowApps(not_null<Main::Session*> session) {
	auto url = AppPickerUrl().trimmed();
	if (url.isEmpty()) {
		url = QString::fromUtf8(kDefaultAppPickerUrl);
	}
	if (!url.endsWith('/')) {
		url += '/';
	}
	if (!AppsWindow) {
		AppsWindow = std::make_unique<AppsBrowser>(session);
	}
	AppsWindow->open(url);
}

void ShowAppPickerUrl() {
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"App picker URL"_q));
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"Address of the web store used to pick apps. Leave the default to use the public app list."_q,
				st::aboutLabel),
			st::boxRowPadding,
			style::al_top);
		const auto field = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"https://"_q)));
		field->setText(AppPickerUrl());
		box->setFocusCallback([=] { field->setFocusFast(); });
		box->addButton(rpl::single(u"Save"_q), [=] {
			SetAppPickerUrl(field->getLastText().trimmed());
			box->closeBox();
		});
		box->addButton(rpl::single(u"Cancel"_q), [=] { box->closeBox(); });
	}));
}

void ShowCreateGroup(Fn<void(int)> done) {
	constexpr auto kSelfContactId = 1;
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"New Group"_q));
		const auto name = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Group name"_q)));
		const auto description = box->addRow(object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			rpl::single(u"Description"_q),
			QString()));
		const auto imagePath = box->lifetime().make_state<QString>();
		const auto photo = box->addRow(
			object_ptr<Ui::SettingsButton>(
				box,
				rpl::single(u"Add group photo"_q),
				st::settingsButtonNoIcon),
			style::margins());
		const auto photoName = box->addRow(
			object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel),
			st::boxRowPadding);
		photo->setClickedCallback([=] {
			const auto path = QFileDialog::getOpenFileName(
				box,
				u"Group photo"_q,
				QString(),
				u"Images (*.png *.jpg *.jpeg *.webp)"_q);
			if (path.isEmpty()) {
				return;
			}
			*imagePath = path;
			photoName->setText(QFileInfo(path).fileName());
		});
		const auto members = box->lifetime().make_state<
			std::vector<std::pair<int, QString>>>();
		members->push_back({ kSelfContactId, u"You"_q });
		const auto count = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				u"1 member"_q,
				st::aboutLabel),
			st::boxRowPadding);
		const auto list = box->addRow(
			object_ptr<Ui::VerticalLayout>(box),
			style::margins());
		const auto error = box->addRow(
			object_ptr<Ui::FlatLabel>(box, QString(), st::aboutLabel),
			st::boxRowPadding);
		error->setSelectable(true);
		const auto busy = box->lifetime().make_state<bool>(false);
		const auto contains = [=](int id) {
			return std::any_of(
				members->begin(),
				members->end(),
				[&](const auto &row) { return row.first == id; });
		};
		const auto rebuild = box->lifetime().make_state<Fn<void()>>();
		*rebuild = [=] {
			list->clear();
			count->setText(members->size() == 1
				? u"1 member"_q
				: u"%1 members"_q.arg(members->size()));
			const auto add = list->add(object_ptr<Ui::SettingsButton>(
				list,
				rpl::single(u"Add members"_q),
				st::settingsButtonNoIcon));
			add->setClickedCallback([=] {
				box->uiShow()->showBox(Box([=](not_null<Ui::GenericBox*> picker) {
					picker->setTitle(rpl::single(u"Add members"_q));
					const auto chosen = picker->lifetime().make_state<
						std::vector<std::pair<int, QString>>>();
					const auto rows = picker->addRow(
						object_ptr<Ui::VerticalLayout>(picker),
						style::margins());
					const auto status = picker->addRow(
						object_ptr<Ui::FlatLabel>(
							picker,
							u"Loading…"_q,
							st::aboutLabel),
						st::boxRowPadding);
					ListContacts([=](
							std::vector<ContactRow> contacts,
							QString failure) {
						if (!failure.isEmpty()) {
							status->setText(failure);
							return;
						}
						auto shown = 0;
						for (const auto &contact : contacts) {
							if (contact.id == kSelfContactId
								|| contains(contact.id)) {
								continue;
							}
							++shown;
							const auto id = contact.id;
							const auto label = contact.name;
							const auto check = rows->add(
								object_ptr<Ui::Checkbox>(
									rows,
									label,
									false,
									st::defaultCheckbox));
							check->checkedChanges(
							) | rpl::on_next([=](bool checked) {
								const auto found = std::find_if(
									chosen->begin(),
									chosen->end(),
									[&](const auto &row) {
										return row.first == id;
									});
								if (checked && found == chosen->end()) {
									chosen->push_back({ id, label });
								} else if (!checked && found != chosen->end()) {
									chosen->erase(found);
								}
							}, check->lifetime());
						}
						status->setText(shown
							? QString()
							: u"No contacts to add."_q);
					});
					picker->addButton(rpl::single(u"Add"_q), [=] {
						for (const auto &row : *chosen) {
							if (!contains(row.first)) {
								members->push_back(row);
							}
						}
						picker->closeBox();
						(*rebuild)();
					});
					picker->addButton(tr::lng_cancel(), [=] {
						picker->closeBox();
					});
				}));
			});
			for (const auto &row : *members) {
				const auto id = row.first;
				const auto label = (id == kSelfContactId)
					? row.second
					: u"%1 — Remove"_q.arg(row.second);
				const auto button = list->add(object_ptr<Ui::SettingsButton>(
					list,
					rpl::single(label),
					st::settingsButtonNoIcon));
				if (id == kSelfContactId) {
					continue;
				}
				button->setClickedCallback([=] {
					members->erase(
						std::remove_if(
							members->begin(),
							members->end(),
							[&](const auto &entry) {
								return entry.first == id;
							}),
						members->end());
					(*rebuild)();
				});
			}
		};
		(*rebuild)();
		const auto submit = [=] {
			if (*busy) {
				return;
			}
			const auto title = name->getLastText().trimmed();
			if (title.isEmpty()) {
				name->showError();
				return;
			}
			*busy = true;
			error->setText(u"Creating…"_q);
			auto ids = std::vector<int>();
			ids.reserve(members->size());
			for (const auto &row : *members) {
				ids.push_back(row.first);
			}
			CreateGroup(
				title,
				description->getLastText().trimmed(),
				ids,
				*imagePath,
				[=](int chatId, QString failure) {
					*busy = false;
					if (!failure.isEmpty() || chatId <= 0) {
						error->setText(failure.isEmpty()
							? u"Could not create the group."_q
							: failure);
						return;
					}
					box->closeBox();
					done(chatId);
				});
		};
		name->submits() | rpl::on_next([=] { submit(); }, name->lifetime());
		box->setFocusCallback([=] { name->setFocusFast(); });
		box->addButton(rpl::single(u"Create"_q), submit);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

void ShowMessageInfo(not_null<Window::SessionController*> controller, int messageId) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Message info"_q));
		box->setMaxHeight(style::ConvertScale(480));
		const auto label = box->addRow(object_ptr<Ui::FlatLabel>(box,
			u"Loading…"_q, st::aboutLabel), st::boxRowPadding);
		label->setSelectable(true);
		const auto weak = base::make_weak(box);
		MessageInfo(messageId, [=](QString text, QString error) {
			if (!weak) return;
			label->setText(error.isEmpty() ? text : error);
		});
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

} // namespace Delta
