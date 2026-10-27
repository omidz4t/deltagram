#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "contact_blocking.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Delta {

inline const QString kOpenContactUnavailable = QStringLiteral(
	"This conversation cannot be opened.");
inline const QString kOpenContactUnreadable = QStringLiteral(
	"The contact conversation could not be checked. Refresh before trying again.");
inline const QString kOpenContactBlocked = QStringLiteral(
	"This contact is blocked.");
inline const QString kOpenContactUnconfirmed = QStringLiteral(
	"The conversation could not be opened. Refresh before trying again.");

struct OpenContactChat {
	int id = 0;
	bool isContactRequest = false;
	bool isSelfTalk = false;
	std::vector<int> contactIds;
};

[[nodiscard]] inline bool CoreOpenChatId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline bool CoreOpenSelfContact(int contact) {
	return contact == 1;
}

[[nodiscard]] inline bool CoreOpenPeerContact(int contact) {
	return contact > 9 && CoreIdFitsInt(std::uint64_t(contact));
}

[[nodiscard]] inline std::optional<int> CoreOpenExactInt(const QJsonValue &value) {
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

enum class CoreOpenLookup : unsigned char {
	Invalid,
	Missing,
	Found,
};

struct CoreOpenLookupResult {
	CoreOpenLookup kind = CoreOpenLookup::Invalid;
	int chatId = 0;
};

[[nodiscard]] inline CoreOpenLookupResult CoreOpenChatIdLookup(
		const QJsonObject &reply) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return {};
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (value.isNull()) {
		return { CoreOpenLookup::Missing, 0 };
	}
	const auto id = CoreOpenExactInt(value);
	if (!id || !CoreOpenChatId(*id)) {
		return {};
	}
	return { CoreOpenLookup::Found, *id };
}

[[nodiscard]] inline std::optional<OpenContactChat> CoreOpenContactChat(
		const QJsonObject &json,
		int expectedChatId) {
	if (!CoreOpenChatId(expectedChatId) || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	if (!CoreJsonIdMatches(json, std::uint64_t(expectedChatId))) {
		return std::nullopt;
	}
	const auto type = CoreJsonKnownChatType(json.value(QStringLiteral("chatType")));
	if (!type || *type != CoreChatType::Single) {
		return std::nullopt;
	}
	const auto requestFlag = json.value(QStringLiteral("isContactRequest"));
	if (!requestFlag.isBool()) {
		return std::nullopt;
	}
	const auto selfTalk = json.value(QStringLiteral("isSelfTalk"));
	if (!selfTalk.isBool()) {
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
	OpenContactChat result;
	result.id = expectedChatId;
	result.isContactRequest = requestFlag.toBool();
	result.isSelfTalk = selfTalk.toBool();
	auto seen = QSet<int>();
	for (const auto &entry : contacts.toArray()) {
		const auto id = CoreOpenExactInt(entry);
		if (!id || seen.contains(*id)) {
			return std::nullopt;
		}
		seen.insert(*id);
		result.contactIds.push_back(*id);
	}
	return result;
}

[[nodiscard]] inline std::optional<OpenContactChat> CoreOpenContactChatFromReply(
		const QJsonObject &reply,
		int expectedChatId) {
	if (reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	return CoreOpenContactChat(value.toObject(), expectedChatId);
}

[[nodiscard]] inline bool CoreOpenContactChatFits(
		const OpenContactChat &chat,
		int contact,
		bool created) {
	if (!CoreOpenChatId(chat.id) || chat.contactIds.size() != 1) {
		return false;
	}
	const auto peer = chat.contactIds.front();
	if (CoreOpenSelfContact(contact)) {
		return chat.isSelfTalk
			&& !chat.isContactRequest
			&& peer == 1;
	}
	if (!CoreOpenPeerContact(contact)
		|| chat.isSelfTalk
		|| peer != contact
		|| (created && chat.isContactRequest)) {
		return false;
	}
	return true;
}

// WHY: lookup null is missing or hidden Yes; a request can also appear
// between that read and create, and create is not retried.
inline void OpenCoreContactChat(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int contact,
		std::function<void(int, QString)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		int contact = 0;
		std::function<void(int, QString)> done;
		bool finished = false;
		bool wrote = false;

		void finish(int chatId, QString error) {
			if (finished) {
				return;
			}
			finished = true;
			if (!error.isEmpty()) {
				chatId = 0;
			}
			if (done) {
				done(chatId, std::move(error));
			}
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->contact = contact;
	state->done = std::move(done);

	const auto call = [state](QString method, QJsonArray args, DeltaRpc::Callback callback) {
		if (!state->valid()) {
			return;
		}
		state->request(std::move(method), std::move(args),
			[state, callback = std::move(callback)](QJsonObject reply) {
				if (state->valid()) {
					callback(std::move(reply));
				}
			});
	};

	if (account <= 0
		|| (!CoreOpenSelfContact(contact) && !CoreOpenPeerContact(contact))) {
		state->finish(0, kOpenContactUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	const auto createOnce = [state, call] {
		if (state->wrote || !state->valid()) {
			return;
		}
		state->wrote = true;
		call(QStringLiteral("create_chat_by_contact_id"),
			{ state->account, state->contact },
			[state, call](QJsonObject reply) {
				if (reply.contains(QStringLiteral("error"))
					|| !reply.contains(QStringLiteral("result"))) {
					state->finish(0, kOpenContactUnconfirmed);
					return;
				}
				const auto created = CoreOpenExactInt(
					reply.value(QStringLiteral("result")));
				if (!created || !CoreOpenChatId(*created)) {
					state->finish(0, kOpenContactUnconfirmed);
					return;
				}
				call(QStringLiteral("get_full_chat_by_id"),
					{ state->account, *created },
					[state, chatId = *created](QJsonObject reply) {
						const auto chat = CoreOpenContactChatFromReply(
							reply,
							chatId);
						if (!chat
							|| !CoreOpenContactChatFits(
								*chat,
								state->contact,
								true)) {
							state->finish(0, kOpenContactUnconfirmed);
							return;
						}
						state->finish(chat->id, {});
					});
			});
	};

	call(QStringLiteral("get_chat_id_by_contact_id"),
		{ account, contact },
		[state, call, createOnce](QJsonObject reply) {
			const auto lookup = CoreOpenChatIdLookup(reply);
			if (lookup.kind == CoreOpenLookup::Invalid) {
				state->finish(0, kOpenContactUnreadable);
				return;
			}
			if (lookup.kind == CoreOpenLookup::Found) {
				call(QStringLiteral("get_full_chat_by_id"),
					{ state->account, lookup.chatId },
					[state, chatId = lookup.chatId](QJsonObject reply) {
						const auto chat = CoreOpenContactChatFromReply(
							reply,
							chatId);
						if (!chat
							|| !CoreOpenContactChatFits(
								*chat,
								state->contact,
								false)) {
							state->finish(0, kOpenContactUnreadable);
							return;
						}
						state->finish(chat->id, {});
					});
				return;
			}
			if (CoreOpenSelfContact(state->contact)) {
				createOnce();
				return;
			}
			call(QStringLiteral("get_contact"),
				{ state->account, state->contact },
				[state, createOnce](QJsonObject reply) {
					const auto blocked = CoreContactBlocked(
						reply,
						state->contact);
					if (!blocked) {
						state->finish(0, kOpenContactUnreadable);
						return;
					}
					if (*blocked) {
						state->finish(0, kOpenContactBlocked);
						return;
					}
					createOnce();
				});
		});
}

} // namespace Delta
