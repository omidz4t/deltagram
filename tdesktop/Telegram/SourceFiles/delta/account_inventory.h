#pragma once

#include "delta_rpc.h"

#include <QSet>
#include <QVector>
#include <memory>

namespace Delta {

struct AccountInventory {
	QVector<int> configured;
	int unconfigured = 0;
	QString error;
};

using AccountInventoryRequest = std::function<void(
	QString, QJsonArray, DeltaRpc::Callback)>;

// The caller binds each request callback to its UI/account lifetime. A failed
// or malformed read must never nominate an existing account for setup writes.
inline void ReadAccountInventory(
		AccountInventoryRequest request,
		std::function<void(AccountInventory)> done) {
	request(QStringLiteral("get_all_account_ids"), {}, [=](QJsonObject reply) {
		const auto value = reply.value(QStringLiteral("result"));
		const auto failure = QStringLiteral("Could not read profile state. Please try again.");
		if (reply.contains(QStringLiteral("error")) || !value.isArray()) {
			done({ {}, 0, failure });
			return;
		}
		auto ids = QVector<int>();
		auto seen = QSet<int>();
		for (const auto &entry : value.toArray()) {
			const auto id = entry.toInt();
			if (!entry.isDouble() || id <= 0 || entry.toDouble() != id || seen.contains(id)) {
				done({ {}, 0, failure });
				return;
			}
			seen.insert(id);
			ids.push_back(id);
		}
		if (ids.empty()) {
			done({});
			return;
		}
		struct State {
			QVector<bool> configured;
			int remaining = 0;
			bool failed = false;
		};
		const auto state = std::make_shared<State>();
		state->configured.resize(ids.size());
		state->remaining = ids.size();
		for (auto i = 0; i != ids.size(); ++i) {
			request(QStringLiteral("is_configured"), { ids[i] }, [=](QJsonObject reply) {
				const auto value = reply.value(QStringLiteral("result"));
				if (reply.contains(QStringLiteral("error")) || !value.isBool()) {
					state->failed = true;
				} else {
					state->configured[i] = value.toBool();
				}
				if (--state->remaining) return;
				if (state->failed) {
					done({ {}, 0, failure });
					return;
				}
				auto result = AccountInventory();
				for (auto j = 0; j != ids.size(); ++j) {
					if (state->configured[j]) result.configured.push_back(ids[j]);
					else if (!result.unconfigured) result.unconfigured = ids[j];
				}
				done(std::move(result));
			});
		}
	});
}

} // namespace Delta
