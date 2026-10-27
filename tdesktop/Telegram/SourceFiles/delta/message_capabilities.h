#pragma once

#include "media_identity.h"

#include <QJsonObject>
#include <QString>

#include <optional>

namespace Delta {

inline bool CanEditCoreMessage(const QJsonObject &message) {
	return message.value(QStringLiteral("fromId")).toInt() == 1
		&& message.value(QStringLiteral("isInfo")).isBool()
		&& !message.value(QStringLiteral("isInfo")).toBool()
		&& message.value(QStringLiteral("hasHtml")).isBool()
		&& !message.value(QStringLiteral("hasHtml")).toBool()
		&& !message.value(QStringLiteral("viewType")).toString().isEmpty()
		&& message.value(QStringLiteral("viewType")).toString() != QStringLiteral("Call")
		&& !message.value(QStringLiteral("text")).toString().isEmpty();
}

[[nodiscard]] inline QString CoreOverrideSenderName(const QJsonObject &json) {
	const auto value = json.value(QStringLiteral("overrideSenderName"));
	return value.isString() ? value.toString() : QString();
}

[[nodiscard]] inline bool CoreHasOriginalMsgId(const QJsonObject &json) {
	const auto value = json.value(QStringLiteral("originalMsgId"));
	if (!value.isDouble()) {
		return false;
	}
	const auto id = value.toInt();
	return id > 0 && value.toDouble() == double(id);
}

[[nodiscard]] inline QString CoreAuthorDisplayName(
		const QString &displayName,
		const QString &override) {
	return override.isEmpty()
		? displayName
		: (QStringLiteral("~") + override);
}

// Same painted ~name is not enough: override vs none must not collapse.
[[nodiscard]] inline bool CoreAuthorPresentationsAttach(
		const QString &previousDisplayName,
		const QString &previousOverride,
		const QString &currentDisplayName,
		const QString &currentOverride) {
	if (previousOverride != currentOverride) {
		return false;
	}
	return CoreAuthorDisplayName(previousDisplayName, previousOverride)
		== CoreAuthorDisplayName(currentDisplayName, currentOverride);
}

struct CoreAuthorVisibility {
	std::optional<CoreChatType> chatType;
	bool hasOverride = false;
	bool originalMsgId = false;
	bool selfTalk = false;
	bool incoming = false;
	bool info = false;

};

// Unknown chatType is not Single or Group. Override still forces the author.
[[nodiscard]] inline bool CoreShouldShowAuthor(const CoreAuthorVisibility &v) {
	if (v.info || !v.incoming) {
		return false;
	} else if (v.hasOverride || v.originalMsgId || v.selfTalk) {
		return true;
	} else if (!v.chatType) {
		return false;
	}
	return *v.chatType != CoreChatType::Single;
}

} // namespace Delta
