#include "compose_batch.h"

#include <QFileInfo>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QThreadPool>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace Delta {
namespace {

bool SameQueue(const ComposeQueue &a, const ComposeQueue &b) {
	return a.id == b.id && a.entries.size() == b.entries.size()
		&& std::equal(a.entries.begin(), a.entries.end(), b.entries.begin(),
			[](const QueuedCompose &a, const QueuedCompose &b) {
				return a.id == b.id && a.messageId == b.messageId
					&& a.forward.has_value() == b.forward.has_value()
					&& (!a.forward || (a.forward->chat == b.forward->chat
						&& a.forward->message == b.forward->message))
					&& a.forwardState == b.forwardState
					&& a.draft.text == b.draft.text
					&& a.draft.quotedMessageId == b.draft.quotedMessageId
					&& a.draft.file == b.draft.file
					&& a.draft.filename == b.draft.filename
					&& a.draft.viewType == b.draft.viewType;
			});
}

} // namespace

std::shared_ptr<ComposeBatch> ComposeBatch::Create(
		std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, size_t count, Changed changed) {
	if (!queues || account <= 0 || chat <= 0 || !count) return nullptr;
	return std::shared_ptr<ComposeBatch>(new ComposeBatch(
		std::move(queues), account, chat, count, std::move(changed)));
}

std::shared_ptr<ComposeBatch> ComposeBatch::Create(
		std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, std::vector<ComposeDraft> selection, Changed changed) {
	auto result = Create(std::move(queues), account, chat, selection.size(), std::move(changed));
	if (result) result->_selection = std::move(selection);
	return result;
}

ComposeBatch::ComposeBatch(std::shared_ptr<ComposeQueueStore> queues,
		int account, int chat, size_t count, Changed changed)
: _queues(std::move(queues))
, _account(account)
, _chat(chat)
, _slots(count)
, _selection(count)
, _changed(std::move(changed)) {
}

bool ComposeBatch::accepting(size_t index) const {
	return (_state == State::Preparing || _state == State::Failed)
		&& index < _slots.size() && !_slots[index].draft && !_slots[index].forward
		&& !_slots[index].writing
		&& _slots[index].error.isEmpty();
}

bool ComposeBatch::preparedBytes(size_t index, ComposeDraft draft, QByteArray bytes,
		QString temporaryDirectory) {
	if (!accepting(index)) return false;
	if (temporaryDirectory.isEmpty()) temporaryDirectory = QDir::tempPath();
	if (bytes.isEmpty() || draft.quotedMessageId < 0 || draft.filename.isEmpty()
		|| draft.filename == u"."_s || draft.filename == u".."_s
		|| draft.filename.contains(u'/') || draft.filename.contains(u'\\')
		|| draft.filename.contains(QChar(0)) || !QDir::isAbsolutePath(temporaryDirectory)) {
		failed(index, u"The recording could not be prepared. Nothing was sent."_s);
		return false;
	}
	draft.file.clear();
	_selection[index] = std::move(draft);
	_slots[index].bytes = std::move(bytes);
	_slots[index].temporaryDirectory = std::move(temporaryDirectory);
	writeBytes(index);
	return true;
}

void ComposeBatch::writeBytes(size_t index) {
	const auto self = shared_from_this();
	auto &slot = _slots[index];
	slot.writing = true;
	slot.error.clear();
	const auto bytes = slot.bytes;
	const auto name = _selection[index].filename;
	const auto root = slot.temporaryDirectory;
	static auto pool = QThreadPool();
	pool.setMaxThreadCount(1);
	pool.start([=] {
		auto directory = std::make_shared<QTemporaryDir>(QDir(root).filePath(u"delta-recording-XXXXXX"_s));
		auto path = directory->isValid() ? directory->filePath(name) : QString();
		if (!path.isEmpty()) {
			auto file = QFile(path);
			if (!file.open(QIODevice::WriteOnly)
				|| !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
				|| file.write(bytes) != bytes.size() || !file.flush()) path.clear();
		}
		QMetaObject::invokeMethod(QCoreApplication::instance(), [=] {
			self->_slots[index].writing = false;
			if (self->_state == State::Discarded) return;
			if (path.isEmpty()) {
				self->failed(index, u"The recording could not be saved locally. Free some space and retry, or discard it. Nothing was sent."_s);
				return;
			}
			auto draft = self->_selection[index];
			draft.file = path;
			self->prepared(index, std::move(draft), directory);
			if (self->_slots[index].draft) self->_slots[index].bytes.clear();
		});
	});
}

bool ComposeBatch::prepared(size_t index, ComposeDraft draft,
		std::shared_ptr<void> owner) {
	if (!accepting(index)) return false;
	if (draft.quotedMessageId < 0 || !owner
		|| !QFileInfo(draft.file).isFile() || QFileInfo(draft.file).size() <= 0) {
		return failed(index, u"An attachment could not be prepared. No files were sent."_s);
	}
	_selection[index] = draft;
	_slots[index].draft = std::move(draft);
	_slots[index].owner = std::move(owner);
	if (ready()) save();
	else update(_state, _error);
	return true;
}

bool ComposeBatch::failed(size_t index, QString error) {
	if (!accepting(index)) return false;
	if (error.isEmpty()) error = u"An attachment could not be prepared. No files were sent."_s;
	_slots[index].error = error;
	update(State::Failed, std::move(error));
	return true;
}

bool ComposeBatch::preparedText(size_t index, ComposeDraft draft) {
	if (!accepting(index)) return false;
	if (draft.text.trimmed().isEmpty() || draft.quotedMessageId < 0
		|| !draft.file.isEmpty() || !draft.filename.isEmpty() || draft.viewType != u"Text"_s) {
		failed(index, u"The text could not be prepared. Nothing was sent."_s);
		return false;
	}
	_selection[index] = draft;
	_slots[index].draft = std::move(draft);
	if (ready()) save();
	else update(_state, _error);
	return true;
}

bool ComposeBatch::preparedForward(size_t index, ForwardSource source) {
	if (!accepting(index)) return false;
	if (source.chat <= 0 || source.message <= 0) {
		failed(index, u"The forwarded message could not be prepared. Nothing was sent."_s);
		return false;
	}
	_slots[index].forward = source;
	if (ready()) save();
	else update(_state, _error);
	return true;
}

bool ComposeBatch::ready() const {
	return std::all_of(_slots.begin(), _slots.end(), [](const Slot &slot) {
		return slot.draft.has_value() || slot.forward.has_value();
	});
}

void ComposeBatch::save() {
	const auto self = shared_from_this();
	update(State::Saving);
	auto intents = std::vector<ComposeIntent>();
	for (const auto &slot : _slots) intents.push_back({ slot.draft.value_or(ComposeDraft()), slot.forward });
	_queues->stagePlan(_account, _chat, std::move(intents),
		[self](ComposeQueue queue, QString error) {
			self->_queue = std::move(queue);
			const auto state = error.isEmpty() ? State::Saved : State::Failed;
			if (state == State::Saved) self->releaseOwners();
			self->update(state, std::move(error));
		});
}

bool ComposeBatch::retrySave() {
	if (!canRetrySave()) return false;
	if (!ready()) {
		update(State::Preparing);
		if (_state != State::Preparing) return true;
		for (size_t index = 0; index != _slots.size(); ++index) {
			if (!_slots[index].draft && !_slots[index].forward) writeBytes(index);
		}
		return true;
	}
	if (_queue.id.isEmpty()) {
		save();
		return true;
	}
	const auto self = shared_from_this();
	update(State::Saving);
	_queues->load(_account, _chat, [self](ComposeQueue queue, QString error) {
		if (error.isEmpty() && SameQueue(queue, self->_queue)) {
			self->_queue = std::move(queue);
			self->_reconciled = true;
			self->releaseOwners();
			self->update(State::Saved);
		} else {
			self->update(State::Failed, error.isEmpty()
				? u"The saved batch changed. Review its recovery queue before continuing."_s
				: std::move(error));
		}
	});
	return true;
}

bool ComposeBatch::canRetrySave() const {
	return _state == State::Failed
		&& std::all_of(_slots.begin(), _slots.end(), [](const Slot &slot) {
			return !slot.writing && (slot.draft.has_value() || slot.forward.has_value() || !slot.bytes.isEmpty());
		});
}

bool ComposeBatch::discard() {
	if (!canDiscard()) return false;
	const auto self = shared_from_this();
	releaseOwners();
	update(State::Discarded);
	return true;
}

void ComposeBatch::releaseOwners() {
	for (auto &slot : _slots) {
		slot.owner.reset();
		slot.bytes.clear();
	}
}

void ComposeBatch::update(State state, QString error) {
	const auto self = shared_from_this();
	_state = state;
	_error = std::move(error);
	if (_changed) _changed();
}

} // namespace Delta
