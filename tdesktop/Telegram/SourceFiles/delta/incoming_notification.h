#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <optional>

namespace Delta {

inline constexpr int kIncomingFreshState = 10;
inline constexpr int kIncomingDraftState = 19;

inline const QString kIncomingNameUnavailable = QStringLiteral(
	"The conversation name could not be confirmed.");

enum class IncomingEventKind : unsigned char {
	Existing,
	Incoming,
};

enum class IncomingApplyKind : unsigned char {
	Skip,
	Existing,
	Unread,
};

[[nodiscard]] inline IncomingEventKind IncomingEventKindFromName(
		const QString &kind) {
	return kind == QStringLiteral("IncomingMsg")
		? IncomingEventKind::Incoming
		: IncomingEventKind::Existing;
}

[[nodiscard]] inline std::optional<int> IncomingExactInt(
		const QJsonValue &value) {
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

[[nodiscard]] inline int IncomingEventFieldId(
		const QJsonObject &event,
		const QString &camel,
		const QString &snake) {
	for (const auto &key : { camel, snake }) {
		if (const auto id = IncomingExactInt(event.value(key))) {
			return *id;
		}
	}
	return 0;
}

[[nodiscard]] inline int IncomingEventChatId(const QJsonObject &event) {
	return IncomingEventFieldId(
		event,
		QStringLiteral("chatId"),
		QStringLiteral("chat_id"));
}

[[nodiscard]] inline int IncomingEventMsgId(const QJsonObject &event) {
	return IncomingEventFieldId(
		event,
		QStringLiteral("msgId"),
		QStringLiteral("msg_id"));
}

[[nodiscard]] inline bool IncomingNameUsable(const QString &name) {
	return !name.trimmed().isEmpty();
}

[[nodiscard]] inline bool IncomingExistingTextFastPath(
		IncomingApplyKind decision,
		bool hasPhotoOrDocument) {
	return decision == IncomingApplyKind::Existing && !hasPhotoOrDocument;
}

[[nodiscard]] inline IncomingApplyKind ClassifyIncomingMessage(
		IncomingEventKind origin,
		int expectedChat,
		int expectedMsg,
		const QJsonObject &message) {
	if (expectedChat <= 0
		|| expectedMsg <= 0
		|| !CoreJsonIdMatches(message, std::uint64_t(expectedMsg))) {
		return IncomingApplyKind::Skip;
	}
	const auto chat = IncomingExactInt(
		message.value(QStringLiteral("chatId")));
	const auto state = IncomingExactInt(
		message.value(QStringLiteral("state")));
	const auto info = message.value(QStringLiteral("isInfo"));
	if (!chat
		|| *chat != expectedChat
		|| !state
		|| !info.isBool()) {
		return IncomingApplyKind::Skip;
	}
	if (*state == kIncomingDraftState) {
		return IncomingApplyKind::Skip;
	}
	if (origin == IncomingEventKind::Incoming
		&& !info.toBool()
		&& *state == kIncomingFreshState) {
		return IncomingApplyKind::Unread;
	}
	return IncomingApplyKind::Existing;
}

[[nodiscard]] inline QString IncomingChatNameFromReply(
		const QJsonObject &reply,
		int chatId) {
	if (chatId <= 0 || reply.contains(QStringLiteral("error"))) {
		return {};
	}
	const auto result = reply.value(QStringLiteral("result"));
	if (!result.isObject()) {
		return {};
	}
	const auto json = result.toObject();
	if (!CoreJsonIdMatches(json, std::uint64_t(chatId))) {
		return {};
	}
	const auto name = json.value(QStringLiteral("name"));
	if (!name.isString()) {
		return {};
	}
	const auto text = name.toString().trimmed();
	return IncomingNameUsable(text) ? text : QString();
}

inline void ReadIncomingChatName(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		std::function<void(QString)> done) {
	if (account <= 0 || chat <= 0) {
		done({});
		return;
	}
	request(
		QStringLiteral("get_basic_chat_info"),
		{ account, chat },
		scope.bind([done = std::move(done), chat](QJsonObject reply) {
			done(IncomingChatNameFromReply(reply, chat));
		}));
}

} // namespace Delta
