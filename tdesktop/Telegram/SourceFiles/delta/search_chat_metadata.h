#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "media_identity.h"

#include <QHash>
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

// Bounded get_basic_chat_info for search-hit chats. Missing metadata cannot
// manufacture a name. RememberCoreChat stays with the caller.

inline constexpr int kSearchChatMetadataPageLimit = 50;

inline const QString kSearchChatMetadataUnavailable = QStringLiteral(
	"Search result names could not be confirmed.");

struct SearchChatMetadata {
	int chatId = 0;
	QString name;
	QString avatar;
	QJsonObject json;
};

struct SearchChatMetadataResult {
	QHash<int, SearchChatMetadata> chats;
	std::vector<int> failedIds;
	QString error;
};

[[nodiscard]] inline bool CoreSearchChatId(int id) {
	return id > 9 && CoreIdFitsInt(std::uint64_t(id));
}

[[nodiscard]] inline bool SearchChatNameUsable(const QString &name) {
	return !name.trimmed().isEmpty();
}

[[nodiscard]] inline std::optional<SearchChatMetadata> SearchChatMetadataFromReply(
		const QJsonObject &reply,
		int chatId) {
	if (!CoreSearchChatId(chatId) || reply.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto result = reply.value(QStringLiteral("result"));
	if (!result.isObject()) {
		return std::nullopt;
	}
	const auto json = result.toObject();
	if (!CoreJsonIdMatches(json, std::uint64_t(chatId))) {
		return std::nullopt;
	}
	const auto name = json.value(QStringLiteral("name"));
	if (!name.isString()) {
		return std::nullopt;
	}
	const auto text = name.toString().trimmed();
	if (!SearchChatNameUsable(text)) {
		return std::nullopt;
	}
	SearchChatMetadata payload;
	payload.chatId = chatId;
	payload.name = text;
	const auto image = json.value(QStringLiteral("profileImage"));
	if (image.isString()) {
		payload.avatar = image.toString();
	}
	payload.json = json;
	return payload;
}

[[nodiscard]] inline std::optional<CoreChatType> SearchChatKnownType(
		const SearchChatMetadata &payload) {
	return CoreJsonKnownChatType(
		payload.json.value(QStringLiteral("chatType")));
}

inline void ReadSearchChatMetadata(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		const std::vector<int> &chatIds,
		std::function<void(SearchChatMetadataResult)> done) {
	// Keep the caller's start generation; bind() after a reentrant
	// invalidate would accept a later profile.
	const auto entryValid = scope.guard();
	if (!entryValid()) {
		return;
	}
	SearchChatMetadataResult result;
	std::vector<int> unique;
	auto seen = QSet<int>();
	const auto markFailed = [&](int chat) {
		result.failedIds.push_back(chat);
		if (result.error.isEmpty()) {
			result.error = kSearchChatMetadataUnavailable;
		}
	};
	if (account <= 0) {
		for (const auto chat : chatIds) {
			if (seen.contains(chat)) {
				continue;
			}
			seen.insert(chat);
			markFailed(chat);
		}
		done(std::move(result));
		return;
	}
	for (const auto chat : chatIds) {
		if (seen.contains(chat)) {
			continue;
		}
		seen.insert(chat);
		if (!CoreSearchChatId(chat)
			|| int(unique.size()) >= kSearchChatMetadataPageLimit) {
			markFailed(chat);
			continue;
		}
		unique.push_back(chat);
	}
	if (unique.empty()) {
		done(std::move(result));
		return;
	}
	struct State {
		SearchChatMetadataResult result;
		int remaining = 0;
		std::function<void(SearchChatMetadataResult)> done;
	};
	const auto state = std::make_shared<State>();
	state->result = std::move(result);
	state->remaining = int(unique.size());
	state->done = std::move(done);
	for (const auto chat : unique) {
		if (!entryValid()) {
			return;
		}
		request(
			QStringLiteral("get_basic_chat_info"),
			{ account, chat },
			[entryValid, state, chat](QJsonObject reply) {
				if (!entryValid()) {
					return;
				}
				if (const auto payload = SearchChatMetadataFromReply(
						reply,
						chat)) {
					state->result.chats.insert(chat, *payload);
				} else {
					state->result.failedIds.push_back(chat);
					if (state->result.error.isEmpty()) {
						state->result.error = kSearchChatMetadataUnavailable;
					}
				}
				if (--state->remaining) {
					return;
				}
				state->done(std::move(state->result));
			});
	}
}

} // namespace Delta
