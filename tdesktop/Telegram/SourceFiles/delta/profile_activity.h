#pragma once

#include "account_inventory.h"

#include <QMap>
#include <memory>

namespace Delta {

class ProfileActivity final {
public:
	using Changed = std::function<void()>;
	using Error = std::function<void(QString)>;
	ProfileActivity(AccountInventoryRequest request, Changed changed, Error error);
	void start();
	void refreshProfiles();
	void handleEvent(const QJsonObject &envelope);
	[[nodiscard]] int unread(int account) const;
	[[nodiscard]] int totalUnread() const;

private:
	struct State {
		quint64 generation = 0;
		int unread = 0;
		bool loading = false;
		bool dirty = false;
	};
	void refresh(int account);
	void fail(QString error);
	void call(QString method, QJsonArray params, DeltaRpc::Callback done);

	AccountInventoryRequest _request;
	Changed _changed;
	Error _error;
	QString _lastError;
	QMap<int, State> _accounts;
	quint64 _generation = 0;
	bool _inventoryLoading = false;
	bool _inventoryDirty = false;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

}
