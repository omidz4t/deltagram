#pragma once

#include "account_scope.h"
#include "delta_rpc.h"

#include <QVector>
#include <optional>

namespace Delta {

struct HistoryDeletionResult {
	QVector<int> deletedIds;
	bool chatDeleted = false;
	QString error;
};

struct HistoryDateRange {
	qint64 after = 0;
	qint64 before = 0;
};

void DeleteCoreMessages(
	std::shared_ptr<DeltaRpc> rpc,
	const AccountScope &scope,
	int accountId,
	const QJsonArray &ids,
	bool revoke,
	std::function<void(QString)> done);

// Each continuation is cancelled when the owning account scope changes.
void DeleteChatHistory(
	std::shared_ptr<DeltaRpc> rpc,
	const AccountScope &scope,
	int accountId,
	int chatId,
	bool removeChat,
	bool revoke,
	std::function<void(HistoryDeletionResult)> done,
	std::optional<HistoryDateRange> dates = std::nullopt);

} // namespace Delta
