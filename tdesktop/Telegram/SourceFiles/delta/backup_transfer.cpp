#include "backup_transfer.h"

#include <QTimer>

using namespace Qt::StringLiterals;

namespace Delta {
namespace {

QString Error(const QJsonObject &reply) {
	return reply.value(u"error"_s).toObject().value(u"message"_s)
		.toString(u"Backup transfer failed."_s);
}

} // namespace

struct BackupTransfers::Transfer : std::enable_shared_from_this<Transfer> {
	std::weak_ptr<DeltaRpc> rpc;
	int accountId = 0;
	Ready ready;
	Finished done;
	bool finished = false;
	bool canceled = false;
	bool providerReady = false;
	bool stopRequested = false;
	QString reportedError;

	void cancel() {
		canceled = true;
		// Before readiness, stop_ongoing_process could cancel an unrelated job
		// or do nothing, allowing the delayed provider to start afterward.
		if (finished || !providerReady || stopRequested) return;
		stopRequested = true;
		if (const auto server = rpc.lock()) {
			server->call(u"stop_ongoing_process"_s, { accountId }, [](QJsonObject) {});
		}
	}

	void readQr() {
		const auto server = rpc.lock();
		if (!server || finished) return;
		const auto self = shared_from_this();
		server->call(u"get_backup_qr"_s, { accountId }, [self](QJsonObject reply) {
			if (self->finished) return;
			const auto qr = reply.value(u"result"_s).toString();
			if (reply.contains(u"error"_s) || qr.isEmpty()) {
				const auto error = reply.contains(u"error"_s)
					? Error(reply) : u"Backup QR code is empty."_s;
				if (!self->canceled && self->ready && self->reportedError != error) {
					self->reportedError = error;
					self->ready({}, error);
				}
				// Core's readiness read expires after 60 seconds. Keep ownership
				// until provide_backup completes; only this read may be retried.
				if (const auto server = self->rpc.lock(); server && server->running()) {
					QTimer::singleShot(1000, server.get(), [self] { self->readQr(); });
				}
				return;
			}
			self->providerReady = true;
			if (self->canceled) {
				self->cancel();
				return;
			}
			if (const auto server = self->rpc.lock()) {
				server->call(u"create_qr_svg"_s, { qr }, [self](QJsonObject svg) {
					if (self->finished || self->canceled || !self->ready) return;
					self->ready(svg.value(u"result"_s).toString(),
						svg.contains(u"error"_s) ? Error(svg) : QString());
				});
			}
		});
	}
};

BackupTransfers::BackupTransfers(std::shared_ptr<DeltaRpc> rpc)
: _rpc(std::move(rpc)) {
}

BackupTransfers::~BackupTransfers() {
	for (const auto &weak : _active) {
		if (const auto transfer = weak.lock()) transfer->cancel();
	}
}

BackupTransfers::Cancel BackupTransfers::start(
		int accountId, Ready ready, Finished finished) {
	if (const auto previous = _active.value(accountId).lock(); previous && !previous->finished) {
		if (finished) finished(u"A backup transfer is already in progress."_s);
		return {};
	}
	const auto server = _rpc.lock();
	if (!server || !server->running()) {
		if (finished) finished(u"Delta Chat is not running."_s);
		return {};
	}
	const auto transfer = std::make_shared<Transfer>();
	transfer->rpc = server;
	transfer->accountId = accountId;
	transfer->ready = std::move(ready);
	transfer->done = std::move(finished);
	_active.insert(accountId, transfer);
	server->call(u"provide_backup"_s, { accountId }, [transfer](QJsonObject reply) {
		transfer->finished = true;
		transfer->ready = {};
		if (const auto done = std::move(transfer->done)) {
			done(reply.contains(u"error"_s) ? Error(reply) : QString());
		}
	});
	transfer->readQr();
	return [transfer] { transfer->cancel(); };
}

} // namespace Delta
