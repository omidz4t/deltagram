#pragma once

#include "delta_rpc.h"

#include <QHash>
#include <memory>

namespace Delta {

class BackupTransfers final {
public:
	using Cancel = std::function<void()>;
	using Ready = std::function<void(QString svg, QString error)>;
	using Finished = std::function<void(QString error)>;

	explicit BackupTransfers(std::shared_ptr<DeltaRpc> rpc);
	~BackupTransfers();

	// Cancellation belongs to this attempt and its original account. It stays
	// effective if the provider becomes ready after the UI has been closed.
	[[nodiscard]] Cancel start(int accountId, Ready ready, Finished finished);

private:
	struct Transfer;
	std::weak_ptr<DeltaRpc> _rpc;
	QHash<int, std::weak_ptr<Transfer>> _active;
};

} // namespace Delta
