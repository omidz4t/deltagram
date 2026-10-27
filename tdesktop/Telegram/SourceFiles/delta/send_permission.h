#pragma once

#include "account_inventory.h"
#include "account_scope.h"
#include "contact_request_actions.h"
#include "media_identity.h"
#include "reply_store.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace Delta {

inline const QString kCoreSendDenied = QStringLiteral(
	"This conversation cannot accept messages.");
inline const QString kCoreSendDeniedKeepReply = QStringLiteral(
	"This conversation cannot accept messages. The reply was saved for review.");
inline const QString kCoreSendUnknown = QStringLiteral(
	"Message availability could not be confirmed. Try again.");
inline const QString kCoreSendUnknownKeepReply = QStringLiteral(
	"Message availability could not be confirmed. Try again. The reply was saved for review.");
inline const QString kCoreSendReplyAccountChanged = QStringLiteral(
	"A notification reply in your previous profile needs review. Switch back and open Saved replies.");

[[nodiscard]] inline std::optional<bool> CoreFullChatCanSend(
		const QJsonObject &json,
		int chatId) {
	if (chatId <= 0 || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	if (!CoreJsonIdMatches(json, std::uint64_t(chatId))) {
		return std::nullopt;
	}
	const auto canSend = json.value(QStringLiteral("canSend"));
	return canSend.isBool() ? std::optional<bool>(canSend.toBool()) : std::nullopt;
}

[[nodiscard]] inline std::optional<bool> CoreChatCanSend(
		const QJsonObject &json,
		int chatId) {
	if (chatId <= 0 || json.contains(QStringLiteral("error"))) {
		return std::nullopt;
	}
	const auto result = json.value(QStringLiteral("result"));
	if (!result.isObject()) {
		return std::nullopt;
	}
	return CoreFullChatCanSend(result.toObject(), chatId);
}

[[nodiscard]] inline bool CoreSendAllowed(std::optional<bool> canSend) {
	return canSend.has_value() && *canSend;
}

[[nodiscard]] inline QString CoreSendUnavailable(
		std::optional<bool> canSend,
		bool keepReply = false) {
	if (canSend.has_value() && !*canSend) {
		return keepReply ? kCoreSendDeniedKeepReply : kCoreSendDenied;
	}
	return keepReply ? kCoreSendUnknownKeepReply : kCoreSendUnknown;
}

[[nodiscard]] inline QString CoreSendUnavailable(
		std::optional<bool> canSend,
		std::optional<bool> contactRequest,
		bool keepReply = false) {
	if (contactRequest == true) {
		return keepReply ? kCoreSendDeniedKeepReply : kCoreSendDenied;
	}
	return CoreSendUnavailable(canSend, keepReply);
}

[[nodiscard]] inline int CoreEventChatId(const QJsonObject &event) {
	for (const auto &key : { QStringLiteral("chatId"), QStringLiteral("chat_id") }) {
		const auto value = event.value(key);
		if (!value.isDouble()) {
			continue;
		}
		const auto id = value.toInt();
		if (id > 0
			&& value.toDouble() == double(id)
			&& CoreIdFitsInt(std::uint64_t(id))) {
			return id;
		}
	}
	return 0;
}

inline void ReadCoreChatCanSend(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int chat,
		std::function<void(std::optional<bool>)> done) {
	if (account <= 0 || chat <= 0) {
		done(std::nullopt);
		return;
	}
	request(QStringLiteral("get_full_chat_by_id"), { account, chat },
		scope.bind([done = std::move(done), chat](QJsonObject reply) {
			done(CoreChatCanSend(reply, chat));
		}));
}

class ChatSendPermissions final {
public:
	using Request = AccountInventoryRequest;
	using Changed = std::function<void()>;
	using Done = std::function<void(std::optional<bool>)>;

	explicit ChatSendPermissions(Request request, Changed changed = {})
	: _request(std::move(request))
	, _changed(std::move(changed)) {
	}

	void observe(
			const AccountScope &scope,
			int account,
			int chat,
			Done done = {}) {
		if (account <= 0 || chat <= 0) {
			if (done) done(std::nullopt);
			return;
		}
		auto &slot = ensureSlot(account, chat);
		if (done) slot.waiters.push_back(std::move(done));
		if (slot.inFlight) return;
		startRead(account, chat, scope.guard());
	}

	void ensure(const AccountScope &scope, int account, int chat) {
		if (watching(account, chat)) return;
		observe(scope, account, chat);
	}

	void watch(const AccountScope &scope, int account, int chat) {
		if (watching(account, chat)) {
			refresh(scope, account, chat);
			return;
		}
		ensure(scope, account, chat);
	}

	void refresh(const AccountScope &scope, int account, int chat) {
		if (!watching(account, chat)) return;
		auto &slot = _slots[{ account, chat }];
		if (slot.inFlight) {
			slot.dirty = true;
			slot.followUpGuard = scope.guard();
			return;
		}
		startRead(account, chat, scope.guard());
	}

	void refreshWatched(
			const AccountScope &scope,
			int account,
			int focusedChat = 0) {
		auto chats = std::vector<int>();
		auto seen = std::set<int>();
		const auto add = [&](int chat) {
			if (chat > 0 && seen.insert(chat).second && watching(account, chat)) {
				chats.push_back(chat);
			}
		};
		add(focusedChat);
		for (const auto &[key, slot] : _slots) {
			if (key.first == account && !slot.waiters.empty()) {
				add(key.second);
			}
		}
		for (const auto chat : chats) refresh(scope, account, chat);
	}

	void refreshObserved(
			const AccountScope &scope,
			int account,
			int chat,
			int focusedChat = 0) {
		if (account <= 0 || chat <= 0 || !watching(account, chat)) return;
		if (chat != focusedChat) {
			const auto i = _slots.find({ account, chat });
			if (i == _slots.end() || i->second.waiters.empty()) return;
		}
		refresh(scope, account, chat);
	}

	void forget(int account, int chat) {
		const auto i = _slots.find({ account, chat });
		if (i == _slots.end()) return;
		auto waiters = std::move(i->second.waiters);
		_slots.erase(i);
		auto notify = _changed;
		if (notify) notify();
		for (auto &waiter : waiters) waiter(std::nullopt);
	}

	void reset() {
		auto previous = std::move(_slots);
		_slots.clear();
		for (auto &entry : previous) {
			auto waiters = std::move(entry.second.waiters);
			for (auto &waiter : waiters) waiter(std::nullopt);
		}
	}

	[[nodiscard]] std::optional<bool> known(int account, int chat) const {
		const auto i = _slots.find({ account, chat });
		return (i == _slots.end()) ? std::nullopt : i->second.value;
	}

	[[nodiscard]] std::optional<bool> contactRequest(int account, int chat) const {
		const auto i = _slots.find({ account, chat });
		return (i == _slots.end()) ? std::nullopt : i->second.contactRequest;
	}

	[[nodiscard]] std::optional<CoreChatType> knownType(int account, int chat) const {
		const auto i = _slots.find({ account, chat });
		return (i == _slots.end()) ? std::nullopt : i->second.type;
	}

	[[nodiscard]] bool allowed(int account, int chat) const {
		return CoreSendAllowed(known(account, chat));
	}

	[[nodiscard]] bool sendable(int account, int chat) const {
		return allowed(account, chat) && contactRequest(account, chat) != true;
	}

	[[nodiscard]] bool watching(int account, int chat) const {
		return _slots.find({ account, chat }) != _slots.end();
	}

	[[nodiscard]] bool reading(int account, int chat) const {
		const auto i = _slots.find({ account, chat });
		return i != _slots.end() && i->second.inFlight != 0;
	}

private:
	using Key = std::pair<int, int>;
	struct Slot {
		std::optional<bool> value;
		std::optional<bool> contactRequest;
		std::optional<CoreChatType> type;
		std::uint64_t incarnation = 0;
		std::uint64_t seq = 0;
		std::uint64_t inFlight = 0;
		bool dirty = false;
		std::function<bool()> followUpGuard;
		std::vector<Done> waiters;
	};

	Slot &ensureSlot(int account, int chat) {
		auto &slot = _slots[{ account, chat }];
		if (!slot.incarnation) slot.incarnation = ++_nextIncarnation;
		return slot;
	}

	void startRead(int account, int chat, std::function<bool()> valid) {
		auto &slot = _slots[{ account, chat }];
		slot.inFlight = ++slot.seq;
		slot.dirty = false;
		slot.followUpGuard = {};
		const auto incarnation = slot.incarnation;
		const auto seq = slot.inFlight;
		const auto alive = std::weak_ptr<bool>(_alive);
		auto request = _request;
		request(QStringLiteral("get_full_chat_by_id"), { account, chat },
			[alive, valid = std::move(valid), account, chat, incarnation, seq, this](
					QJsonObject reply) {
				if (alive.expired()) return;
				const auto live = valid();
				const auto parsed = live
					? CoreContactRequestFromReply(reply, chat)
					: std::nullopt;
				completeRead(
					{ account, chat },
					incarnation,
					seq,
					live ? CoreChatCanSend(reply, chat) : std::nullopt,
					parsed ? std::optional<bool>(parsed->isContactRequest) : std::nullopt,
					parsed ? std::optional<CoreChatType>(parsed->type) : std::nullopt);
			});
	}

	void followUp(Key key) {
		const auto i = _slots.find(key);
		if (i == _slots.end()) return;
		auto valid = i->second.followUpGuard
			? std::move(i->second.followUpGuard)
			: std::function<bool()>([] { return true; });
		i->second.followUpGuard = {};
		startRead(key.first, key.second, std::move(valid));
	}

	void publish(
			Key key,
			std::optional<bool> value,
			std::optional<bool> contactRequest,
			std::optional<CoreChatType> type) {
		const auto i = _slots.find(key);
		if (i == _slots.end()) return;
		const auto changed = i->second.value != value
			|| i->second.contactRequest != contactRequest
			|| i->second.type != type;
		i->second.value = value;
		i->second.contactRequest = contactRequest;
		i->second.type = type;
		auto waiters = std::move(i->second.waiters);
		i->second.waiters.clear();
		auto notify = _changed;
		if (changed && notify) notify();
		for (auto &waiter : waiters) waiter(value);
	}

	void completeRead(
			Key key,
			std::uint64_t incarnation,
			std::uint64_t seq,
			std::optional<bool> value,
			std::optional<bool> contactRequest,
			std::optional<CoreChatType> type) {
		const auto i = _slots.find(key);
		if (i == _slots.end() || i->second.incarnation != incarnation) return;
		if (i->second.inFlight == seq) i->second.inFlight = 0;
		if (seq != i->second.seq) {
			if (i->second.dirty && i->second.inFlight == 0) followUp(key);
			return;
		}
		if (i->second.dirty) {
			followUp(key);
			return;
		}
		publish(key, value, contactRequest, type);
	}

	Request _request;
	Changed _changed;
	std::map<Key, Slot> _slots;
	std::uint64_t _nextIncarnation = 0;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

inline void SubmitAuthorizedReply(
		ReplyStore &replies,
		ChatSendPermissions &permissions,
		const AccountScope &scope,
		int account,
		int chat,
		QString text,
		ReplyStore::Done done) {
	const auto valid = scope.guard();
	replies.persist(account, chat, std::move(text),
		[&replies, &permissions, &scope, account, valid, done = std::move(done)](
				std::optional<SavedReply> saved, QString error) {
			if (!saved) {
				if (done) done(0, std::move(error));
				return;
			}
			if (!valid()) {
				if (done) done(0, kCoreSendReplyAccountChanged);
				return;
			}
			const auto reviewed = *saved;
			permissions.observe(scope, account, reviewed.chat,
				[&replies, &permissions, account, reviewed, valid, done](
						std::optional<bool> canSend) {
					if (!valid()) {
						if (done) done(0, kCoreSendReplyAccountChanged);
						return;
					}
					if (!permissions.sendable(account, reviewed.chat)) {
						if (done) {
							done(0, CoreSendUnavailable(
								canSend,
								permissions.contactRequest(account, reviewed.chat),
								true));
						}
						return;
					}
					replies.send(account, reviewed, done);
				});
		});
}

} // namespace Delta
