#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "contact_blocking.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Delta {

// FullChat.canSend is not a request. Group block_chat deletes without
// blocking members, so an ack is success and a lost-ack get_full error is not.

inline const QString kContactRequestUnavailable = QStringLiteral(
	"This conversation cannot be accepted or declined.");
inline const QString kContactRequestStale = QStringLiteral(
	"This is not an open contact request. Refresh before trying again.");
inline const QString kContactRequestNeedsDeletionConfirm = QStringLiteral(
	"Deleting this group request needs explicit confirmation.");
inline const QString kContactRequestCannotBlockChannel = QStringLiteral(
	"This channel cannot be blocked.");
inline const QString kContactRequestUnconfirmed = QStringLiteral(
	"The request change was not confirmed. Refresh before trying again.");
inline const QString kContactRequestUnverified = QStringLiteral(
	"The request change could not be verified. Refresh the conversation before trying again.");

enum class ContactRequestAction : unsigned char {
	Accept,
	Decline,
};

enum class ContactRequestOutcome : unsigned char {
	Denied,
	Verified,
	Acknowledged,
	Unknown,
};

struct ContactRequestChat {
	int id = 0;
	CoreChatType type = CoreChatType::Single;
	bool isContactRequest = false;
	std::vector<int> contactIds;
	std::vector<int> peerContactIds;
};

struct ContactRequestDeletionReview {
	int chatId = 0;
	CoreChatType type = CoreChatType::Single;
	bool confirmed = false;
};

struct ContactRequestResult {
	ContactRequestOutcome outcome = ContactRequestOutcome::Denied;
	bool acknowledged = false;
	bool wrote = false;
	QString error;
};

[[nodiscard]] inline bool ContactRequestSucceeded(const ContactRequestResult &result) {
	return result.outcome == ContactRequestOutcome::Verified
		|| result.outcome == ContactRequestOutcome::Acknowledged;
}

[[nodiscard]] inline bool CoreRpcReturnedNull(const QJsonObject &reply) {
	return !reply.contains(QStringLiteral("error"))
		&& reply.contains(QStringLiteral("result"))
		&& reply.value(QStringLiteral("result")).isNull();
}

[[nodiscard]] inline bool CoreRealChatId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline std::optional<int> CoreSingleRequestPeer(
		const ContactRequestChat &chat) {
	if (chat.type != CoreChatType::Single || chat.contactIds.size() != 1) {
		return std::nullopt;
	}
	const auto id = chat.contactIds.front();
	if (id <= 9) {
		return std::nullopt;
	}
	return id;
}

[[nodiscard]] inline std::optional<ContactRequestChat> CoreContactRequestChat(
		const QJsonObject &json,
		int expectedChatId) {
	if (!CoreRealChatId(expectedChatId) || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	if (!CoreJsonIdMatches(json, std::uint64_t(expectedChatId))) {
		return std::nullopt;
	}
	const auto requestFlag = json.value(QStringLiteral("isContactRequest"));
	if (!requestFlag.isBool()) {
		return std::nullopt;
	}
	const auto type = CoreJsonKnownChatType(json.value(QStringLiteral("chatType")));
	if (!type) {
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
	ContactRequestChat result;
	result.id = expectedChatId;
	result.type = *type;
	result.isContactRequest = requestFlag.toBool();
	auto seen = QSet<int>();
	for (const auto &entry : contacts.toArray()) {
		if (!entry.isDouble()) {
			return std::nullopt;
		}
		const auto id = entry.toInt();
		if (id <= 0
			|| entry.toDouble() != double(id)
			|| !CoreIdFitsInt(std::uint64_t(id))
			|| seen.contains(id)) {
			return std::nullopt;
		}
		seen.insert(id);
		result.contactIds.push_back(id);
		if (id > 9) {
			result.peerContactIds.push_back(id);
		}
	}
	return result;
}

[[nodiscard]] inline std::optional<ContactRequestChat> CoreContactRequestFromReply(
		const QJsonObject &reply,
		int expectedChatId) {
	if (reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	return CoreContactRequestChat(value.toObject(), expectedChatId);
}

inline void ReadCoreContactRequest(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		std::function<void(std::optional<ContactRequestChat>)> done) {
	if (account <= 0 || !CoreRealChatId(chat)) {
		done(std::nullopt);
		return;
	}
	if (!scope.guard()()) {
		return;
	}
	request(QStringLiteral("get_full_chat_by_id"), { account, chat },
		scope.bind([done = std::move(done), chat](QJsonObject reply) {
			done(CoreContactRequestFromReply(reply, chat));
		}));
}

inline void ApplyCoreContactRequest(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		ContactRequestAction action,
		ContactRequestDeletionReview review,
		std::function<void(ContactRequestResult)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		int chat = 0;
		ContactRequestAction action = ContactRequestAction::Accept;
		ContactRequestDeletionReview review;
		std::function<void(ContactRequestResult)> done;
		ContactRequestChat before;
		bool finished = false;
		bool wrote = false;
		bool acknowledged = false;

		void finish(ContactRequestOutcome outcome, QString error) {
			if (finished) {
				return;
			}
			finished = true;
			ContactRequestResult result;
			result.outcome = outcome;
			result.acknowledged = acknowledged;
			result.wrote = wrote;
			result.error = std::move(error);
			if (done) {
				done(std::move(result));
			}
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->chat = chat;
	state->action = action;
	state->review = review;
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

	if (account <= 0 || !CoreRealChatId(chat)) {
		state->finish(ContactRequestOutcome::Denied, kContactRequestUnavailable);
		return;
	}
	if (!state->valid()) {
		return;
	}

	call(QStringLiteral("get_full_chat_by_id"), { account, chat }, [state, call](QJsonObject reply) {
		const auto before = CoreContactRequestFromReply(reply, state->chat);
		if (!before || before->id != state->chat || !before->isContactRequest) {
			state->finish(ContactRequestOutcome::Denied, kContactRequestStale);
			return;
		}
		if (state->action == ContactRequestAction::Decline) {
			if (before->type == CoreChatType::OutBroadcast) {
				state->finish(ContactRequestOutcome::Denied, kContactRequestCannotBlockChannel);
				return;
			}
			if (before->type == CoreChatType::Group
				&& (!state->review.confirmed
					|| state->review.chatId != state->chat
					|| state->review.type != CoreChatType::Group)) {
				state->finish(ContactRequestOutcome::Denied, kContactRequestNeedsDeletionConfirm);
				return;
			}
			if (before->type == CoreChatType::Single && !CoreSingleRequestPeer(*before)) {
				state->finish(ContactRequestOutcome::Denied, kContactRequestStale);
				return;
			}
		}
		state->before = *before;
		const auto method = state->action == ContactRequestAction::Accept
			? QStringLiteral("accept_chat")
			: QStringLiteral("block_chat");
		call(method, { state->account, state->chat }, [state, call](QJsonObject writeReply) {
			state->wrote = true;
			state->acknowledged = CoreRpcReturnedNull(writeReply);

			call(QStringLiteral("get_full_chat_by_id"), { state->account, state->chat },
				[state, call](QJsonObject afterReply) {
					const auto after = CoreContactRequestFromReply(afterReply, state->chat);
					if (state->action == ContactRequestAction::Accept) {
						if (after
							&& after->type == state->before.type
							&& !after->isContactRequest) {
							state->finish(ContactRequestOutcome::Verified, {});
							return;
						}
						if (after && after->isContactRequest) {
							state->finish(
								ContactRequestOutcome::Denied,
								kContactRequestUnconfirmed);
							return;
						}
						state->finish(
							ContactRequestOutcome::Unknown,
							kContactRequestUnverified);
						return;
					}
					if (state->before.type == CoreChatType::Group) {
						if (after && after->isContactRequest) {
							state->finish(
								ContactRequestOutcome::Denied,
								kContactRequestUnconfirmed);
							return;
						}
						if (state->acknowledged) {
							state->finish(ContactRequestOutcome::Acknowledged, {});
							return;
						}
						state->finish(
							ContactRequestOutcome::Unknown,
							kContactRequestUnverified);
						return;
					}
					if (state->before.type == CoreChatType::Single) {
						if (after && after->isContactRequest) {
							state->finish(
								ContactRequestOutcome::Denied,
								kContactRequestUnconfirmed);
							return;
						}
						const auto peer = CoreSingleRequestPeer(state->before);
						if (!peer) {
							state->finish(
								ContactRequestOutcome::Unknown,
								kContactRequestUnverified);
							return;
						}
						call(QStringLiteral("get_contact"), { state->account, *peer },
							[state, id = *peer](QJsonObject reply) {
								const auto blocked = CoreContactBlocked(reply, id);
								if (!blocked) {
									state->finish(
										ContactRequestOutcome::Unknown,
										kContactRequestUnverified);
									return;
								}
								if (!*blocked) {
									state->finish(
										ContactRequestOutcome::Denied,
										kContactRequestUnconfirmed);
									return;
								}
								state->finish(ContactRequestOutcome::Verified, {});
							});
						return;
					}
					if (after
						&& after->type == state->before.type
						&& !after->isContactRequest) {
						if (state->acknowledged) {
							state->finish(ContactRequestOutcome::Verified, {});
							return;
						}
						state->finish(
							ContactRequestOutcome::Unknown,
							kContactRequestUnverified);
						return;
					}
					if (after && after->isContactRequest) {
						state->finish(
							ContactRequestOutcome::Denied,
							kContactRequestUnconfirmed);
						return;
					}
					state->finish(
						ContactRequestOutcome::Unknown,
						kContactRequestUnverified);
				});
		});
	});
}

inline void AcceptCoreContactRequest(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		std::function<void(ContactRequestResult)> done) {
	ApplyCoreContactRequest(
		std::move(request),
		scope,
		account,
		chat,
		ContactRequestAction::Accept,
		{},
		std::move(done));
}

inline void DeclineCoreContactRequest(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		ContactRequestDeletionReview review,
		std::function<void(ContactRequestResult)> done) {
	ApplyCoreContactRequest(
		std::move(request),
		scope,
		account,
		chat,
		ContactRequestAction::Decline,
		review,
		std::move(done));
}

} // namespace Delta
