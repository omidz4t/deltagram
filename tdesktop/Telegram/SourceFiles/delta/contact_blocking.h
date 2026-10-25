#pragma once

#include "account_inventory.h"
#include "account_scope.h"

#include <QSet>

#include <optional>
#include <vector>

namespace Delta {

struct BlockedContact {
	int id = 0;
	QString name;
	QString address;
	QString avatar;
};

inline std::optional<bool> CoreContactBlocked(const QJsonObject &reply, int id) {
	const auto contact = reply.value(QStringLiteral("result")).toObject();
	const auto actual = contact.value(QStringLiteral("id"));
	const auto blocked = contact.value(QStringLiteral("isBlocked"));
	if (reply.contains(QStringLiteral("error")) || id <= 9
		|| !actual.isDouble() || actual.toDouble() != id || !blocked.isBool()) {
		return std::nullopt;
	}
	return blocked.toBool();
}

inline void ReadCoreBlockedContacts(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		std::function<void(std::vector<BlockedContact>, QString)> done) {
	if (account <= 0) {
		done({}, QStringLiteral("No profile selected."));
		return;
	}
	request(QStringLiteral("get_blocked_contacts"), { account }, scope.bind(
		[done = std::move(done)](QJsonObject reply) {
			const auto value = reply.value(QStringLiteral("result"));
			auto valid = !reply.contains(QStringLiteral("error")) && value.isArray();
			auto rows = std::vector<BlockedContact>();
			auto ids = QSet<int>();
			for (const auto &entry : value.toArray()) {
				const auto contact = entry.toObject();
				const auto id = contact.value(QStringLiteral("id")).toInt();
				const auto blocked = CoreContactBlocked({{QStringLiteral("result"), contact}}, id);
				const auto name = contact.value(QStringLiteral("displayName"));
				const auto address = contact.value(QStringLiteral("address"));
				const auto avatar = contact.value(QStringLiteral("profileImage"));
				if (!blocked || !*blocked || ids.contains(id)
					|| !name.isString() || !address.isString()
					|| (!avatar.isNull() && !avatar.isString())) {
					valid = false;
					break;
				}
				ids.insert(id);
				rows.push_back({ id, name.toString(), address.toString(), avatar.toString() });
			}
			if (!valid) {
				done({}, QStringLiteral("Blocked contacts could not be checked. Refresh before making changes."));
				return;
			}
			done(std::move(rows), {});
		}));
}

inline void ChangeCoreContactBlocked(
		AccountInventoryRequest request,
		const AccountScope &scope,
		int account,
		int contact,
		bool blocked,
		std::function<void(int, QString)> done) {
	if (account <= 0 || contact <= 9) {
		done(0, QStringLiteral("This contact cannot be blocked."));
		return;
	}
	const auto current = scope.guard();
	const auto call = [=](QString method, QJsonArray args, DeltaRpc::Callback callback) {
		if (!current()) return;
		request(std::move(method), std::move(args),
			[current, callback = std::move(callback)](QJsonObject reply) {
				if (current()) callback(std::move(reply));
			});
	};
	call(QStringLiteral("get_contact"), { account, contact }, [=](QJsonObject reply) {
		const auto before = CoreContactBlocked(reply, contact);
		if (!before || *before == blocked) {
			done(0, QStringLiteral("The contact state changed or could not be checked. Refresh before trying again."));
			return;
		}
		call(QStringLiteral("get_chat_id_by_contact_id"), { account, contact }, [=](QJsonObject reply) {
			const auto value = reply.value(QStringLiteral("result"));
			const auto chat = value.isNull() ? 0 : value.toInt(-1);
			if (reply.contains(QStringLiteral("error"))
				|| (!value.isNull() && (!value.isDouble() || chat <= 0 || value.toDouble() != chat))) {
				done(0, QStringLiteral("The contact conversation could not be checked."));
				return;
			}
			call(blocked ? QStringLiteral("block_contact") : QStringLiteral("unblock_contact"),
				{ account, contact }, [=](QJsonObject) {
					call(QStringLiteral("get_contact"), { account, contact }, [=](QJsonObject reply) {
						const auto after = CoreContactBlocked(reply, contact);
						done(chat, after && *after == blocked ? QString() : QStringLiteral(
							"The blocking change was not confirmed. Refresh the contact state before trying again."));
					});
				});
		});
	});
}

} // namespace Delta
