#pragma once

#include "compose_queue.h"

namespace Delta {

class ComposeQueueSender final {
public:
	using Done = std::function<void(int, ComposeQueue, QString)>;
	ComposeQueueSender(std::shared_ptr<DeltaRpc> rpc,
		std::shared_ptr<ComposeQueueStore> queues, std::shared_ptr<DraftStore> drafts);
	ComposeQueueSender(const ComposeQueueSender&) = delete;
	ComposeQueueSender &operator=(const ComposeQueueSender&) = delete;
	// An explicit request advances one reviewed entry; failures never retry it.
	void sendNext(int account, int chat, const QString &queueId,
		const QString &entryId, Done done);
	void discardNext(int account, int chat, const QString &queueId,
		const QString &entryId, Done done);
	[[nodiscard]] bool busy(int account, int chat) const;

private:
	struct Sending;
	void begin(int account, int chat, const QString &queueId,
		const QString &entryId, bool discard, Done done);
	void inspect(std::shared_ptr<Sending> state);
	void forward(std::shared_ptr<Sending> state);
	void markForward(std::shared_ptr<Sending> state, ForwardState next,
		std::function<void()> done);
	void discard(std::shared_ptr<Sending> state);
	void checkpoint(std::shared_ptr<Sending> state, int id, ComposeDraft draft,
		std::function<void(QString)> done);
	void send(std::shared_ptr<Sending> state);
	void advance(std::shared_ptr<Sending> state, int id);
	void finish(std::shared_ptr<Sending> state, int id, QString error);
	void call(QString method, QJsonArray params, std::function<void(QJsonObject)> done);
	std::shared_ptr<DeltaRpc> _rpc;
	std::shared_ptr<ComposeQueueStore> _queues;
	std::shared_ptr<DraftStore> _drafts;
	std::set<std::pair<int, int>> _busy;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

} // namespace Delta
