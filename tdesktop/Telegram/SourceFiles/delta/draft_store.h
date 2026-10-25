#pragma once

#include "delta_rpc.h"

#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace Delta {

struct ComposeDraft {
	QString text;
	int quotedMessageId = 0;
	QString file;
	QString filename;
	QString viewType;
};

class DraftStore final {
public:
	using Callback = std::function<void(ComposeDraft, QString)>;
	using SendCallback = std::function<void(int, QString)>;
	using PreparedCallback = std::function<void(ComposeDraft)>;
	using Checkpoint = std::function<void(int, ComposeDraft, std::function<void(QString)>)>;
	DraftStore(std::shared_ptr<DeltaRpc> rpc, std::function<void(QString)> error);
	void load(int accountId, int chatId, Callback done);
	void reload(int accountId, int chatId, Callback done);
	void settle(int accountId, int chatId, std::function<void(QString)> done);
	void save(int accountId, int chatId, const QString &text, int quotedMessageId);
	void recoverText(int accountId, int chatId, const QString &text, int quotedMessageId,
		std::function<void(ComposeDraft)> recovered = {});
	void sendText(int accountId, int chatId, const QString &text, int quotedMessageId,
		SendCallback done);
	void sendQueued(int accountId, int chatId, ComposeDraft draft, SendCallback done,
		Checkpoint checkpoint);
	void resumeQueued(int accountId, int chatId, int messageId, ComposeDraft draft,
		SendCallback done);
	// One handoff per chat. On rejection, load() returns Core's managed file
	// path; callers must use it for recovery and must serialize attachment batches.
	// prepared() runs only after Core confirms its durable draft, before send.
	// It can also report a committed save after a lost reply; that case never sends.
	// checkpoint() must durably record the ID/canonical draft, then resolve with
	// an empty error to permit sending. A failed checkpoint leaves the draft intact.
	void sendAttachment(int accountId, int chatId, ComposeDraft draft, SendCallback done,
		PreparedCallback prepared = {}, Checkpoint checkpoint = {});
	// Resume only an already-checkpointed queue item. Read/verify its existing
	// draft ID and contents; never replace the draft or allocate a new message ID.
	void resumeAttachment(int accountId, int chatId, int messageId, ComposeDraft draft,
		SendCallback done, PreparedCallback prepared = {});
	void discardAttachment(int accountId, int chatId, const QString &expectedFile,
		std::function<void(bool)> done);
	[[nodiscard]] QString attachmentName(int accountId, int chatId) const;
	[[nodiscard]] bool sending(int accountId, int chatId) const;
	[[nodiscard]] bool busy(int accountId, int chatId) const;
	[[nodiscard]] bool preparingSend() const;

private:
	using Key = std::pair<int, int>;
	struct Entry {
		struct Send {
			ComposeDraft draft;
			SendCallback done;
			bool started = false;
			bool prepared = false;
			bool discard = false;
			PreparedCallback preparedCallback;
			Checkpoint checkpoint;
			int knownDraftId = 0;
		};
		ComposeDraft value;
		std::optional<Send> send;
		std::optional<std::pair<QString, int>> pending;
		std::vector<Callback> readers;
		std::vector<std::function<void(QString)>> settlers;
		bool loaded = false;
		bool loading = false;
		bool saving = false;
	};
	void ensureLoaded(Key key);
	void beginSend(Key key, ComposeDraft draft, SendCallback done,
		PreparedCallback prepared = {}, Checkpoint checkpoint = {}, int knownDraftId = 0);
	void flush(Key key);
	void startSend(Key key);
	void confirmDraft(Key key, QString saveError);
	void sendSavedDraft(Key key, int id);
	void startDiscard(Key key);
	void finishSend(Key key, int messageId, QString error);
	void notifyReaders(Entry &entry, const QString &error);
	void notifySettled(Entry &entry, const QString &error);
	std::shared_ptr<DeltaRpc> _rpc;
	std::function<void(QString)> _error;
	std::map<Key, Entry> _entries;
	std::shared_ptr<bool> _alive = std::make_shared<bool>(true);
};

} // namespace Delta
