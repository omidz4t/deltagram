#pragma once

#include <cstdint>
#include <limits>
#include <optional>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace Delta {

// Minted in ApplyMessageData for fromId > 1. Survives PeerId's 48-bit mask.
inline constexpr std::uint64_t kContactUserIdBase = std::uint64_t(1) << 40;

// The bridge uses positive signed 32-bit Core IDs. Keep both identities in
// native media/cache keys, including across process restarts and profile order.
[[nodiscard]] constexpr std::uint64_t MediaIdentity(
		std::int32_t accountId,
		std::int32_t messageId) {
	return (accountId > 0 && messageId > 0)
		? (std::uint64_t(1) << 62)
			| (std::uint64_t(accountId) << 31)
			| std::uint64_t(messageId)
		: 0;
}

// Device-talk membership and ChannelRequest still use 32-bit Core chat IDs.
// Sender identities (1<<40)+contact must not be truncated into that set.
[[nodiscard]] constexpr bool CoreIdFitsInt(std::uint64_t id) {
	return id > 0
		&& id <= std::uint64_t(std::numeric_limits<std::int32_t>::max());
}

[[nodiscard]] constexpr bool IsSenderUserId(std::uint64_t id) {
	return id >= kContactUserIdBase;
}

[[nodiscard]] constexpr std::optional<int> CoreChatUserId(std::uint64_t id) {
	return CoreIdFitsInt(id)
		? std::optional<int>(static_cast<int>(id))
		: std::nullopt;
}

[[nodiscard]] inline bool CoreJsonIdMatches(
		const QJsonObject &json,
		std::uint64_t expected) {
	if (!CoreIdFitsInt(expected)) {
		return false;
	}
	const auto id = json.value(QStringLiteral("id"));
	return id.isDouble() && id.toDouble() == double(expected);
}

enum class CoreChatType : unsigned char {
	Single,
	Group,
	Mailinglist,
	OutBroadcast,
	InBroadcast,
};

[[nodiscard]] inline std::optional<CoreChatType> CoreJsonKnownChatType(
		const QJsonValue &value) {
	if (!value.isString()) {
		return std::nullopt;
	}
	const auto name = value.toString();
	if (name == QStringLiteral("Single")) {
		return CoreChatType::Single;
	} else if (name == QStringLiteral("Group")) {
		return CoreChatType::Group;
	} else if (name == QStringLiteral("Mailinglist")) {
		return CoreChatType::Mailinglist;
	} else if (name == QStringLiteral("OutBroadcast")) {
		return CoreChatType::OutBroadcast;
	} else if (name == QStringLiteral("InBroadcast")) {
		return CoreChatType::InBroadcast;
	}
	return std::nullopt;
}

// Chat-list items serialize isDeviceTalk; FullChat/BasicChat serialize
// isDeviceChat. Absence or false is not a negative classification.
[[nodiscard]] inline bool CoreJsonMarksDeviceTalk(const QJsonObject &json) {
	const auto talk = json.value(QStringLiteral("isDeviceTalk"));
	if (talk.isBool() && talk.toBool()) {
		return true;
	}
	const auto chat = json.value(QStringLiteral("isDeviceChat"));
	return chat.isBool() && chat.toBool();
}

struct CoreChatMembership {
	std::optional<bool> direct;
	std::optional<bool> group;
	bool deviceTalk = false;
};

[[nodiscard]] inline std::optional<CoreChatMembership> CoreChatMembershipFromJson(
		const QJsonObject &json,
		std::uint64_t expectedId) {
	if (!CoreJsonIdMatches(json, expectedId)) {
		return std::nullopt;
	}
	auto result = CoreChatMembership();
	if (const auto type = CoreJsonKnownChatType(
			json.value(QStringLiteral("chatType")))) {
		result.direct = (*type == CoreChatType::Single);
		result.group = (*type == CoreChatType::Group);
	}
	result.deviceTalk = CoreJsonMarksDeviceTalk(json);
	return result;
}

} // namespace Delta
