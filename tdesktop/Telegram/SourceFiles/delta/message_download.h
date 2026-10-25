#pragma once

#include "account_scope.h"
#include "delta_rpc.h"

namespace Delta {

inline bool CanDownloadMessageState(const QString &state) {
	return state == QStringLiteral("Available")
		|| state == QStringLiteral("Failure");
}

// Recheck Core at activation time. Rendering must never override its download
// limit, and a failed/ambiguous request must not be retried automatically.
inline void DownloadCoreMessage(
		std::shared_ptr<DeltaRpc> rpc,
		const AccountScope &scope,
		int accountId,
		int messageId,
		std::function<void(QString)> done) {
	const auto finish = scope.bind([=](QJsonObject reply) {
		const auto value = reply.value(QStringLiteral("error"));
		auto error = value.toObject().value(QStringLiteral("message")).toString();
		if (!value.isUndefined() && error.isEmpty()) {
			error = QStringLiteral("Message download failed.");
		}
		done(std::move(error));
	});
	rpc->call(QStringLiteral("get_message"), { accountId, messageId },
		scope.bind([=](QJsonObject reply) {
			if (reply.contains(QStringLiteral("error"))) {
				finish(std::move(reply));
				return;
			}
			const auto state = reply.value(QStringLiteral("result")).toObject()
				.value(QStringLiteral("downloadState")).toString();
			if (CanDownloadMessageState(state)) {
				rpc->call(QStringLiteral("download_full_message"),
					{ accountId, messageId }, finish);
			} else if (state == QStringLiteral("Done")
				|| state == QStringLiteral("InProgress")) {
				done({});
			} else {
				done(QStringLiteral("This message cannot be downloaded."));
			}
		}));
}

} // namespace Delta
