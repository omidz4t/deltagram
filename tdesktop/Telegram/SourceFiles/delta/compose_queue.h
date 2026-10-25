#pragma once

#include "draft_store.h"

#include <set>

namespace Delta {

struct ForwardSource {
	int chat = 0;
	int message = 0;
};

enum class ForwardState { Pending, Attempted, Confirmed };

struct ComposeIntent {
	ComposeDraft draft;
	std::optional<ForwardSource> forward;
};

struct QueuedCompose {
	QString id;
	ComposeDraft draft;
	int messageId = 0;
	std::optional<ForwardSource> forward;
	ForwardState forwardState = ForwardState::Pending;
};

struct ComposeQueue {
	QString id;
	std::vector<QueuedCompose> entries;
};

// UI compose intent lives in Core's account-local ui.* config. Core also owns
// the attachment bytes; separate $BLOBDIR pins keep them through housekeeping.
// This class never sends a message or retries an ambiguous mutation.
class ComposeQueueStore final {
public:
	using Done = std::function<void(ComposeQueue, QString)>;
	explicit ComposeQueueStore(std::shared_ptr<DeltaRpc> rpc);
	ComposeQueueStore(const ComposeQueueStore&) = delete;
	ComposeQueueStore &operator=(const ComposeQueueStore&) = delete;
	void load(int account, int chat, Done done);
	// Read-only UI snapshot; mutations must reload and validate the reviewed IDs.
	void inspect(int account, int chat, Done done);
	// Keep the source owners until success. On an error, load again before any
	// retry: the returned queue may already be durable after a lost readback.
	// An unrelated existing queue is never returned as this stage's candidate.
	void stage(int account, int chat, std::vector<ComposeDraft> drafts, Done done);
	void stagePlan(int account, int chat, std::vector<ComposeIntent> intents, Done done);
	// Reconcile orphan pins after an interrupted stage/removal. Call while this
	// account is idle. Malformed/unreadable queues abort cleanup without deletion.
	void cleanup(int account, std::function<void(QString)> done);
	// Persist Core's known draft ID before allowing a send. Only the first item
	// can advance, and a known ID cannot be replaced by a different one.
	void checkpoint(int account, int chat, const QString &queueId,
		const QString &entryId, int messageId, Done done,
		std::optional<ComposeDraft> prepared = std::nullopt);
	// Call only after reconciling a send or an explicit user discard. The queue
	// is updated before removing its blob pin. Errors require a fresh load.
	void removeFirst(int account, int chat, const QString &queueId,
		const QString &entryId, Done done);
	void markForward(int account, int chat, const QString &queueId,
		const QString &entryId, ForwardState state, Done done);
	[[nodiscard]] bool busy(int account, int chat) const;

private:
	using Key = std::pair<int, int>;
	using ReadDone = std::function<void(ComposeQueue, QString, QString)>;
	struct Staging;
	struct Cleaning;
	bool begin(Key key, const Done &done);
	void finish(Key key, ComposeQueue queue, QString error, Done done);
	void call(QString method, QJsonArray params, std::function<void(QJsonObject)> done);
	void read(Key key, ReadDone done);
	void write(int account, QString key, QJsonValue value, std::function<void(QString, bool)> done);
	void stageNext(std::shared_ptr<Staging> state);
	void stageForward(std::shared_ptr<Staging> state, ForwardSource source);
	void abandonStage(std::shared_ptr<Staging> state, QString error, size_t index = 0);
	void changeFirst(Key key, QString queueId, QString entryId, int messageId, Done done,
		std::optional<ComposeDraft> prepared = std::nullopt,
		std::optional<ForwardState> forwardState = std::nullopt);
	void inspectCleanup(std::shared_ptr<Cleaning> state, size_t index = 0);
	void releaseOrphans(std::shared_ptr<Cleaning> state, size_t index = 0);
	void finishCleanup(std::shared_ptr<Cleaning> state, QString error);
	std::shared_ptr<DeltaRpc> _rpc;
	std::set<Key> _busy;
	std::set<int> _cleaning;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

} // namespace Delta
