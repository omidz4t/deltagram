#pragma once

#include "account_scope.h"
#include "delta_rpc.h"

namespace Delta {

// Core events can name hidden transport messages (for example reactions).
// Only IDs in Core's visible history may create a new conversation item.
inline void CheckMessageVisibility(
		std::shared_ptr<DeltaRpc> rpc,
		const AccountScope &scope,
		int accountId,
		int chatId,
		int messageId,
		std::function<void(bool)> done) {
	rpc->call(QStringLiteral("get_message_ids"),
		{ accountId, chatId, false, false }, scope.bind([=](QJsonObject reply) {
			const auto result = reply.value(QStringLiteral("result"));
			done(!reply.contains(QStringLiteral("error"))
				&& result.isArray()
				&& result.toArray().contains(messageId));
		}));
}

// A zero chatId permits lookup by account-wide message ID.
inline void ReadVisibleMessage(
		std::shared_ptr<DeltaRpc> rpc,
		const AccountScope &scope,
		int accountId,
		int chatId,
		int messageId,
		std::function<void(QJsonObject)> done) {
	const auto current = scope.guard();
	rpc->call(QStringLiteral("get_message"),
		{ accountId, messageId }, scope.bind([=](QJsonObject reply) {
			const auto message = reply.value(QStringLiteral("result")).toObject();
			const auto actualChat = message.value(QStringLiteral("chatId")).toInt();
			if (reply.contains(QStringLiteral("error"))
				|| message.value(QStringLiteral("id")).toInt() != messageId
				|| messageId <= 0 || actualChat <= 0
				|| (chatId && actualChat != chatId)
				|| message.value(QStringLiteral("state")).toInt() == 19) {
				done({});
				return;
			}
			rpc->call(QStringLiteral("get_message_ids"),
				{ accountId, actualChat, false, false },
				[=](QJsonObject listed) {
					if (!current()) return;
					const auto ids = listed.value(QStringLiteral("result"));
					done(!listed.contains(QStringLiteral("error"))
						&& ids.isArray() && ids.toArray().contains(messageId)
						? message : QJsonObject());
				});
		}));
}

} // namespace Delta
