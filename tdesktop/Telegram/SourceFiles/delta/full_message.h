#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace Delta {

inline const QString kFullMessageUnavailable = QStringLiteral(
	"This message cannot be shown in full.");
inline const QString kFullMessageUnreadable = QStringLiteral(
	"The full message could not be read.");
inline const QString kFullMessageEmpty = QStringLiteral(
	"The full message is empty.");
inline const QString kFullMessageMalformed = QStringLiteral(
	"The full message could not be displayed.");

enum class FullMessageOutcome : unsigned char {
	Ready,
	Unavailable,
	Unreadable,
	Empty,
	Malformed,
};

struct FullMessageResult {
	FullMessageOutcome outcome = FullMessageOutcome::Unavailable;
	QString html;
	QString fallback;
	QString error;
	QString title;
};

inline constexpr int kFullMessageTitleLimit = 80;

[[nodiscard]] inline bool FullMessageRealId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline std::optional<int> FullMessageExactInt(
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

[[nodiscard]] inline std::optional<int> FullMessageChatFromMetadata(
		const QJsonObject &json) {
	const auto chat = FullMessageExactInt(
		json.value(QStringLiteral("chatId")));
	if (!chat || !FullMessageRealId(*chat)) {
		return std::nullopt;
	}
	return chat;
}

[[nodiscard]] inline QString FullMessageFallbackFromMetadata(
		const QJsonObject &json) {
	const auto text = json.value(QStringLiteral("text"));
	return text.isString() ? text.toString() : QString();
}

[[nodiscard]] inline bool FullMessageFlag(
		const QJsonObject &json,
		int messageId) {
	if (!FullMessageRealId(messageId)
		|| !CoreJsonIdMatches(json, std::uint64_t(messageId))) {
		return false;
	}
	if (!FullMessageChatFromMetadata(json)) {
		return false;
	}
	const auto html = json.value(QStringLiteral("hasHtml"));
	return html.isBool() && html.toBool();
}

[[nodiscard]] inline bool FullMessageSessionAllows(
		bool ownsSession,
		const QJsonObject &json,
		int messageId) {
	return ownsSession && FullMessageFlag(json, messageId);
}

[[nodiscard]] inline QString FullMessageTitleFromMetadata(
		const QJsonObject &json) {
	const auto value = json.value(QStringLiteral("subject"));
	if (!value.isString()) {
		return {};
	}
	auto text = value.toString();
	text.replace(QChar(u'\n'), QChar(u' '));
	text.replace(QChar(u'\r'), QChar(u' '));
	text.replace(QChar(u'\t'), QChar(u' '));
	QString cleaned;
	cleaned.reserve(text.size());
	for (const auto ch : text) {
		// WHY: Subject is untrusted; drop bidi/format controls, keep printable text.
		if (!ch.isPrint()) {
			continue;
		}
		cleaned.append(ch);
		if (cleaned.size() >= kFullMessageTitleLimit) {
			break;
		}
	}
	return cleaned.simplified();
}

[[nodiscard]] inline bool FullMessageMetadataFromReply(
		const QJsonObject &reply,
		int messageId,
		int expectedChat) {
	if (reply.contains(QStringLiteral("error"))
		|| !FullMessageRealId(messageId)
		|| !FullMessageRealId(expectedChat)) {
		return false;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return false;
	}
	const auto json = value.toObject();
	if (!CoreJsonIdMatches(json, std::uint64_t(messageId))) {
		return false;
	}
	const auto chat = FullMessageExactInt(
		json.value(QStringLiteral("chatId")));
	if (!chat || *chat != expectedChat) {
		return false;
	}
	const auto html = json.value(QStringLiteral("hasHtml"));
	return html.isBool() && html.toBool();
}

[[nodiscard]] inline FullMessageResult FullMessageHtmlFromReply(
		const QJsonObject &reply) {
	FullMessageResult result;
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		result.outcome = FullMessageOutcome::Unreadable;
		result.error = kFullMessageUnreadable;
		return result;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (value.isNull()) {
		result.outcome = FullMessageOutcome::Empty;
		result.error = kFullMessageEmpty;
		return result;
	}
	if (!value.isString()) {
		result.outcome = FullMessageOutcome::Malformed;
		result.error = kFullMessageMalformed;
		return result;
	}
	const auto html = value.toString();
	if (html.isEmpty()) {
		result.outcome = FullMessageOutcome::Empty;
		result.error = kFullMessageEmpty;
		return result;
	}
	result.outcome = FullMessageOutcome::Ready;
	result.html = html;
	return result;
}

inline void ReadFullMessage(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int messageId,
		int expectedChat,
		std::function<void(FullMessageResult)> done) {
	const auto deny = [&] {
		done({
			FullMessageOutcome::Unavailable,
			{},
			{},
			kFullMessageUnavailable,
		});
	};
	if (account <= 0
		|| !FullMessageRealId(messageId)
		|| !FullMessageRealId(expectedChat)) {
		deny();
		return;
	}
	if (!scope.guard()()) {
		return;
	}
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> guard;
		std::function<void(FullMessageResult)> finish;
		QString title;

		[[nodiscard]] bool valid() const {
			return !guard || guard();
		}
		void complete(FullMessageResult result) {
			if (!valid()) {
				return;
			}
			if (result.title.isEmpty()) {
				result.title = title;
			}
			auto done = std::move(finish);
			finish = nullptr;
			if (done) {
				done(std::move(result));
			}
		}
	};
	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->guard = scope.guard();
	state->finish = std::move(done);
	const auto call = [weak = std::weak_ptr<State>(state)](
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
	call(
		QStringLiteral("get_message"),
		{ account, messageId },
		[=](QJsonObject reply) {
			if (reply.contains(QStringLiteral("error"))) {
				state->complete({
					FullMessageOutcome::Unreadable,
					{},
					{},
					kFullMessageUnreadable,
				});
				return;
			}
			if (!FullMessageMetadataFromReply(
					reply,
					messageId,
					expectedChat)) {
				state->complete({
					FullMessageOutcome::Unavailable,
					{},
					{},
					kFullMessageUnavailable,
				});
				return;
			}
			const auto metadata = reply.value(QStringLiteral("result")).toObject();
			const auto fallback = FullMessageFallbackFromMetadata(metadata);
			state->title = FullMessageTitleFromMetadata(metadata);
			if (!state->valid()) {
				return;
			}
			call(
				QStringLiteral("get_message_html"),
				{ account, messageId },
				[=](QJsonObject htmlReply) {
					auto result = FullMessageHtmlFromReply(
						std::move(htmlReply));
					result.fallback = fallback;
					state->complete(std::move(result));
				});
		});
}

} // namespace Delta
