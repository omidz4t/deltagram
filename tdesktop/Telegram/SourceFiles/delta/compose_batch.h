#pragma once

#include "compose_queue.h"

namespace Delta {

class ComposeBatch final : public std::enable_shared_from_this<ComposeBatch> {
public:
	enum class State { Preparing, Saving, Failed, Saved, Discarded };
	using Changed = std::function<void()>;
	static std::shared_ptr<ComposeBatch> Create(
		std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, size_t count, Changed changed);
	static std::shared_ptr<ComposeBatch> Create(
		std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, std::vector<ComposeDraft> selection, Changed changed);
	bool prepared(size_t index, ComposeDraft draft, std::shared_ptr<void> owner);
	bool preparedText(size_t index, ComposeDraft draft);
	bool preparedForward(size_t index, ForwardSource source);
	bool preparedBytes(size_t index, ComposeDraft draft, QByteArray bytes,
		QString temporaryDirectory = {});
	bool failed(size_t index, QString error);
	bool retrySave();
	bool discard();
	[[nodiscard]] State state() const { return _state; }
	[[nodiscard]] const QString &error() const { return _error; }
	[[nodiscard]] const ComposeQueue &queue() const { return _queue; }
	[[nodiscard]] bool reconciled() const { return _reconciled; }
	[[nodiscard]] const std::vector<ComposeDraft> &selection() const { return _selection; }
	[[nodiscard]] bool canRetrySave() const;
	[[nodiscard]] bool canDiscard() const {
		return (_state == State::Preparing || _state == State::Failed) && _queue.id.isEmpty();
	}

private:
	struct Slot {
		std::optional<ComposeDraft> draft;
		std::optional<ForwardSource> forward;
		std::shared_ptr<void> owner;
		QString error;
		QByteArray bytes;
		QString temporaryDirectory;
		bool writing = false;
	};
	ComposeBatch(std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, size_t count, Changed changed);
	bool accepting(size_t index) const;
	bool ready() const;
	void writeBytes(size_t index);
	void save();
	void update(State state, QString error = {});
	void releaseOwners();
	std::shared_ptr<ComposeQueueStore> _queues;
	const int _account;
	const int _chat;
	std::vector<Slot> _slots;
	std::vector<ComposeDraft> _selection;
	Changed _changed;
	State _state = State::Preparing;
	QString _error;
	ComposeQueue _queue;
	bool _reconciled = false;
};

} // namespace Delta
