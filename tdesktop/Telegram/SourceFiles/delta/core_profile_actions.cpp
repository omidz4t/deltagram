/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/core_profile_actions.h"

#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "delta/delta_bridge.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace Delta {
namespace {

constexpr auto kLiveAccount = 1;

[[nodiscard]] AccountInventoryRequest LiveProfileRpc(std::function<bool()> current) {
	return [current = std::move(current)](QString method, QJsonArray args, DeltaRpc::Callback done) {
		if (!current || !current()) {
			return;
		}
		auto rest = QJsonArray();
		for (auto i = 1; i < args.size(); ++i) {
			rest.append(args.at(i));
		}
		AccountRequest(method, rest, [=](QJsonValue result, QString error) {
			if (!current()) {
				return;
			}
			if (!error.isEmpty()) {
				done({ { u"error"_q, error } });
			} else {
				done({ { u"result"_q, result } });
			}
		});
	};
}

void WatchAccount(not_null<Ui::RpWidget*> widget, const std::shared_ptr<AccountScope> &scope) {
	AccountViewResets() | rpl::on_next([scope](int) {
		scope->invalidate();
	}, widget->lifetime());
}

[[nodiscard]] std::optional<int> SenderContactId(std::uint64_t userId) {
	const auto row = ContactForPeer(userId);
	if (!row || !CoreProfileRealId(row->id)) {
		return std::nullopt;
	}
	return row->id;
}

[[nodiscard]] std::optional<int> ProfileChatId(std::uint64_t userId) {
	if (SenderContactId(userId)) {
		return std::nullopt;
	}
	const auto chat = CoreChatUserId(userId);
	if (!chat || !CoreProfileRealId(*chat)) {
		return std::nullopt;
	}
	return chat;
}

[[nodiscard]] std::optional<int> DirectProfileChatId(std::uint64_t userId) {
	const auto chat = CoreChatUserId(userId);
	if (!chat || !CoreProfileRealId(*chat)) {
		return std::nullopt;
	}
	return chat;
}

[[nodiscard]] QString ContactEncryptionFlags(const CoreProfileContact &contact) {
	auto lines = QStringList();
	lines.push_back(contact.isKeyContact
		? u"Key contact"_q
		: u"Address contact"_q);
	lines.push_back(contact.e2eeAvail
		? u"End-to-end encryption available"_q
		: u"End-to-end encryption is not available"_q);
	return lines.join('\n');
}

[[nodiscard]] style::margins EncryptionInfoLabelPadding() {
	const auto padding = st::boxPadding;
	return QMargins(padding.left(), 0, padding.right(), padding.bottom());
}

[[nodiscard]] object_ptr<Ui::GenericBox> MakeEncryptionInfoBox(QString text) {
	return Box([text = std::move(text)](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(u"Encryption info"_q));
		box->setMaxHeight(st::boxMaxListHeight);
		const auto label = box->addRow(
			object_ptr<Ui::FlatLabel>(box, text, st::boxLabel),
			EncryptionInfoLabelPadding());
		label->setSelectable(true);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	});
}

void ShowEncryptionBox(
		not_null<Window::SessionController*> controller,
		const std::shared_ptr<AccountScope> &scope,
		std::optional<int> contactId,
		std::optional<int> chatId) {
	const auto current = CaptureAccountGuard();
	const auto consumed = std::make_shared<bool>(false);
	const auto loading = controller->show(Box([scope, consumed](
			not_null<Ui::GenericBox*> box) {
		box->boxClosing() | rpl::on_next([consumed] {
			*consumed = true;
		}, box->lifetime());
		box->setTitle(rpl::single(u"Encryption info"_q));
		box->addRow(
			object_ptr<Ui::FlatLabel>(box, u"Loading…"_q, st::boxLabel),
			EncryptionInfoLabelPadding());
		WatchAccount(box, scope);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
	const auto abandoned = [=] {
		return *consumed || !loading;
	};
	const auto present = crl::guard(controller, [=](QString text) {
		if (abandoned()) {
			return;
		}
		*consumed = true;
		loading->closeBox();
		if (current && !current()) {
			return;
		}
		controller->show(MakeEncryptionInfoBox(std::move(text)));
	});
	ReadCoreEncryptionInfo(
		LiveProfileRpc(current),
		*scope,
		kLiveAccount,
		contactId,
		chatId,
		crl::guard(controller, [=](CoreProfileResult result) {
			if (abandoned()) {
				return;
			}
			if (current && !current()) {
				*consumed = true;
				loading->closeBox();
				return;
			}
			if (result.outcome != CoreProfileOutcome::Verified) {
				present(result.error.isEmpty()
					? kCoreEncryptionUnreadable
					: result.error);
				return;
			}
			const auto text = result.text;
			if (!contactId) {
				present(text);
				return;
			}
			ResolveCoreProfileContact(
				LiveProfileRpc(current),
				*scope,
				kLiveAccount,
				contactId,
				std::nullopt,
				crl::guard(controller, [=](CoreProfileResult contact) {
					if (abandoned()) {
						return;
					}
					if (current && !current()) {
						*consumed = true;
						loading->closeBox();
						return;
					}
					if (CoreProfileSucceeded(contact)) {
						present(text
							+ u"\n\n"_q
							+ ContactEncryptionFlags(contact.contact));
					} else {
						present(text);
					}
				}));
		}));
}

} // namespace

void ShowCoreChatEncryptionInfo(
		Window::SessionController *controller,
		int chat) {
	if (!controller) {
		return;
	}
	if (!OwnsSession(&controller->session()) || !CoreProfileRealId(chat)) {
		controller->showToast(kCoreEncryptionUnavailable);
		return;
	}
	const auto current = CaptureAccountGuard();
	if (!current || !current()) {
		controller->showToast(kCoreEncryptionUnreadable);
		return;
	}
	ShowEncryptionBox(controller, std::make_shared<AccountScope>(), {}, chat);
}

void ShowCorePeerEncryptionInfo(
		Window::SessionController *controller,
		std::uint64_t userId) {
	if (!controller) {
		return;
	}
	if (!OwnsSession(&controller->session())
		|| IsSystemUser(userId)
		|| IsGroup(userId)) {
		controller->showToast(kCoreEncryptionUnavailable);
		return;
	}
	const auto current = CaptureAccountGuard();
	if (!current || !current()) {
		controller->showToast(kCoreEncryptionUnreadable);
		return;
	}
	if (const auto contact = SenderContactId(userId)) {
		ShowEncryptionBox(
			controller,
			std::make_shared<AccountScope>(),
			contact,
			std::nullopt);
		return;
	}
	if (const auto chat = ProfileChatId(userId)) {
		ShowEncryptionBox(
			controller,
			std::make_shared<AccountScope>(),
			std::nullopt,
			chat);
		return;
	}
	controller->showToast(kCoreEncryptionUnavailable);
}

void FillCoreContactNameBox(
		Ui::GenericBox *box,
		Window::SessionController *controller,
		std::uint64_t userId,
		bool) {
	if (!box || !controller) {
		return;
	}
	box->setTitle(rpl::single(kCoreEditNameLabel));
	const auto status = box->addRow(
		object_ptr<Ui::FlatLabel>(box, u"Loading…"_q, st::aboutLabel),
		st::boxRowPadding);
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::defaultInputField,
			rpl::single(u"Name"_q)));
	field->hide();
	const auto scope = std::make_shared<AccountScope>();
	const auto current = CaptureAccountGuard();
	const auto busy = std::make_shared<bool>(false);
	const auto contactId = std::make_shared<int>(0);
	WatchAccount(box, scope);
	if (!OwnsSession(&controller->session())
		|| IsSystemUser(userId)
		|| IsGroup(userId)
		|| !current
		|| !current()) {
		status->setText(kCoreProfileUnavailable);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
		return;
	}
	const auto save = [=] {
		if (*busy || *contactId <= 9 || (current && !current())) {
			return;
		}
		const auto name = field->getLastText().trimmed();
		*busy = true;
		status->setText(u"Saving…"_q);
		ChangeCoreContactName(
			LiveProfileRpc(current),
			*scope,
			kLiveAccount,
			*contactId,
			name,
			[=](CoreProfileResult result) {
				*busy = false;
				if (current && !current()) {
					return;
				}
				if (result.outcome == CoreProfileOutcome::Verified) {
					box->closeBox();
					return;
				}
				status->setText(result.error.isEmpty()
					? kCoreProfileUnverified
					: result.error);
			});
	};
	box->addButton(tr::lng_box_done(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	ResolveCoreProfileContact(
		LiveProfileRpc(current),
		*scope,
		kLiveAccount,
		SenderContactId(userId),
		ProfileChatId(userId),
		[=](CoreProfileResult result) {
			if (current && !current()) {
				return;
			}
			if (!CoreProfileSucceeded(result) || !CoreProfileRealId(result.contactId)) {
				status->setText(result.error.isEmpty()
					? kCoreProfileUnavailable
					: result.error);
				return;
			}
			*contactId = result.contactId;
			field->setText(result.contact.name);
			field->show();
			status->setText(ContactEncryptionFlags(result.contact));
			box->setFocusCallback([=] { field->setFocusFast(); });
			field->submits() | rpl::on_next(save, field->lifetime());
		});
}

void ShowCoreContactNameBox(
		Window::SessionController *controller,
		std::uint64_t userId,
		bool adding) {
	if (!controller) {
		return;
	}
	if (!OwnsSession(&controller->session())) {
		controller->showToast(kCoreProfileUnavailable);
		return;
	}
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		FillCoreContactNameBox(box, controller, userId, adding);
	}));
}

void ShowCoreContactDeleteBox(
		Window::SessionController *controller,
		std::uint64_t userId) {
	if (!controller) {
		return;
	}
	if (!OwnsSession(&controller->session())
		|| IsSystemUser(userId)
		|| IsGroup(userId)) {
		controller->showToast(kCoreProfileUnavailable);
		return;
	}
	const auto current = CaptureAccountGuard();
	if (!current || !current()) {
		controller->showToast(kCoreProfileUnreadable);
		return;
	}
	const auto scope = std::make_shared<AccountScope>();
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(tr::lng_info_delete_contact(tr::now)));
		const auto status = box->addRow(
			object_ptr<Ui::FlatLabel>(box, u"Loading…"_q, st::aboutLabel),
			st::boxRowPadding);
		const auto check = box->addRow(
			object_ptr<Ui::Checkbox>(
				box,
				u"Also delete chat and its messages"_q,
				false,
				st::defaultCheckbox),
			st::boxRowPadding);
		check->hide();
		const auto busy = std::make_shared<bool>(false);
		const auto contactId = std::make_shared<int>(0);
		const auto reviewedChat = std::make_shared<int>(
			DirectProfileChatId(userId).value_or(0));
		WatchAccount(box, scope);
		const auto weak = base::make_weak(box);
		box->addButton(tr::lng_box_delete(), [=] {
			if (*busy || *contactId <= 9 || (current && !current())) {
				return;
			}
			*busy = true;
			status->setText(u"Deleting…"_q);
			const auto chat = CoreProfileRealId(*reviewedChat)
				? std::optional<int>(*reviewedChat)
				: std::nullopt;
			DeleteCoreProfileContact(
				LiveProfileRpc(current),
				*scope,
				kLiveAccount,
				*contactId,
				[=](CoreProfileResult result) {
					*busy = false;
					if (!weak || (current && !current())) {
						return;
					}
					if (CoreProfileSucceeded(result)) {
						const auto peer = controller->activeChatCurrent().peer();
						const auto activeChat = peer
							? CoreChatUserId(peerToUser(peer->id).bare)
							: std::nullopt;
						box->closeBox();
						if (CoreProfileShouldUnselectChat(
								result,
								kLiveAccount,
								kLiveAccount,
								activeChat)) {
							controller->showBackFromStack();
						}
						return;
					}
					status->setText(result.error.isEmpty()
						? kCoreProfileUnverified
						: result.error);
				},
				check->checked(),
				chat);
		}, st::attentionBoxButton);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
		ResolveCoreProfileContact(
			LiveProfileRpc(current),
			*scope,
			kLiveAccount,
			SenderContactId(userId),
			ProfileChatId(userId),
			[=](CoreProfileResult result) {
				if (!weak || (current && !current())) {
					return;
				}
				if (!CoreProfileSucceeded(result) || !CoreProfileRealId(result.contactId)) {
					status->setText(result.error.isEmpty()
						? kCoreProfileUnavailable
						: result.error);
					return;
				}
				*contactId = result.contactId;
				if (CoreProfileRealId(result.chatId)) {
					*reviewedChat = result.chatId;
				}
				const auto shown = result.contact.displayName.isEmpty()
					? result.contact.name
					: result.contact.displayName;
				status->setText(tr::lng_sure_delete_contact(
					tr::now,
					lt_contact,
					shown));
				check->show();
			});
	}));
}

} // namespace Delta
