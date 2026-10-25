#pragma once

#include <QJsonObject>

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

} // namespace Delta
