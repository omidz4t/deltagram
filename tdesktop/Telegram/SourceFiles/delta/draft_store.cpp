#include "draft_store.h"

#include <QCoreApplication>

#include <utility>

using namespace Qt::StringLiterals;

namespace Delta {
namespace {

QString Failure(const QJsonObject &reply) {
	return reply.value(u"error"_s).toObject()
		.value(u"message"_s).toString(u"Could not save draft."_s);
}

} // namespace

DraftStore::DraftStore(std::shared_ptr<DeltaRpc> rpc, std::function<void(QString)> error)
: _rpc(std::move(rpc)), _error(std::move(error)) {
}

void DraftStore::load(int accountId, int chatId, Callback done) {
	const auto key = Key(accountId, chatId);
	auto &entry = _entries[key];
	entry.readers.push_back(std::move(done));
	ensureLoaded(key);
}

void DraftStore::reload(int accountId, int chatId, Callback done) {
	if (busy(accountId, chatId)) {
		if (done) done({}, u"Wait for the current draft operation before reloading."_s);
		return;
	}
	_entries[Key(accountId, chatId)].loaded = false;
	load(accountId, chatId, std::move(done));
}

void DraftStore::save(int accountId, int chatId, const QString &text, int quotedMessageId) {
	const auto key = Key(accountId, chatId);
	auto &entry = _entries[key];
	// The composer is read-only during the local Core handoff. In particular,
	// a stale donor draft-clear must not delete the message being handed over.
	if (entry.send) return;
	if (!entry.loaded && !entry.pending && text.isEmpty() && !quotedMessageId) return;
	if (entry.loaded && !entry.pending
		&& entry.value.text == text && entry.value.quotedMessageId == quotedMessageId) return;
	entry.pending = std::make_pair(text, quotedMessageId);
	ensureLoaded(key);
}

void DraftStore::settle(int accountId, int chatId, std::function<void(QString)> done) {
	auto &entry = _entries[Key(accountId, chatId)];
	if (entry.send) {
		if (done) done(u"Another message is using this draft."_s);
	} else if (entry.loading || entry.saving) {
		entry.settlers.push_back(std::move(done));
	} else if (done) {
		done(entry.pending ? u"The composer could not be saved. Its text was preserved."_s : QString());
	}
}

void DraftStore::recoverText(
		int accountId,
		int chatId,
		const QString &text,
		int quotedMessageId,
		std::function<void(ComposeDraft)> recovered) {
	if (text.isEmpty()) return;
	const auto guard = std::weak_ptr<bool>(_alive);
	load(accountId, chatId, [=, this](ComposeDraft draft, QString error) {
		if (guard.expired() || !error.isEmpty()) return;
		const auto combined = draft.text.isEmpty()
			? text
			: text + u"\n\n"_s + draft.text;
		const auto quote = draft.text.isEmpty() && draft.file.isEmpty()
			&& !draft.quotedMessageId
			? quotedMessageId
			: draft.quotedMessageId;
		save(accountId, chatId, combined, quote);
		draft.text = combined;
		draft.quotedMessageId = quote;
		if (recovered) recovered(std::move(draft));
	});
}

void DraftStore::ensureLoaded(Key key) {
	auto &entry = _entries[key];
	if (entry.loaded) {
		flush(key);
		notifyReaders(entry, {});
		return;
	}
	if (entry.loading) return;
	entry.loading = true;
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(u"get_draft"_s, { key.first, key.second }, [=, this](QJsonObject reply) {
		if (guard.expired()) return;
		auto &loaded = _entries[key];
		loaded.loading = false;
		if (reply.contains(u"error"_s)) {
			const auto error = Failure(reply);
			notifyReaders(loaded, error);
			notifySettled(loaded, error);
			if (loaded.send) finishSend(key, 0, error);
			if (_error) _error(error);
			return;
		}
		const auto value = reply.value(u"result"_s).toObject();
		loaded.value = ComposeDraft{
			value.value(u"text"_s).toString(),
			value.value(u"quote"_s).toObject().value(u"messageId"_s).toInt(),
			value.value(u"file"_s).toString(),
			value.value(u"fileName"_s).toString(),
			value.value(u"viewType"_s).toString(),
		};
		loaded.loaded = true;
		flush(key);
		notifyReaders(loaded, {});
	});
}

void DraftStore::flush(Key key) {
	auto &entry = _entries[key];
	if (entry.saving || (entry.send && entry.send->started)) return;
	if (!entry.pending) {
		if (entry.send) startSend(key);
		else notifySettled(entry, {});
		return;
	}
	entry.value.text = entry.pending->first;
	entry.value.quotedMessageId = entry.pending->second;
	entry.pending.reset();
	entry.saving = true;
	const auto value = entry.value;
	const auto remove = value.text.isEmpty() && !value.quotedMessageId && value.file.isEmpty();
	const auto params = remove ? QJsonArray{ key.first, key.second } : QJsonArray{
		key.first, key.second, value.text,
		value.file.isEmpty() ? QJsonValue() : QJsonValue(value.file),
		value.filename.isEmpty() ? QJsonValue() : QJsonValue(value.filename),
		value.quotedMessageId > 0 ? QJsonValue(value.quotedMessageId) : QJsonValue(),
		value.viewType.isEmpty() ? QJsonValue() : QJsonValue(value.viewType),
	};
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(remove ? u"remove_draft"_s : u"misc_set_draft"_s, params,
		[=, this](QJsonObject reply) {
			if (guard.expired()) return;
			auto &saved = _entries[key];
			saved.saving = false;
			if (reply.contains(u"error"_s)) {
				if (!saved.pending) saved.pending = std::make_pair(value.text, value.quotedMessageId);
				notifySettled(saved, Failure(reply));
				if (saved.send) finishSend(key, 0, Failure(reply));
				if (_error) _error(Failure(reply));
				return;
			}
			flush(key);
		});
}

bool DraftStore::sending(int accountId, int chatId) const {
	const auto i = _entries.find(Key(accountId, chatId));
	return i != _entries.end() && i->second.send.has_value();
}

bool DraftStore::busy(int accountId, int chatId) const {
	const auto i = _entries.find(Key(accountId, chatId));
	return i != _entries.end() && (i->second.send || i->second.saving
		|| i->second.loading || i->second.pending);
}

bool DraftStore::preparingSend() const {
	for (const auto &[key, entry] : _entries) {
		if (entry.send && !entry.send->prepared) return true;
	}
	return false;
}

void DraftStore::sendText(int accountId, int chatId, const QString &text,
		int quotedMessageId, SendCallback done) {
	beginSend(Key(accountId, chatId), ComposeDraft{ text, quotedMessageId }, std::move(done));
}

void DraftStore::sendAttachment(int accountId, int chatId, ComposeDraft draft,
		SendCallback done, PreparedCallback prepared, Checkpoint checkpoint) {
	if (draft.file.isEmpty()) {
		if (done) done(0, u"The attachment file is missing."_s);
		return;
	}
	beginSend(Key(accountId, chatId), std::move(draft), std::move(done), std::move(prepared), std::move(checkpoint));
}

void DraftStore::sendQueued(int accountId, int chatId, ComposeDraft draft,
		SendCallback done, Checkpoint checkpoint) {
	beginSend(Key(accountId, chatId), std::move(draft), std::move(done), {}, std::move(checkpoint));
}

void DraftStore::resumeQueued(int accountId, int chatId, int messageId, ComposeDraft draft,
		SendCallback done) {
	if (messageId <= 0) {
		if (done) done(0, u"The saved message ID is missing."_s);
		return;
	}
	beginSend(Key(accountId, chatId), std::move(draft), std::move(done), {}, {}, messageId);
}

void DraftStore::resumeAttachment(int accountId, int chatId, int messageId, ComposeDraft draft,
		SendCallback done, PreparedCallback prepared) {
	if (messageId <= 0 || draft.file.isEmpty()) {
		if (done) done(0, u"The saved attachment or message ID is missing."_s);
		return;
	}
	beginSend(Key(accountId, chatId), std::move(draft), std::move(done), std::move(prepared), {}, messageId);
}

void DraftStore::discardAttachment(int accountId, int chatId,
		const QString &expectedFile, std::function<void(bool)> done) {
	const auto key = Key(accountId, chatId);
	auto &entry = _entries[key];
	if (entry.send || expectedFile.isEmpty()) {
		if (done) done(false);
		return;
	}
	entry.send = Entry::Send{
		ComposeDraft{ {}, 0, expectedFile },
		[done = std::move(done)](int, QString error) {
			if (done) done(error.isEmpty());
		}, false, false, true,
	};
	ensureLoaded(key);
}

QString DraftStore::attachmentName(int accountId, int chatId) const {
	const auto i = _entries.find(Key(accountId, chatId));
	if (i == _entries.end() || i->second.value.file.isEmpty()) return {};
	const auto &name = i->second.value.filename;
	return name.isEmpty() ? u"Attachment"_s : name;
}

void DraftStore::startDiscard(Key key) {
	auto &entry = _entries[key];
	entry.send->started = true;
	auto target = entry.value;
	target.file.clear();
	target.filename.clear();
	target.viewType = u"Text"_s;
	const auto remove = target.text.isEmpty() && !target.quotedMessageId;
	const auto params = remove ? QJsonArray{key.first, key.second} : QJsonArray{
		key.first, key.second, target.text, QJsonValue(), QJsonValue(),
		target.quotedMessageId > 0 ? QJsonValue(target.quotedMessageId) : QJsonValue(),
		u"Text"_s,
	};
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(remove ? u"remove_draft"_s : u"misc_set_draft"_s, params,
		[=, this](QJsonObject reply) {
			if (guard.expired()) return;
			// Read back even on an error: Core may have committed the removal.
			_rpc->call(u"get_draft"_s, {key.first, key.second}, [=, this](QJsonObject read) {
				if (guard.expired()) return;
				const auto value = read.value(u"result"_s).toObject();
				const auto confirmed = !read.contains(u"error"_s)
					&& value.value(u"file"_s).toString().isEmpty()
					&& value.value(u"text"_s).toString() == target.text
					&& value.value(u"quote"_s).toObject().value(u"messageId"_s).toInt() == target.quotedMessageId;
				if (confirmed) _entries[key].value = target;
				finishSend(key, 0, confirmed ? QString()
					: u"Could not confirm attachment removal. Review the saved draft before trying again."_s);
			});
		});
}

void DraftStore::beginSend(Key key, ComposeDraft draft, SendCallback done,
		PreparedCallback prepared, Checkpoint checkpoint, int knownDraftId) {
	auto &entry = _entries[key];
	if (entry.send || (draft.text.trimmed().isEmpty() && draft.file.isEmpty())) {
		if (done) done(0, u"A message is already being saved, or its text is empty."_s);
		return;
	}
	entry.send = Entry::Send{ std::move(draft), std::move(done) };
	entry.send->preparedCallback = std::move(prepared);
	entry.send->checkpoint = std::move(checkpoint);
	entry.send->knownDraftId = knownDraftId;
	ensureLoaded(key);
}

void DraftStore::startSend(Key key) {
	auto &entry = _entries[key];
	if (!entry.value.file.isEmpty() && entry.send->draft.file != entry.value.file) {
		finishSend(key, 0, u"This chat already has an attachment draft. It was preserved; review or remove it before sending another message."_s);
		return;
	}
	if (entry.send->discard) {
		startDiscard(key);
		return;
	}
	entry.send->started = true;
	if (entry.send->knownDraftId > 0) {
		confirmDraft(key, {});
		return;
	}
	const auto target = entry.send->draft;
	const auto text = target.text;
	const auto quote = target.quotedMessageId;
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(u"misc_set_draft"_s, {key.first, key.second, text,
		target.file.isEmpty() ? QJsonValue() : QJsonValue(target.file),
		target.filename.isEmpty() ? QJsonValue() : QJsonValue(target.filename),
		quote > 0 ? QJsonValue(quote) : QJsonValue(),
		target.viewType.isEmpty() ? (target.file.isEmpty() ? u"Text"_s : u"File"_s) : target.viewType},
		[=, this](QJsonObject reply) {
			if (guard.expired()) return;
			const auto saveError = reply.contains(u"error"_s) ? Failure(reply) : QString();
			if (!saveError.isEmpty() && !_entries[key].send->preparedCallback && !_entries[key].send->checkpoint) {
				finishSend(key, 0, Failure(reply));
				return;
			}
			_entries[key].value = target;
			confirmDraft(key, saveError);
		});
}

void DraftStore::confirmDraft(Key key, QString saveError) {
	const auto target = _entries[key].send->draft;
	const auto knownId = _entries[key].send->knownDraftId;
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(u"get_draft"_s, {key.first, key.second}, [=, this](QJsonObject read) {
		if (guard.expired()) return;
		const auto draft = read.value(u"result"_s).toObject();
		const auto id = draft.value(u"id"_s).toInt();
		const auto file = draft.value(u"file"_s).toString();
		if (read.contains(u"error"_s) || id <= 0
			|| (knownId > 0 && (id != knownId || file != target.file
				|| draft.value(u"fileName"_s).toString() != target.filename
				|| draft.value(u"viewType"_s).toString() != target.viewType))
			|| draft.value(u"state"_s).toInt() != 19
			|| draft.value(u"chatId"_s).toInt() != key.second
			|| draft.value(u"text"_s).toString() != target.text
			|| draft.value(u"quote"_s).toObject().value(u"messageId"_s).toInt() != target.quotedMessageId
			|| file.isEmpty() != target.file.isEmpty()) {
			finishSend(key, 0, u"Could not confirm the saved draft. Nothing was sent."_s);
			return;
		}
		// Core owns the managed file before the optional queue checkpoint.
		auto &entry = _entries[key];
		entry.value = target;
		entry.value.file = file;
		entry.value.filename = draft.value(u"fileName"_s).toString();
		entry.value.viewType = draft.value(u"viewType"_s).toString();
		entry.send->prepared = true;
		const auto prepared = entry.send->preparedCallback;
		if (prepared) prepared(entry.value);
		if (guard.expired()) return;
		if (!saveError.isEmpty()) {
			// A reconciled save error permits recovery, not an automatic send.
			finishSend(key, 0, saveError);
			return;
		}
		const auto checkpoint = _entries[key].send->checkpoint;
		if (!checkpoint) {
			sendSavedDraft(key, id);
			return;
		}
		const auto resolved = std::make_shared<bool>(false);
		checkpoint(id, _entries[key].value, [=, this](QString error) {
			if (guard.expired() || std::exchange(*resolved, true)) return;
			if (!error.isEmpty()) finishSend(key, 0, std::move(error));
			else sendSavedDraft(key, id);
		});
	});
}

void DraftStore::sendSavedDraft(Key key, int id) {
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(u"misc_send_draft"_s, {key.first, key.second}, [=, this](QJsonObject sent) {
		if (guard.expired()) return;
		if (!sent.contains(u"error"_s) && sent.value(u"result"_s).toInt() == id) {
			finishSend(key, id, {});
			return;
		}
		// Reconcile the known ID instead of retrying a possibly committed send.
		_rpc->call(u"get_message"_s, {key.first, id}, [=, this](QJsonObject checked) {
			if (guard.expired()) return;
			const auto message = checked.value(u"result"_s).toObject();
			const auto state = message.value(u"state"_s).toInt();
			const auto owned = !checked.contains(u"error"_s)
				&& message.value(u"id"_s).toInt() == id
				&& message.value(u"chatId"_s).toInt() == key.second
				&& (state == 20 || state == 24 || state == 26 || state == 28);
			finishSend(key, owned ? id : 0, owned ? QString()
				: u"The send was not confirmed. Check the chat and its saved draft before retrying."_s);
		});
	});
}

void DraftStore::finishSend(Key key, int messageId, QString error) {
	auto &entry = _entries[key];
	const auto done = std::move(entry.send->done);
	entry.send.reset();
	if (messageId > 0) entry.value = {};
	// An error response does not prove that Core rejected the mutation. Reload
	// its draft before another operation, including the managed attachment path.
	if (!error.isEmpty()) entry.loaded = false;
	notifySettled(entry, error);
	if (done) done(messageId, std::move(error));
}

void DraftStore::notifySettled(Entry &entry, const QString &error) {
	if (error.isEmpty() && (entry.loading || entry.saving || entry.pending || entry.send)) return;
	const auto settlers = std::exchange(entry.settlers, {});
	if (settlers.empty()) return;
	const auto guard = std::weak_ptr<bool>(_alive);
	QMetaObject::invokeMethod(QCoreApplication::instance(), [=] {
		if (guard.expired()) return;
		for (const auto &done : settlers) if (done) done(error);
	}, Qt::QueuedConnection);
}

void DraftStore::notifyReaders(Entry &entry, const QString &error) {
	const auto readers = std::exchange(entry.readers, {});
	auto value = entry.value;
	if (entry.pending) {
		value.text = entry.pending->first;
		value.quotedMessageId = entry.pending->second;
	}
	for (const auto &done : readers) if (done) done(value, error);
}

} // namespace Delta
