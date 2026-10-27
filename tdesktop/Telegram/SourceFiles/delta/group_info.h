#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Delta {

// Group header inspect is get_full_chat_by_id plus get_contact. Member
// rows mint kContactUserIdBase+contactId so they cannot be used as chat
// ids. Explicit Message on that profile still uses ContactForPeer.

inline const QString kGroupInfoUnavailable = QStringLiteral(
	"This group cannot be opened.");
inline const QString kGroupInfoUnreadable = QStringLiteral(
	"Group details could not be read. Refresh before trying again.");
inline const QString kGroupInfoNotGroup = QStringLiteral(
	"This is not a group conversation.");
inline const QString kGroupMemberUnknown = QStringLiteral(
	"That member is not in this group.");
inline const QString kGroupMemberUnreadable = QStringLiteral(
	"This member could not be opened. Refresh before trying again.");

enum class CoreGroupMemberKind : unsigned char {
	Invalid,
	Self,
	Reserved,
	Peer,
};

enum class CoreGroupRead : unsigned char {
	Invalid,
	NotGroup,
	Group,
};

struct CoreGroupChat {
	int id = 0;
	QString name;
	QString profileImage;
	std::optional<bool> muted;
	std::vector<int> contactIds;
};

struct CoreGroupReadResult {
	CoreGroupRead kind = CoreGroupRead::Invalid;
	CoreGroupChat group;
};

struct CoreGroupMemberProfile {
	int contactId = 0;
	CoreGroupMemberKind kind = CoreGroupMemberKind::Invalid;
	std::uint64_t profileUserId = 0;
	QString name;
	QString address;
	QString avatar;
	qint64 lastSeen = 0;
};

[[nodiscard]] inline bool CoreGroupChatId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline std::optional<int> CoreGroupExactInt(const QJsonValue &value) {
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

[[nodiscard]] inline CoreGroupMemberKind CoreGroupClassifyMember(int contactId) {
	if (contactId <= 0 || !CoreIdFitsInt(std::uint64_t(contactId))) {
		return CoreGroupMemberKind::Invalid;
	}
	if (contactId == 1) {
		return CoreGroupMemberKind::Self;
	}
	if (contactId <= 9) {
		return CoreGroupMemberKind::Reserved;
	}
	return CoreGroupMemberKind::Peer;
}

// Minted sender identity for a group member. Self has no minted id.
// Reserved and peer contacts never collapse to a Core chat id.
[[nodiscard]] inline std::optional<std::uint64_t> CoreGroupMemberProfileUserId(
		int contactId) {
	const auto kind = CoreGroupClassifyMember(contactId);
	if (kind != CoreGroupMemberKind::Peer
		&& kind != CoreGroupMemberKind::Reserved) {
		return std::nullopt;
	}
	return kContactUserIdBase + std::uint64_t(contactId);
}

[[nodiscard]] inline bool CoreGroupMemberInChat(
		const CoreGroupChat &group,
		int contactId) {
	return std::find(
		group.contactIds.begin(),
		group.contactIds.end(),
		contactId) != group.contactIds.end();
}

[[nodiscard]] inline CoreGroupReadResult CoreGroupChatFromJson(
		const QJsonObject &json,
		std::uint64_t expected) {
	CoreGroupReadResult result;
	if (!CoreIdFitsInt(expected)
		|| !CoreGroupChatId(int(expected))
		|| !CoreJsonIdMatches(json, expected)) {
		return result;
	}
	const auto type = CoreJsonKnownChatType(
		json.value(QStringLiteral("chatType")));
	if (!type) {
		return result;
	}
	if (*type != CoreChatType::Group) {
		result.kind = CoreGroupRead::NotGroup;
		return result;
	}
	const auto selfTalk = json.value(QStringLiteral("isSelfTalk"));
	if (selfTalk.isBool() && selfTalk.toBool()) {
		return result;
	}
	const auto contacts = json.value(QStringLiteral("contactIds"));
	if (!contacts.isArray()) {
		return result;
	}
	CoreGroupChat group;
	group.id = int(expected);
	const auto name = json.value(QStringLiteral("name"));
	if (!name.isUndefined() && !name.isNull() && !name.isString()) {
		return result;
	}
	group.name = name.toString();
	const auto image = json.value(QStringLiteral("profileImage"));
	if (!image.isUndefined() && !image.isNull() && !image.isString()) {
		return result;
	}
	group.profileImage = image.toString();
	const auto muted = json.value(QStringLiteral("isMuted"));
	if (muted.isBool()) {
		group.muted = muted.toBool();
	} else if (!muted.isUndefined() && !muted.isNull()) {
		return result;
	}
	auto seen = QSet<int>();
	for (const auto &entry : contacts.toArray()) {
		const auto id = CoreGroupExactInt(entry);
		if (!id || seen.contains(*id)) {
			return result;
		}
		seen.insert(*id);
		group.contactIds.push_back(*id);
	}
	result.kind = CoreGroupRead::Group;
	result.group = std::move(group);
	return result;
}

[[nodiscard]] inline CoreGroupReadResult CoreGroupChatFromReply(
		const QJsonObject &reply,
		int expectedChatId) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return {};
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return {};
	}
	return CoreGroupChatFromJson(value.toObject(), std::uint64_t(expectedChatId));
}

[[nodiscard]] inline QString CoreGroupMemberPlaceholder(int contactId) {
	switch (CoreGroupClassifyMember(contactId)) {
	case CoreGroupMemberKind::Self:
		return QStringLiteral("You");
	case CoreGroupMemberKind::Reserved:
		return (contactId == 2)
			? QStringLiteral("Info")
			: (contactId == 5)
			? QStringLiteral("Device")
			: QStringLiteral("System");
	case CoreGroupMemberKind::Peer:
		return QStringLiteral("Contact");
	case CoreGroupMemberKind::Invalid:
		break;
	}
	return QString();
}

[[nodiscard]] inline std::optional<CoreGroupMemberProfile> CoreGroupMemberFromContact(
		const QJsonObject &reply,
		int expectedId) {
	if (reply.contains(QStringLiteral("error"))
		|| !reply.contains(QStringLiteral("result"))) {
		return std::nullopt;
	}
	const auto kind = CoreGroupClassifyMember(expectedId);
	if (kind == CoreGroupMemberKind::Invalid) {
		return std::nullopt;
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (!value.isObject()) {
		return std::nullopt;
	}
	const auto contact = value.toObject();
	const auto id = CoreGroupExactInt(contact.value(QStringLiteral("id")));
	if (!id || *id != expectedId) {
		return std::nullopt;
	}
	CoreGroupMemberProfile profile;
	profile.contactId = expectedId;
	profile.kind = kind;
	if (const auto userId = CoreGroupMemberProfileUserId(expectedId)) {
		profile.profileUserId = *userId;
	}
	const auto display = contact.value(QStringLiteral("displayName"));
	const auto name = contact.value(QStringLiteral("name"));
	const auto address = contact.value(QStringLiteral("address"));
	const auto avatar = contact.value(QStringLiteral("profileImage"));
	const auto lastSeen = contact.value(QStringLiteral("lastSeen"));
	if (!display.isUndefined() && !display.isNull() && !display.isString()) {
		return std::nullopt;
	}
	if (!name.isUndefined() && !name.isNull() && !name.isString()) {
		return std::nullopt;
	}
	if (!address.isUndefined() && !address.isNull() && !address.isString()) {
		return std::nullopt;
	}
	if (!avatar.isUndefined() && !avatar.isNull() && !avatar.isString()) {
		return std::nullopt;
	}
	if (!lastSeen.isUndefined() && !lastSeen.isNull() && !lastSeen.isDouble()) {
		return std::nullopt;
	}
	profile.address = address.toString();
	profile.avatar = avatar.toString();
	if (lastSeen.isDouble()) {
		profile.lastSeen = qint64(lastSeen.toDouble());
	}
	profile.name = display.toString();
	if (profile.name.isEmpty()) {
		profile.name = name.toString();
	}
	if (profile.name.isEmpty()) {
		profile.name = profile.address;
	}
	if (profile.name.isEmpty()) {
		profile.name = CoreGroupMemberPlaceholder(expectedId);
	}
	if (profile.name.isEmpty()) {
		return std::nullopt;
	}
	return profile;
}

inline void ReadCoreGroupChat(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chatId,
		std::function<void(CoreGroupReadResult, QString)> done) {
	if (account <= 0 || !CoreGroupChatId(chatId)) {
		done({}, kGroupInfoUnavailable);
		return;
	}
	if (!scope.guard()()) {
		return;
	}
	request(
		QStringLiteral("get_full_chat_by_id"),
		{ account, chatId },
		scope.bind([done = std::move(done), chatId](QJsonObject reply) {
			const auto parsed = CoreGroupChatFromReply(reply, chatId);
			if (parsed.kind == CoreGroupRead::Group) {
				done(parsed, {});
				return;
			}
			if (parsed.kind == CoreGroupRead::NotGroup) {
				done(parsed, kGroupInfoNotGroup);
				return;
			}
			done({}, kGroupInfoUnreadable);
		}));
}

inline void InspectCoreGroupMember(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chatId,
		int contactId,
		std::function<void(CoreGroupMemberProfile, QString)> done) {
	struct State {
		AccountInventoryRequest request;
		std::function<bool()> valid;
		int account = 0;
		int chatId = 0;
		int contactId = 0;
		std::function<void(CoreGroupMemberProfile, QString)> done;
		bool finished = false;

		void finish(CoreGroupMemberProfile profile, QString error) {
			if (finished) {
				return;
			}
			finished = true;
			if (!error.isEmpty()) {
				profile = {};
			}
			if (done) {
				done(std::move(profile), std::move(error));
			}
		}
	};

	const auto state = std::make_shared<State>();
	state->request = std::move(request);
	state->valid = scope.guard();
	state->account = account;
	state->chatId = chatId;
	state->contactId = contactId;
	state->done = std::move(done);

	if (account <= 0 || !CoreGroupChatId(chatId)) {
		state->finish({}, kGroupInfoUnavailable);
		return;
	}
	if (CoreGroupClassifyMember(contactId) == CoreGroupMemberKind::Invalid) {
		state->finish({}, kGroupMemberUnreadable);
		return;
	}
	if (!state->valid()) {
		return;
	}

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

	call(
		QStringLiteral("get_full_chat_by_id"),
		{ account, chatId },
		[state, call](QJsonObject reply) {
			const auto parsed = CoreGroupChatFromReply(reply, state->chatId);
			if (parsed.kind != CoreGroupRead::Group) {
				state->finish(
					{},
					parsed.kind == CoreGroupRead::NotGroup
						? kGroupInfoNotGroup
						: kGroupInfoUnreadable);
				return;
			}
			if (!CoreGroupMemberInChat(parsed.group, state->contactId)) {
				state->finish({}, kGroupMemberUnknown);
				return;
			}
			call(
				QStringLiteral("get_contact"),
				{ state->account, state->contactId },
				[state](QJsonObject reply) {
					const auto profile = CoreGroupMemberFromContact(
						reply,
						state->contactId);
					if (!profile) {
						state->finish({}, kGroupMemberUnreadable);
						return;
					}
					state->finish(*profile, {});
				});
		});
}

} // namespace Delta
