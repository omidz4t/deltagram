#pragma once

#include "account_inventory.h"
#include "media_identity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace Delta {

inline const QString kProfileSetupBusy = QStringLiteral(
	"A profile setup is already in progress.");
inline const QString kProfileSetupUnavailable = QStringLiteral(
	"This profile cannot be set up here.");
inline const QString kProfileSetupUnconfirmed = QStringLiteral(
	"The profile change was not confirmed. Refresh before trying again.");
inline const QString kProfileSetupUnreadable = QStringLiteral(
	"The profile could not be checked. Refresh before trying again.");

enum class ProfileSetupKind : unsigned char {
	Create,
	Import,
	Copy,
};

enum class ProfileSetupSlotState : unsigned char {
	Held,
	Completed,
	Closed,
};

enum class ProfileSetupOutcome : unsigned char {
	Denied,
	Succeeded,
	Retryable,
	OpenOnly,
	Closed,
};

struct ProfileSetupResult {
	ProfileSetupOutcome outcome = ProfileSetupOutcome::Denied;
	int accountId = 0;
	bool acknowledged = false;
	bool wrote = false;
	bool configured = false;
	QString error;
};

struct ProfileSetupLease;

struct ProfileSetupTable {
	struct Entry {
		ProfileSetupKind kind = ProfileSetupKind::Create;
		ProfileSetupSlotState state = ProfileSetupSlotState::Held;
		ProfileSetupLease *lease = nullptr;
	};

	std::map<int, Entry> entries;
};

struct ProfileSetupLease {
	std::shared_ptr<ProfileSetupTable> table;
	int accountId = 0;
	ProfileSetupKind kind = ProfileSetupKind::Create;
	std::shared_ptr<void> lifetime = std::make_shared<char>(0);
	bool consumed = false;

	void release() {
		if (consumed) {
			return;
		}
		consumed = true;
		if (!table) {
			return;
		}
		const auto found = table->entries.find(accountId);
		if (found != table->entries.end() && found->second.lease == this) {
			table->entries.erase(found);
		}
		table.reset();
	}

	~ProfileSetupLease() {
		release();
	}

	ProfileSetupLease() = default;
	ProfileSetupLease(const ProfileSetupLease &) = delete;
	ProfileSetupLease &operator=(const ProfileSetupLease &) = delete;

};

[[nodiscard]] inline std::optional<int> ProfileSetupExactId(const QJsonValue &value) {
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

[[nodiscard]] inline bool ProfileSetupAcked(const QJsonObject &reply) {
	return !reply.contains(QStringLiteral("error"))
		&& reply.contains(QStringLiteral("result"))
		&& reply.value(QStringLiteral("result")).isNull();
}

[[nodiscard]] inline QString ProfileSetupReplyError(const QJsonObject &reply) {
	const auto error = reply.value(QStringLiteral("error"));
	if (error.isObject()) {
		const auto message = error.toObject().value(
			QStringLiteral("message")).toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	if (error.isString()) {
		const auto message = error.toString();
		if (!message.isEmpty()) {
			return message;
		}
	}
	return {};
}

[[nodiscard]] inline bool ProfileSetupShouldOpen(
		const ProfileSetupResult &result,
		int capturedId) {
	return capturedId > 0
		&& result.accountId == capturedId
		&& (result.outcome == ProfileSetupOutcome::Succeeded
			|| result.outcome == ProfileSetupOutcome::OpenOnly);
}

[[nodiscard]] inline bool ProfileSetupCanRetryMutation(
		const ProfileSetupResult &result,
		int capturedId) {
	return capturedId > 0
		&& result.accountId == capturedId
		&& result.outcome == ProfileSetupOutcome::Retryable;
}

[[nodiscard]] inline std::shared_ptr<ProfileSetupTable> ProfileSetupTableFor(
		const void *rpc) {
	struct World {
		std::map<const void *, std::weak_ptr<ProfileSetupTable>> tables;
	};
	static World world;
	for (auto i = world.tables.begin(); i != world.tables.end(); ) {
		if (i->second.expired()) {
			i = world.tables.erase(i);
		} else {
			++i;
		}
	}
	if (!rpc) {
		return std::make_shared<ProfileSetupTable>();
	}
	auto &weak = world.tables[rpc];
	if (const auto pinned = weak.lock()) {
		return pinned;
	}
	auto table = std::make_shared<ProfileSetupTable>();
	weak = table;
	return table;
}

[[nodiscard]] inline bool ProfileSetupBlocksMutation(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId) {
	return table && accountId > 0 && table->entries.contains(accountId);
}

[[nodiscard]] inline bool ProfileSetupHeld(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId) {
	if (!table || accountId <= 0) {
		return false;
	}
	const auto found = table->entries.find(accountId);
	return found != table->entries.end()
		&& found->second.state == ProfileSetupSlotState::Held
		&& found->second.lease != nullptr;
}

struct ProfileSetupProgressView {
	int contextId = 0;
	int progress = 0;
	QString comment;
};

[[nodiscard]] inline std::optional<ProfileSetupProgressView>
ParseProfileSetupProgress(const QJsonObject &envelope) {
	const auto contextId = ProfileSetupExactId(
		envelope.value(QStringLiteral("contextId")));
	if (!contextId) {
		return std::nullopt;
	}
	const auto event = envelope.value(QStringLiteral("event"));
	if (!event.isObject()) {
		return std::nullopt;
	}
	const auto object = event.toObject();
	const auto kind = object.value(QStringLiteral("kind")).toString();
	if (kind != QStringLiteral("ConfigureProgress")
		&& kind != QStringLiteral("ImexProgress")) {
		return std::nullopt;
	}
	auto view = ProfileSetupProgressView();
	view.contextId = *contextId;
	view.progress = object.value(QStringLiteral("progress")).toInt();
	view.comment = object.value(QStringLiteral("comment")).toString();
	return view;
}

[[nodiscard]] inline std::optional<QString> ProfileSetupProgressStatus(
		const QJsonObject &envelope,
		const std::shared_ptr<ProfileSetupTable> &table,
		int expectedContextId) {
	if (!ProfileSetupHeld(table, expectedContextId)) {
		return std::nullopt;
	}
	const auto view = ParseProfileSetupProgress(envelope);
	if (!view || view->contextId != expectedContextId) {
		return std::nullopt;
	}
	if (!view->comment.isEmpty()) {
		return view->comment;
	}
	if (view->progress > 0) {
		return QStringLiteral("%1%").arg(view->progress / 10);
	}
	return std::nullopt;
}

[[nodiscard]] inline bool ProfileSetupOpenReady(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId) {
	if (!table || accountId <= 0) {
		return false;
	}
	const auto found = table->entries.find(accountId);
	return found != table->entries.end()
		&& found->second.state == ProfileSetupSlotState::Completed;
}

[[nodiscard]] inline bool ProfileSetupClosed(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId) {
	if (!table || accountId <= 0) {
		return false;
	}
	const auto found = table->entries.find(accountId);
	return found != table->entries.end()
		&& found->second.state == ProfileSetupSlotState::Closed;
}

inline void ProfileSetupNoteOpened(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId) {
	if (!table || accountId <= 0) {
		return;
	}
	const auto found = table->entries.find(accountId);
	if (found != table->entries.end()
		&& found->second.state == ProfileSetupSlotState::Completed
		&& !found->second.lease) {
		table->entries.erase(found);
	}
}

[[nodiscard]] inline std::shared_ptr<ProfileSetupLease> TryAcquireProfileSetup(
		const std::shared_ptr<ProfileSetupTable> &table,
		int accountId,
		ProfileSetupKind kind) {
	if (!table || accountId <= 0) {
		return nullptr;
	}
	if (table->entries.contains(accountId)) {
		return nullptr;
	}
	auto lease = std::make_shared<ProfileSetupLease>();
	lease->table = table;
	lease->accountId = accountId;
	lease->kind = kind;
	ProfileSetupTable::Entry entry;
	entry.kind = kind;
	entry.state = ProfileSetupSlotState::Held;
	entry.lease = lease.get();
	table->entries.emplace(accountId, entry);
	return lease;
}

inline void ReleaseProfileSetup(const std::shared_ptr<ProfileSetupLease> &lease) {
	if (lease) {
		lease->release();
	}
}

// Live onboarding can disappear while Core is still configuring/importing.
// Retain the shared transport through every reply, including error readback,
// and release it outside its own read/failure handler.
[[nodiscard]] inline AccountInventoryRequest MakeOwnedProfileSetupRequest(
		std::shared_ptr<DeltaRpc> rpc) {
	return [rpc = std::move(rpc)](
			QString method,
			QJsonArray params,
			DeltaRpc::Callback done) {
		CallOwnedDeltaRpc(rpc, method, params, std::move(done));
	};
}

inline void CommitProfileSetup(
		AccountInventoryRequest request,
		std::shared_ptr<ProfileSetupLease> lease,
		const QString &method,
		const QJsonArray &params,
		std::function<void(ProfileSetupResult)> done) {
	const auto finish = [done = std::move(done)](ProfileSetupResult result) {
		if (done) {
			done(std::move(result));
		}
	};
	auto denied = ProfileSetupResult();
	denied.outcome = ProfileSetupOutcome::Denied;
	denied.error = kProfileSetupUnavailable;
	if (!lease
		|| !lease->table
		|| lease->consumed
		|| lease->accountId <= 0
		|| !request
		|| method.isEmpty()
		|| method == QStringLiteral("start_io")
		|| method == QStringLiteral("add_account")) {
		denied.accountId = lease ? lease->accountId : 0;
		ReleaseProfileSetup(lease);
		finish(std::move(denied));
		return;
	}
	const auto captured = ProfileSetupExactId(
		params.isEmpty() ? QJsonValue() : params.at(0));
	if (!captured || *captured != lease->accountId) {
		denied.accountId = lease->accountId;
		ReleaseProfileSetup(lease);
		finish(std::move(denied));
		return;
	}
	const auto table = lease->table;
	const auto accountId = lease->accountId;
	const auto current = lease.get();
	const auto isCurrent = [table, accountId, current] {
		if (!table) {
			return false;
		}
		const auto found = table->entries.find(accountId);
		return found != table->entries.end()
			&& found->second.state == ProfileSetupSlotState::Held
			&& found->second.lease == current;
	};
	if (!isCurrent()) {
		ReleaseProfileSetup(lease);
		denied.accountId = accountId;
		finish(std::move(denied));
		return;
	}
	request(method, params, [
		request,
		table,
		lease,
		accountId,
		current,
		isCurrent,
		finish
	](QJsonObject reply) {
		if (!isCurrent()) {
			return;
		}
		auto result = ProfileSetupResult();
		result.accountId = accountId;
		result.wrote = true;
		if (ProfileSetupAcked(reply)) {
			result.acknowledged = true;
			result.configured = true;
			result.outcome = ProfileSetupOutcome::Succeeded;
			auto found = table->entries.find(accountId);
			if (found != table->entries.end() && found->second.lease == current) {
				found->second.state = ProfileSetupSlotState::Completed;
				found->second.lease = nullptr;
			}
			lease->consumed = true;
			finish(std::move(result));
			return;
		}
		result.error = ProfileSetupReplyError(reply);
		request(QStringLiteral("is_configured"), { accountId }, [
			table,
			lease,
			accountId,
			current,
			isCurrent,
			finish,
			result
		](QJsonObject configured) mutable {
			if (!isCurrent()) {
				return;
			}
			const auto value = configured.value(QStringLiteral("result"));
			if (configured.contains(QStringLiteral("error")) || !value.isBool()) {
				result.outcome = ProfileSetupOutcome::Closed;
				result.error = kProfileSetupUnreadable;
				auto found = table->entries.find(accountId);
				if (found != table->entries.end() && found->second.lease == current) {
					found->second.state = ProfileSetupSlotState::Closed;
					found->second.lease = nullptr;
				}
				lease->consumed = true;
				finish(std::move(result));
				return;
			}
			if (value.toBool()) {
				result.outcome = ProfileSetupOutcome::OpenOnly;
				result.configured = true;
				result.error = kProfileSetupUnconfirmed;
				auto found = table->entries.find(accountId);
				if (found != table->entries.end() && found->second.lease == current) {
					found->second.state = ProfileSetupSlotState::Completed;
					found->second.lease = nullptr;
				}
				lease->consumed = true;
				finish(std::move(result));
				return;
			}
			result.outcome = ProfileSetupOutcome::Retryable;
			if (result.error.isEmpty()) {
				result.error = kProfileSetupUnconfirmed;
			}
			lease->release();
			finish(std::move(result));
		});
	});
}

inline void ImportProfileBackup(
		AccountInventoryRequest request,
		std::shared_ptr<ProfileSetupLease> lease,
		const QString &path,
		std::function<void(ProfileSetupResult)> done,
		const QString &passphrase = {}) {
	if (!lease || path.isEmpty() || lease->kind != ProfileSetupKind::Import) {
		auto denied = ProfileSetupResult();
		denied.outcome = ProfileSetupOutcome::Denied;
		denied.accountId = lease ? lease->accountId : 0;
		denied.error = kProfileSetupUnavailable;
		ReleaseProfileSetup(lease);
		if (done) {
			done(std::move(denied));
		}
		return;
	}
	const auto accountId = lease->accountId;
	CommitProfileSetup(
		std::move(request),
		std::move(lease),
		QStringLiteral("import_backup"),
		QJsonArray{ accountId, path, passphrase.isEmpty()
			? QJsonValue() : QJsonValue(passphrase) },
		std::move(done));
}

inline void CopyProfileBackup(
		AccountInventoryRequest request,
		std::shared_ptr<ProfileSetupLease> lease,
		const QString &qr,
		std::function<void(ProfileSetupResult)> done) {
	if (!lease || qr.isEmpty() || lease->kind != ProfileSetupKind::Copy) {
		auto denied = ProfileSetupResult();
		denied.outcome = ProfileSetupOutcome::Denied;
		denied.accountId = lease ? lease->accountId : 0;
		denied.error = kProfileSetupUnavailable;
		ReleaseProfileSetup(lease);
		if (done) {
			done(std::move(denied));
		}
		return;
	}
	const auto accountId = lease->accountId;
	CommitProfileSetup(
		std::move(request),
		std::move(lease),
		QStringLiteral("get_backup"),
		QJsonArray{ accountId, qr },
		std::move(done));
}

} // namespace Delta
