#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class GenericBox;
} // namespace Ui

namespace Delta {

inline const QString kCoreProfileUnavailable = QStringLiteral(
	"This contact cannot be changed here.");
inline const QString kCoreProfileUnreadable = QStringLiteral(
	"The contact could not be checked. Refresh before trying again.");
inline const QString kCoreProfileUnconfirmed = QStringLiteral(
	"The contact change was not confirmed. Refresh before trying again.");
inline const QString kCoreProfileUnverified = QStringLiteral(
	"The contact change could not be verified. Refresh before trying again.");
inline const QString kCoreEncryptionUnreadable = QStringLiteral(
	"Encryption info could not be read.");
inline const QString kCoreEncryptionUnavailable = QStringLiteral(
	"Encryption info is not available for this conversation.");
inline const QString kCoreEditNameLabel = QStringLiteral("Edit name");
inline const QString kCoreProfileChatAmbiguous = QStringLiteral(
	"This contact may still have a hidden chat. Open the conversation or uncheck Also delete chat.");

inline constexpr int kCoreContactListDefault = 0;
inline constexpr int kCoreContactListAddSelf = 2;
inline constexpr int kCoreContactListAddress = 4;

enum class CoreProfileOutcome : unsigned char {
	Denied,
	Verified,
	Acknowledged,
	Unknown,
};

enum class CoreProfileLookup : unsigned char {
	Invalid,
	Missing,
	Found,
};

struct CoreProfileContact {
	int id = 0;
	QString name;
	QString displayName;
	bool isKeyContact = false;
	bool e2eeAvail = false;
};

struct CoreProfileChat {
	int id = 0;
	CoreChatType type = CoreChatType::Single;
	bool isSelfTalk = false;
	std::vector<int> contactIds;
	std::vector<int> peerContactIds;
};

struct CoreProfileLookupResult {
	CoreProfileLookup kind = CoreProfileLookup::Invalid;
	int chatId = 0;
};

struct CoreProfileResult {
	CoreProfileOutcome outcome = CoreProfileOutcome::Denied;
	bool attempted = false;
	bool wrote = false;
	bool acknowledged = false;
	int contactId = 0;
	int chatId = 0;
	bool chatAttempted = false;
	bool chatWrote = false;
	bool chatAcknowledged = false;
	CoreProfileContact contact;
	QString text;
	QString error;
};

[[nodiscard]] inline bool CoreProfileSucceeded(const CoreProfileResult &result) {
	return result.outcome == CoreProfileOutcome::Verified
		|| result.outcome == CoreProfileOutcome::Acknowledged;
}

[[nodiscard]] inline bool CoreProfileRealId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline std::optional<int> CoreProfileExactInt(const QJsonValue &value) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto id = value.toInt();
	if (id <= 0
		|| value.toDouble() != double(id)
		|| !CoreIdFitsInt(std::uint64_t(id))) {
		return std::nullopt;
	}
	return id;
}

[[nodiscard]] inline bool CoreProfileRpcNull(const QJsonObject &reply) {
	return !reply.contains(QStringLiteral("error"))
		&& reply.contains(QStringLiteral("result"))
		&& reply.value(QStringLiteral("result")).isNull();
}

[[nodiscard]] inline CoreProfileLookupResult CoreProfileChatIdLookup(
		const QJsonObject &reply) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return {};
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (value.isNull()) {
		return { CoreProfileLookup::Missing, 0 };
	}
	const auto id = CoreProfileExactInt(value);
	if (!id || !CoreProfileRealId(*id)) {
		return {};
	}
	return { CoreProfileLookup::Found, *id };
}

[[nodiscard]] inline std::optional<QSet<int>> CoreProfileContactIdsFromListReply(
		const QJsonObject &reply) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isArray()) {
		return std::nullopt;
	}
	auto ids = QSet<int>();
	for (const auto &entry : value.toArray()) {
		if (!entry.isObject()) {
			return std::nullopt;
		}
		const auto id = CoreProfileExactInt(
			entry.toObject().value(QStringLiteral("id")));
		if (!id || ids.contains(*id)) {
			return std::nullopt;
		}
		ids.insert(*id);
	}
	return ids;
}

[[nodiscard]] inline QJsonArray CoreProfileContactListArgs(
		int account,
		int flags) {
	auto args = QJsonArray();
	args.append(account);
	args.append(flags);
	args.append(QJsonValue(QJsonValue::Null));
	return args;
}

[[nodiscard]] inline std::optional<CoreProfileContact> CoreProfileContactFromJson(
		const QJsonObject &json,
		int expectedId) {
	if (!CoreProfileRealId(expectedId) || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto id = CoreProfileExactInt(json.value(QStringLiteral("id")));
	if (!id || *id != expectedId) {
		return std::nullopt;
	}
	const auto name = json.value(QStringLiteral("name"));
	const auto display = json.value(QStringLiteral("displayName"));
	const auto key = json.value(QStringLiteral("isKeyContact"));
	const auto e2ee = json.value(QStringLiteral("e2eeAvail"));
	if (!name.isString() || !display.isString() || !key.isBool() || !e2ee.isBool()) {
		return std::nullopt;
	}
	CoreProfileContact result;
	result.id = *id;
	result.name = name.toString();
	result.displayName = display.toString();
	result.isKeyContact = key.toBool();
	result.e2eeAvail = e2ee.toBool();
	return result;
}

[[nodiscard]] inline std::optional<CoreProfileContact> CoreProfileContactFromReply(
		const QJsonObject &reply,
		int expectedId) {
	if (reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	return CoreProfileContactFromJson(value.toObject(), expectedId);
}

[[nodiscard]] inline std::optional<CoreProfileContact> CoreProfileContactFromMap(
		const QJsonObject &map,
		int expectedId) {
	if (!CoreProfileRealId(expectedId)) {
		return std::nullopt;
	}
	const auto direct = map.value(QString::number(expectedId));
	if (direct.isObject()) {
		return CoreProfileContactFromJson(direct.toObject(), expectedId);
	}
	for (auto i = map.begin(); i != map.end(); ++i) {
		bool ok = false;
		const auto key = i.key().toInt(&ok);
		if (!ok || key != expectedId || !i.value().isObject()) {
			continue;
		}
		return CoreProfileContactFromJson(i.value().toObject(), expectedId);
	}
	return std::nullopt;
}

[[nodiscard]] inline std::optional<CoreProfileContact> CoreProfileContactFromMapReply(
		const QJsonObject &reply,
		int expectedId) {
	if (reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	return CoreProfileContactFromMap(value.toObject(), expectedId);
}

[[nodiscard]] inline std::optional<QString> CoreEncryptionTextFromReply(
		const QJsonObject &reply) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto text = value.toString();
	if (text.trimmed().isEmpty()) {
		return std::nullopt;
	}
	return text;
}

[[nodiscard]] inline std::optional<CoreProfileChat> CoreProfileSingleChat(
		const QJsonObject &json,
		int expectedChatId) {
	if (!CoreProfileRealId(expectedChatId) || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	if (!CoreJsonIdMatches(json, std::uint64_t(expectedChatId))) {
		return std::nullopt;
	}
	const auto type = CoreJsonKnownChatType(json.value(QStringLiteral("chatType")));
	if (!type || *type != CoreChatType::Single) {
		return std::nullopt;
	}
	const auto selfTalk = json.value(QStringLiteral("isSelfTalk"));
	if (!selfTalk.isBool() || selfTalk.toBool()) {
		return std::nullopt;
	}
	const auto deviceChat = json.value(QStringLiteral("isDeviceChat"));
	const auto deviceTalk = json.value(QStringLiteral("isDeviceTalk"));
	if (!deviceChat.isBool() && !deviceTalk.isBool()) {
		return std::nullopt;
	}
	if ((deviceChat.isBool() && deviceChat.toBool())
		|| (deviceTalk.isBool() && deviceTalk.toBool())) {
		return std::nullopt;
	}
	const auto contacts = json.value(QStringLiteral("contactIds"));
	if (!contacts.isArray()) {
		return std::nullopt;
	}
	CoreProfileChat result;
	result.id = expectedChatId;
	result.type = *type;
	result.isSelfTalk = false;
	auto seen = QSet<int>();
	for (const auto &entry : contacts.toArray()) {
		const auto id = CoreProfileExactInt(entry);
		if (!id || seen.contains(*id)) {
			return std::nullopt;
		}
		seen.insert(*id);
		result.contactIds.push_back(*id);
		if (*id > 9) {
			result.peerContactIds.push_back(*id);
		}
	}
	return result;
}

[[nodiscard]] inline std::optional<CoreProfileChat> CoreProfileSingleChatFromReply(
		const QJsonObject &reply,
		int expectedChatId) {
	if (reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	return CoreProfileSingleChat(value.toObject(), expectedChatId);
}

[[nodiscard]] inline std::optional<int> CoreExactPeerContactId(
		const CoreProfileChat &chat) {
	if (chat.peerContactIds.size() != 1) {
		return std::nullopt;
	}
	const auto id = chat.peerContactIds.front();
	if (!CoreProfileRealId(id)) {
		return std::nullopt;
	}
	return id;
}

[[nodiscard]] inline std::optional<CoreProfileChat> CoreProfileReviewedSingleChat(
		const QJsonObject &reply,
		int expectedChatId,
		int expectedContactId) {
	const auto chat = CoreProfileSingleChatFromReply(reply, expectedChatId);
	if (!chat) {
		return std::nullopt;
	}
	const auto peer = CoreExactPeerContactId(*chat);
	if (!peer || *peer != expectedContactId) {
		return std::nullopt;
	}
	return chat;
}

[[nodiscard]] inline bool CoreProfileFullChatIdPresent(
		const QJsonObject &reply,
		int expectedChatId) {
	if (!CoreProfileRealId(expectedChatId)
		|| reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return false;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return false;
	}
	return CoreJsonIdMatches(value.toObject(), std::uint64_t(expectedChatId));
}

[[nodiscard]] inline bool CoreProfileShouldUnselectChat(
		const CoreProfileResult &result,
		int account,
		int currentAccount,
		std::optional<int> activeChatId) {
	return CoreProfileSucceeded(result)
		&& account > 0
		&& account == currentAccount
		&& CoreProfileRealId(result.chatId)
		&& activeChatId
		&& *activeChatId == result.chatId
		&& (result.chatAcknowledged || result.chatWrote);
}

template <typename State>
[[nodiscard]] inline auto CoreProfileMakeCall(const std::shared_ptr<State> &state) {
	return [weak = std::weak_ptr<State>(state)](
			QString method,
			QJsonArray args,
			DeltaRpc::Callback callback) {
		const auto locked = weak.lock();
		if (!locked || !locked->valid()) {
			return;
		}
		auto request = locked->request;
		request(
			std::move(method),
			std::move(args),
			[locked, callback = std::move(callback)](QJsonObject reply) {
				if (!locked->valid()) {
					return;
				}
				auto done = callback;
				done(std::move(reply));
			});
	};
}

inline void ResolveCoreProfileContact(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		std::optional<int> senderContactId,
		std::optional<int> chatId,
		std::function<void(CoreProfileResult)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		std::function<void(CoreProfileResult)> done;
		bool finished = false;

		void finish(CoreProfileResult result) {
			if (finished) {
				return;
			}
			finished = true;
			auto complete = std::move(done);
			done = {};
			request = {};
			if (complete) {
				complete(std::move(result));
			}
		}

		void deny(QString error) {
			CoreProfileResult result;
			result.outcome = CoreProfileOutcome::Denied;
			result.error = std::move(error);
			finish(std::move(result));
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->done = std::move(done);

	const auto call = CoreProfileMakeCall(state);

	const auto takeContact = [state](std::optional<CoreProfileContact> contact, int chatId = 0) {
		if (!contact) {
			state->deny(kCoreProfileUnreadable);
			return;
		}
		CoreProfileResult result;
		result.outcome = CoreProfileOutcome::Verified;
		result.contactId = contact->id;
		result.chatId = chatId;
		result.contact = std::move(*contact);
		state->finish(std::move(result));
	};

	if (account <= 0) {
		state->deny(kCoreProfileUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	if (senderContactId) {
		if (!CoreProfileRealId(*senderContactId)) {
			state->deny(kCoreProfileUnavailable);
			return;
		}
		call(QStringLiteral("get_contact"),
			{ account, *senderContactId },
			[state, takeContact, id = *senderContactId](QJsonObject reply) {
				takeContact(CoreProfileContactFromReply(reply, id));
			});
		return;
	}

	if (!chatId || !CoreProfileRealId(*chatId)) {
		state->deny(kCoreProfileUnavailable);
		return;
	}
	call(QStringLiteral("get_full_chat_by_id"),
		{ account, *chatId },
		[state, call, takeContact, chat = *chatId](QJsonObject reply) {
			const auto parsed = CoreProfileSingleChatFromReply(reply, chat);
			const auto contact = parsed ? CoreExactPeerContactId(*parsed) : std::nullopt;
			if (!contact) {
				state->deny(kCoreProfileUnavailable);
				return;
			}
			call(QStringLiteral("get_contacts_by_ids"),
				{ state->account, QJsonArray{ *contact } },
				[takeContact, id = *contact, chat](QJsonObject reply) {
					takeContact(CoreProfileContactFromMapReply(reply, id), chat);
				});
		});
}

inline void ReadCoreEncryptionInfo(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		std::optional<int> contactId,
		std::optional<int> chatId,
		std::function<void(CoreProfileResult)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		std::function<void(CoreProfileResult)> done;
		bool finished = false;

		void finish(CoreProfileResult result) {
			if (finished) {
				return;
			}
			finished = true;
			auto complete = std::move(done);
			done = {};
			request = {};
			if (complete) {
				complete(std::move(result));
			}
		}

		void deny(QString error) {
			CoreProfileResult result;
			result.outcome = CoreProfileOutcome::Denied;
			result.error = std::move(error);
			finish(std::move(result));
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->done = std::move(done);

	const auto call = CoreProfileMakeCall(state);

	if (account <= 0) {
		state->deny(kCoreEncryptionUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	if (contactId) {
		if (!CoreProfileRealId(*contactId)) {
			state->deny(kCoreEncryptionUnavailable);
			return;
		}
		call(QStringLiteral("get_contact_encryption_info"),
			{ account, *contactId },
			[state](QJsonObject reply) {
				const auto text = CoreEncryptionTextFromReply(reply);
				if (!text) {
					state->deny(kCoreEncryptionUnreadable);
					return;
				}
				CoreProfileResult result;
				result.outcome = CoreProfileOutcome::Verified;
				result.text = *text;
				state->finish(std::move(result));
			});
		return;
	}

	if (!chatId || !CoreProfileRealId(*chatId)) {
		state->deny(kCoreEncryptionUnavailable);
		return;
	}
	call(QStringLiteral("get_chat_encryption_info"),
		{ account, *chatId },
		[state](QJsonObject reply) {
			const auto text = CoreEncryptionTextFromReply(reply);
			if (!text) {
				state->deny(kCoreEncryptionUnreadable);
				return;
			}
			CoreProfileResult result;
			result.outcome = CoreProfileOutcome::Verified;
			result.text = *text;
			state->finish(std::move(result));
		});
}

inline void ChangeCoreContactName(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int contactId,
		QString name,
		std::function<void(CoreProfileResult)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		int contact = 0;
		QString name;
		std::function<void(CoreProfileResult)> done;
		bool finished = false;
		bool attempted = false;
		bool wrote = false;
		bool acknowledged = false;

		void finish(CoreProfileOutcome outcome, QString error, CoreProfileContact contact = {}) {
			if (finished) {
				return;
			}
			finished = true;
			CoreProfileResult result;
			result.outcome = outcome;
			result.attempted = attempted;
			result.wrote = wrote;
			result.acknowledged = acknowledged;
			result.contactId = contact.id ? contact.id : this->contact;
			result.contact = std::move(contact);
			result.error = std::move(error);
			auto complete = std::move(done);
			done = {};
			request = {};
			if (complete) {
				complete(std::move(result));
			}
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->contact = contactId;
	state->name = std::move(name).trimmed();
	state->done = std::move(done);

	const auto call = CoreProfileMakeCall(state);

	if (account <= 0 || !CoreProfileRealId(contactId)) {
		state->finish(CoreProfileOutcome::Denied, kCoreProfileUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	call(QStringLiteral("get_contact"),
		{ account, contactId },
		[state, call](QJsonObject reply) {
			const auto before = CoreProfileContactFromReply(reply, state->contact);
			if (!before) {
				state->finish(CoreProfileOutcome::Denied, kCoreProfileUnreadable);
				return;
			}
			call(QStringLiteral("change_contact_name"),
				{ state->account, state->contact, state->name },
				[state, call](QJsonObject writeReply) {
					state->attempted = true;
					state->acknowledged = CoreProfileRpcNull(writeReply);
					call(QStringLiteral("get_contact"),
						{ state->account, state->contact },
						[state](QJsonObject afterReply) {
							const auto after = CoreProfileContactFromReply(
								afterReply,
								state->contact);
							if (after && after->name == state->name) {
								state->wrote = true;
								state->finish(
									CoreProfileOutcome::Verified,
									{},
									*after);
								return;
							}
							if (after) {
								state->finish(
									state->acknowledged
										? CoreProfileOutcome::Unknown
										: CoreProfileOutcome::Denied,
									state->acknowledged
										? kCoreProfileUnverified
										: kCoreProfileUnconfirmed,
									*after);
								return;
							}
							state->finish(
								CoreProfileOutcome::Unknown,
								kCoreProfileUnverified);
						});
				});
		});
}

inline void DeleteCoreProfileContact(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int contactId,
		std::function<void(CoreProfileResult)> done,
		bool deleteChat = false,
		std::optional<int> reviewedChatId = std::nullopt) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		int contact = 0;
		std::function<void(CoreProfileResult)> done;
		bool finished = false;
		bool attempted = false;
		bool wrote = false;
		bool acknowledged = false;
		int chatId = 0;
		bool chatAttempted = false;
		bool chatWrote = false;
		bool chatAcknowledged = false;

		void finish(CoreProfileOutcome outcome, QString error) {
			if (finished) {
				return;
			}
			finished = true;
			CoreProfileResult result;
			result.outcome = outcome;
			result.attempted = attempted;
			result.wrote = wrote;
			result.acknowledged = acknowledged;
			result.contactId = contact;
			result.chatId = chatId;
			result.chatAttempted = chatAttempted;
			result.chatWrote = chatWrote;
			result.chatAcknowledged = chatAcknowledged;
			result.error = std::move(error);
			auto complete = std::move(done);
			done = {};
			request = {};
			if (complete) {
				complete(std::move(result));
			}
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->contact = contactId;
	state->done = std::move(done);

	const auto call = CoreProfileMakeCall(state);

	if (account <= 0 || !CoreProfileRealId(contactId)) {
		state->finish(CoreProfileOutcome::Denied, kCoreProfileUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	// WHY: Core hides contacts that still have chats; get_contact stays
	// readable, so key/address lists are the deletion authority.
	const auto removeContact = std::make_shared<std::function<void()>>();
	*removeContact = [state, call] {
		call(QStringLiteral("delete_contact"),
			{ state->account, state->contact },
			[state, call](QJsonObject writeReply) {
				state->attempted = true;
				state->acknowledged = CoreProfileRpcNull(writeReply);
				const auto hidden = [state](const QJsonObject &reply) {
					const auto ids = CoreProfileContactIdsFromListReply(reply);
					if (!ids) {
						return std::optional<bool>();
					}
					return std::optional<bool>(!ids->contains(state->contact));
				};
				const auto listed = [state] {
					state->finish(
						state->acknowledged
							? CoreProfileOutcome::Unknown
							: CoreProfileOutcome::Denied,
						state->acknowledged
							? kCoreProfileUnverified
							: kCoreProfileUnconfirmed);
				};
				const auto unread = [state] {
					if (state->acknowledged) {
						state->wrote = true;
						state->finish(CoreProfileOutcome::Acknowledged, {});
						return;
					}
					state->finish(
						CoreProfileOutcome::Unknown,
						kCoreProfileUnverified);
				};
				call(QStringLiteral("get_contacts"),
					CoreProfileContactListArgs(
						state->account,
						kCoreContactListDefault),
					[state, call, hidden, listed, unread](QJsonObject reply0) {
						const auto d0 = hidden(reply0);
						if (!d0) {
							unread();
							return;
						} else if (!*d0) {
							listed();
							return;
						}
						call(QStringLiteral("get_contacts"),
							CoreProfileContactListArgs(
								state->account,
								kCoreContactListAddSelf),
							[state, call, hidden, listed, unread](QJsonObject reply2) {
								const auto d2 = hidden(reply2);
								if (!d2) {
									unread();
									return;
								} else if (!*d2) {
									listed();
									return;
								}
								call(QStringLiteral("get_contacts"),
									CoreProfileContactListArgs(
										state->account,
										kCoreContactListAddress),
									[state, hidden, listed, unread](QJsonObject reply4) {
										const auto d4 = hidden(reply4);
										if (!d4) {
											unread();
											return;
										} else if (!*d4) {
											listed();
											return;
										}
										state->wrote = true;
										state->finish(CoreProfileOutcome::Verified, {});
									});
							});
					});
			});
	};

	const auto invokeRemove = [removeContact] {
		auto action = *removeContact;
		action();
	};

	const auto deleteReviewedChat = [state, call, invokeRemove](int chatId) {
		state->chatId = chatId;
		call(QStringLiteral("get_full_chat_by_id"),
			{ state->account, chatId },
			[state, call, invokeRemove, chatId](QJsonObject reviewReply) {
				if (!CoreProfileReviewedSingleChat(
						reviewReply,
						chatId,
						state->contact)) {
					if (CoreProfileFullChatIdPresent(reviewReply, chatId)) {
						state->finish(
							CoreProfileOutcome::Denied,
							kCoreProfileUnavailable);
						return;
					}
					state->finish(
						CoreProfileOutcome::Unknown,
						kCoreProfileUnreadable);
					return;
				}
				call(QStringLiteral("delete_chat"),
					{ state->account, chatId },
					[state, call, invokeRemove, chatId](QJsonObject chatReply) {
						state->attempted = true;
						state->chatAttempted = true;
						state->chatAcknowledged = CoreProfileRpcNull(chatReply);
						call(QStringLiteral("get_full_chat_by_id"),
							{ state->account, chatId },
							[state, invokeRemove, chatId](QJsonObject afterReply) {
								if (CoreProfileFullChatIdPresent(afterReply, chatId)) {
									state->finish(
										state->chatAcknowledged
											? CoreProfileOutcome::Unknown
											: CoreProfileOutcome::Denied,
										state->chatAcknowledged
											? kCoreProfileUnverified
											: kCoreProfileUnconfirmed);
									return;
								}
								if (!state->chatAcknowledged) {
									state->finish(
										CoreProfileOutcome::Unknown,
										kCoreProfileUnverified);
									return;
								}
								state->chatWrote = true;
								invokeRemove();
							});
					});
			});
	};

	call(QStringLiteral("get_contact"),
		{ account, contactId },
		[state, call, deleteChat, reviewedChatId, invokeRemove, deleteReviewedChat](
				QJsonObject reply) {
			const auto before = CoreProfileContactFromReply(reply, state->contact);
			if (!before) {
				state->finish(CoreProfileOutcome::Denied, kCoreProfileUnreadable);
				return;
			}
			if (!deleteChat) {
				invokeRemove();
				return;
			}
			if (reviewedChatId) {
				if (!CoreProfileRealId(*reviewedChatId)) {
					state->finish(
						CoreProfileOutcome::Denied,
						kCoreProfileUnavailable);
					return;
				}
				deleteReviewedChat(*reviewedChatId);
				return;
			}
			call(QStringLiteral("get_chat_id_by_contact_id"),
				{ state->account, state->contact },
				[state, deleteReviewedChat](QJsonObject lookupReply) {
					const auto lookup = CoreProfileChatIdLookup(lookupReply);
					if (lookup.kind == CoreProfileLookup::Found) {
						deleteReviewedChat(lookup.chatId);
						return;
					}
					if (lookup.kind == CoreProfileLookup::Missing) {
						state->finish(
							CoreProfileOutcome::Unknown,
							kCoreProfileChatAmbiguous);
						return;
					}
					state->finish(
						CoreProfileOutcome::Unknown,
						kCoreProfileUnreadable);
				});
		});
}

void ShowCoreChatEncryptionInfo(
	Window::SessionController *controller,
	int chat);
void ShowCorePeerEncryptionInfo(
	Window::SessionController *controller,
	std::uint64_t userId);
void ShowCoreContactNameBox(
	Window::SessionController *controller,
	std::uint64_t userId,
	bool adding);
void FillCoreContactNameBox(
	Ui::GenericBox *box,
	Window::SessionController *controller,
	std::uint64_t userId,
	bool adding);
void ShowCoreContactDeleteBox(
	Window::SessionController *controller,
	std::uint64_t userId);

} // namespace Delta
