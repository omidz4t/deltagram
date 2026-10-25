#include "profile_activity.h"

#include <algorithm>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

namespace Delta {

ProfileActivity::ProfileActivity(AccountInventoryRequest request, Changed changed, Error error)
: _request(std::move(request))
, _changed(std::move(changed))
, _error(std::move(error)) {
}

void ProfileActivity::call(QString method, QJsonArray params, DeltaRpc::Callback done) {
	const auto guard = std::weak_ptr<bool>(_alive);
	_request(std::move(method), std::move(params), [guard, done = std::move(done)](QJsonObject reply) {
		if (!guard.expired()) done(std::move(reply));
	});
}

void ProfileActivity::fail(QString error) {
	if (_lastError == error) return;
	_lastError = error;
	if (_error) _error(std::move(error));
}

void ProfileActivity::start() {
	call(u"start_io_for_all_accounts"_s, {}, [=, this](QJsonObject reply) {
		if (reply.contains(u"error"_s) || !reply.contains(u"result"_s)
			|| !reply.value(u"result"_s).isNull()) {
			fail(u"Background messaging could not start for all profiles. Restart Deltagram to reconnect."_s);
		}
	});
	refreshProfiles();
}

void ProfileActivity::refreshProfiles() {
	if (_inventoryLoading) { _inventoryDirty = true; return; }
	_inventoryLoading = true;
	ReadAccountInventory([=, this](QString method, QJsonArray params, DeltaRpc::Callback done) {
		call(std::move(method), std::move(params), std::move(done));
	}, [=, this](AccountInventory inventory) {
		_inventoryLoading = false;
		if (std::exchange(_inventoryDirty, false)) { refreshProfiles(); return; }
		if (!inventory.error.isEmpty()) { fail(inventory.error); return; }
		auto removed = false;
		for (auto i = _accounts.begin(); i != _accounts.end();) {
			if (inventory.configured.contains(i.key())) ++i;
			else { i = _accounts.erase(i); removed = true; }
		}
		for (const auto id : inventory.configured) {
			if (!_accounts.contains(id)) _accounts.insert(id, State{ ++_generation });
			refresh(id);
		}
		if (removed && _changed) _changed();
	});
}

void ProfileActivity::refresh(int account) {
	auto i = _accounts.find(account);
	if (i == _accounts.end()) return;
	if (i->loading) { i->dirty = true; return; }
	i->loading = true;
	const auto generation = i->generation;
	call(u"get_fresh_msgs"_s, { account }, [=, this](QJsonObject reply) {
		auto i = _accounts.find(account);
		if (i == _accounts.end() || i->generation != generation) return;
		i->loading = false;
		if (std::exchange(i->dirty, false)) { refresh(account); return; }
		const auto value = reply.value(u"result"_s);
		auto valid = !reply.contains(u"error"_s) && value.isArray();
		auto ids = QSet<int>();
		for (const auto &entry : value.toArray()) {
			const auto id = entry.toInt();
			if (!entry.isDouble() || id <= 0 || entry.toDouble() != id || ids.contains(id)) {
				valid = false;
				break;
			}
			ids.insert(id);
		}
		if (!valid) { fail(u"Unread messages could not be checked. Existing profile counts were kept."_s); return; }
		_lastError.clear();
		const auto count = int(ids.size());
		if (i->unread == count) return;
		i->unread = count;
		if (_changed) _changed();
	});
}

void ProfileActivity::handleEvent(const QJsonObject &envelope) {
	const auto event = envelope.value(u"event"_s).toObject();
	const auto kind = event.value(u"kind"_s).toString();
	if (kind == u"AccountsChanged"_s) { start(); return; }
	if (kind == u"AccountsItemChanged"_s) { refreshProfiles(); return; }
	if (kind != u"IncomingMsg"_s && kind != u"IncomingMsgBunch"_s
		&& kind != u"MsgsNoticed"_s && kind != u"MsgRead"_s
		&& kind != u"MsgDeleted"_s && kind != u"MsgsChanged"_s
		&& kind != u"ChatModified"_s && kind != u"ChatlistChanged"_s
		&& kind != u"ChatlistItemChanged"_s) return;
	const auto context = envelope.value(u"contextId"_s);
	const auto account = context.toInt();
	if (!context.isDouble() || account <= 0 || context.toDouble() != account) return;
	if (_accounts.contains(account)) refresh(account);
	else refreshProfiles();
}

int ProfileActivity::unread(int account) const {
	const auto i = _accounts.constFind(account);
	return i == _accounts.cend() ? 0 : i->unread;
}

int ProfileActivity::totalUnread() const {
	auto total = qint64(0);
	for (const auto &state : _accounts) total += state.unread;
	return int(std::min(total, qint64(std::numeric_limits<int>::max())));
}

}
