#include "compose_queue.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUuid>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace Delta {
namespace {

QString QueueKey(int chat) {
	return u"ui.deltagram.compose.queue.%1"_s.arg(chat);
}

QString PinKey(const QString &id) {
	return u"ui.deltagram.compose.blob."_s + id;
}

QString NewId() {
	return QUuid::createUuid().toString(QUuid::Id128);
}

bool ValidId(const QString &id) {
	return id.size() == 32 && std::all_of(id.begin(), id.end(), [](QChar c) {
		return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
	});
}

bool ValidBlob(const QString &name) {
	return !name.isEmpty() && name != u"."_s && name != u".."_s
		&& !name.contains(u'/') && !name.contains(u'\\') && !name.contains(QChar(0));
}

bool ValidNumber(const QJsonValue &value) {
	return value.isDouble() && value.toDouble() >= 0
		&& value.toDouble() <= std::numeric_limits<int>::max()
		&& value.toDouble() == value.toInt();
}

void CopyNamedAttachment(const ComposeDraft &source,
		std::function<void(std::shared_ptr<QTemporaryDir>, QString)> done) {
	static auto pool = QThreadPool();
	pool.setMaxThreadCount(1);
	pool.start([=] {
		auto directory = std::make_shared<QTemporaryDir>();
		const auto name = QFileInfo(source.filename.isEmpty() ? source.file : source.filename).fileName();
		auto path = directory->isValid() && ValidBlob(name) ? directory->filePath(name) : QString();
		if (path.isEmpty() || !QFile::copy(source.file, path)
			|| !QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
			path.clear();
		}
		QMetaObject::invokeMethod(QCoreApplication::instance(), [=] {
			done(directory, path);
		});
	});
}

QString Error(const QJsonObject &reply) {
	return reply.contains(u"error"_s)
		? reply[u"error"_s].toObject()[u"message"_s].toString(u"Core storage failed."_s)
		: QString();
}

QJsonValue Encode(int chat, const ComposeQueue &queue) {
	if (queue.entries.empty()) return QJsonValue(QJsonValue::Null);
	auto entries = QJsonArray();
	auto version = 1;
	for (const auto &entry : queue.entries) {
		auto value = QJsonObject{
			{u"id"_s, entry.id}, {u"blob"_s, entry.draft.file.isEmpty() ? QString() : QFileInfo(entry.draft.file).fileName()},
			{u"text"_s, entry.draft.text}, {u"quote"_s, entry.draft.quotedMessageId},
			{u"filename"_s, entry.draft.filename}, {u"viewType"_s, entry.draft.viewType},
			{u"messageId"_s, entry.messageId},
		};
		if (entry.forward) {
			version = 2;
			value[u"forward"_s] = QJsonObject{
				{u"chat"_s, entry.forward->chat}, {u"message"_s, entry.forward->message},
				{u"state"_s, int(entry.forwardState)},
			};
		} else if (entry.draft.file.isEmpty()) version = 2;
		entries.append(std::move(value));
	}
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{u"version"_s, version}, {u"chat"_s, chat}, {u"id"_s, queue.id}, {u"entries"_s, entries},
	}).toJson(QJsonDocument::Compact));
}

bool Decode(int chat, const QJsonValue &value, const QString &blobdir, ComposeQueue &queue) {
	if (value.isNull()) return true;
	if (!value.isString()) return false;
	QJsonParseError error;
	const auto document = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) return false;
	const auto object = document.object();
	const auto extended = object[u"version"_s] == QJsonValue(2);
	if ((!extended && object[u"version"_s] != QJsonValue(1)) || object[u"chat"_s] != QJsonValue(chat)
		|| !ValidId(object[u"id"_s].toString()) || !object[u"entries"_s].isArray()) return false;
	queue.id = object[u"id"_s].toString();
	auto ids = std::set<QString>();
	for (const auto &value : object[u"entries"_s].toArray()) {
		const auto entry = value.toObject();
		const auto id = entry[u"id"_s].toString();
		const auto blob = entry[u"blob"_s].toString();
		if (!ValidId(id) || !ids.insert(id).second || !entry[u"blob"_s].isString()
			|| ((!extended || !blob.isEmpty()) && !ValidBlob(blob))
			|| !entry[u"text"_s].isString() || !entry[u"filename"_s].isString()
			|| !entry[u"viewType"_s].isString() || !ValidNumber(entry[u"quote"_s])
			|| !ValidNumber(entry[u"messageId"_s])) return false;
		auto forward = std::optional<ForwardSource>();
		auto forwardState = ForwardState::Pending;
		if (entry.contains(u"forward"_s)) {
			const auto value = entry[u"forward"_s].toObject();
			if (!extended || !blob.isEmpty() || !ValidNumber(value[u"chat"_s])
				|| !ValidNumber(value[u"message"_s]) || !ValidNumber(value[u"state"_s])
				|| value[u"chat"_s].toInt() <= 0 || value[u"message"_s].toInt() <= 0
				|| value[u"state"_s].toInt() > int(ForwardState::Confirmed)
				|| entry[u"messageId"_s].toInt() || entry[u"quote"_s].toInt()
				|| entry[u"viewType"_s].toString().isEmpty()) return false;
			forward = ForwardSource{ value[u"chat"_s].toInt(), value[u"message"_s].toInt() };
			forwardState = ForwardState(value[u"state"_s].toInt());
		} else if (blob.isEmpty() && (entry[u"text"_s].toString().trimmed().isEmpty()
			|| entry[u"viewType"_s].toString() != u"Text"_s || !entry[u"filename"_s].toString().isEmpty())) return false;
		queue.entries.push_back({ id, ComposeDraft{
			entry[u"text"_s].toString(), entry[u"quote"_s].toInt(),
			blob.isEmpty() ? QString() : QDir(blobdir).filePath(blob), entry[u"filename"_s].toString(),
			entry[u"viewType"_s].toString(),
		}, entry[u"messageId"_s].toInt(), forward, forwardState });
	}
	return !queue.entries.empty();
}

} // namespace

struct ComposeQueueStore::Staging {
	Key key;
	ComposeQueue queue;
	std::vector<ComposeIntent> sources;
	QString blobdir;
	std::vector<QString> pins;
	Done done;
};

struct ComposeQueueStore::Cleaning {
	int account = 0;
	std::vector<int> chats;
	std::vector<QString> pins;
	std::set<QString> referenced;
	std::function<void(QString)> done;
};

ComposeQueueStore::ComposeQueueStore(std::shared_ptr<DeltaRpc> rpc) : _rpc(std::move(rpc)) {
}

bool ComposeQueueStore::busy(int account, int chat) const {
	return _cleaning.contains(account) || _busy.contains({ account, chat });
}

bool ComposeQueueStore::begin(Key key, const Done &done) {
	if (key.first <= 0 || key.second <= 0 || _cleaning.contains(key.first) || !_busy.insert(key).second) {
		if (done) done({}, u"This compose queue is unavailable or already changing."_s);
		return false;
	}
	return true;
}

void ComposeQueueStore::finish(Key key, ComposeQueue queue, QString error, Done done) {
	_busy.erase(key);
	if (done) done(std::move(queue), std::move(error));
}

void ComposeQueueStore::call(QString method, QJsonArray params,
		std::function<void(QJsonObject)> done) {
	const auto guard = std::weak_ptr<bool>(_alive);
	_rpc->call(method, params, [=](QJsonObject result) {
		if (!guard.expired()) done(std::move(result));
	});
}

void ComposeQueueStore::read(Key key, ReadDone done) {
	call(u"get_config"_s, { key.first, QueueKey(key.second) }, [=, this](QJsonObject reply) {
		const auto error = Error(reply);
		if (!error.isEmpty()) { done({}, {}, error); return; }
		const auto value = reply.value(u"result"_s);
		call(u"get_blob_dir"_s, { key.first }, [=](QJsonObject directory) {
			const auto path = directory[u"result"_s].toString();
			auto queue = ComposeQueue();
			const auto error = Error(directory);
			if (!error.isEmpty() || !QDir::isAbsolutePath(path)) {
				done({}, {}, error.isEmpty() ? u"Core's attachment directory is unavailable."_s : error);
			} else if (!Decode(key.second, value, path, queue)) {
				done({}, {}, u"The saved compose queue is unreadable. It was preserved."_s);
			} else {
				done(std::move(queue), path, {});
			}
		});
	});
}

void ComposeQueueStore::write(int account, QString key, QJsonValue value,
		std::function<void(QString, bool)> done) {
	call(u"set_config"_s, { account, key, value }, [=, this](QJsonObject reply) {
		// A failed acknowledgement can follow a committed write. Read back once;
		// never retry the mutation, and never release blobs on an uncertain result.
		call(u"get_config"_s, { account, key }, [=](QJsonObject read) {
			if (Error(read).isEmpty() && read[u"result"_s] == value) {
				done({}, false);
			} else {
				const auto error = Error(reply);
				done(error.isEmpty() ? u"Could not confirm compose queue storage. Reload it before continuing."_s : error,
					!Error(read).isEmpty() || !read.contains(u"result"_s));
			}
		});
	});
}

void ComposeQueueStore::load(int account, int chat, Done done) {
	const auto key = Key(account, chat);
	if (!begin(key, done)) return;
	read(key, [=, this](ComposeQueue queue, QString, QString error) {
		finish(key, std::move(queue), std::move(error), done);
	});
}

void ComposeQueueStore::inspect(int account, int chat, Done done) {
	if (account <= 0 || chat <= 0) {
		if (done) done({}, u"This compose queue is unavailable."_s);
		return;
	}
	read({ account, chat }, [=](ComposeQueue queue, QString, QString error) {
		if (done) done(std::move(queue), std::move(error));
	});
}

void ComposeQueueStore::cleanup(int account, std::function<void(QString)> done) {
	if (account <= 0 || _cleaning.contains(account)
		|| std::any_of(_busy.begin(), _busy.end(), [=](Key key) { return key.first == account; })) {
		if (done) done(u"Wait for compose queue changes before cleaning storage."_s);
		return;
	}
	_cleaning.insert(account);
	auto state = std::make_shared<Cleaning>();
	state->account = account;
	state->done = std::move(done);
	call(u"get_all_ui_config_keys"_s, { account }, [=, this](QJsonObject reply) {
		const auto error = Error(reply);
		if (!error.isEmpty() || !reply[u"result"_s].isArray()) {
			finishCleanup(state, error.isEmpty() ? u"Could not inspect compose storage."_s : error);
			return;
		}
		for (const auto &value : reply[u"result"_s].toArray()) {
			const auto key = value.toString();
			if (key.startsWith(u"ui.deltagram.compose.queue."_s)) {
				auto ok = false;
				const auto chat = key.sliced(QString(u"ui.deltagram.compose.queue."_s).size()).toInt(&ok);
				if (!ok || chat <= 0 || key != QueueKey(chat)) {
					finishCleanup(state, u"An unknown compose queue was preserved; storage cleanup stopped."_s);
					return;
				}
				state->chats.push_back(chat);
			} else if (key.startsWith(u"ui.deltagram.compose.blob."_s)) {
				const auto id = key.sliced(QString(u"ui.deltagram.compose.blob."_s).size());
				if (ValidId(id)) state->pins.push_back(id);
			}
		}
		inspectCleanup(state);
	});
}

void ComposeQueueStore::inspectCleanup(std::shared_ptr<Cleaning> state, size_t index) {
	if (index == state->chats.size()) {
		releaseOrphans(state);
		return;
	}
	read({ state->account, state->chats[index] }, [=, this](ComposeQueue queue, QString, QString error) {
		if (!error.isEmpty()) { finishCleanup(state, std::move(error)); return; }
		for (const auto &entry : queue.entries) if (!entry.draft.file.isEmpty()) state->referenced.insert(entry.id);
		inspectCleanup(state, index + 1);
	});
}

void ComposeQueueStore::releaseOrphans(std::shared_ptr<Cleaning> state, size_t index) {
	while (index < state->pins.size() && state->referenced.contains(state->pins[index])) ++index;
	if (index == state->pins.size()) {
		finishCleanup(state, {});
		return;
	}
	write(state->account, PinKey(state->pins[index]), QJsonValue(QJsonValue::Null),
		[=, this](QString error, bool) {
			if (!error.isEmpty()) finishCleanup(state, std::move(error));
			else releaseOrphans(state, index + 1);
		});
}

void ComposeQueueStore::finishCleanup(std::shared_ptr<Cleaning> state, QString error) {
	_cleaning.erase(state->account);
	if (state->done) state->done(std::move(error));
}

void ComposeQueueStore::stage(int account, int chat, std::vector<ComposeDraft> drafts, Done done) {
	if (std::any_of(drafts.begin(), drafts.end(), [](const ComposeDraft &draft) { return draft.file.isEmpty(); })) {
		if (done) done({}, u"A selected attachment is missing. Nothing was queued."_s);
		return;
	}
	auto intents = std::vector<ComposeIntent>();
	for (auto &draft : drafts) intents.push_back({ std::move(draft), {} });
	stagePlan(account, chat, std::move(intents), std::move(done));
}

void ComposeQueueStore::stagePlan(int account, int chat, std::vector<ComposeIntent> intents, Done done) {
	const auto key = Key(account, chat);
	if (!begin(key, done)) return;
	if (intents.empty() || std::any_of(intents.begin(), intents.end(), [](const ComposeIntent &intent) {
		if (intent.forward) return intent.forward->chat <= 0 || intent.forward->message <= 0;
		const auto &draft = intent.draft;
		return draft.quotedMessageId < 0 || (draft.file.isEmpty()
			? draft.text.trimmed().isEmpty() || draft.viewType != u"Text"_s || !draft.filename.isEmpty()
			: !QFileInfo(draft.file).isFile());
	})) {
		finish(key, {}, u"A selected message or attachment is unavailable. Nothing was queued."_s, std::move(done));
		return;
	}
	auto state = std::make_shared<Staging>();
	state->key = key;
	state->queue.id = NewId();
	state->sources = std::move(intents);
	state->done = std::move(done);
	read(key, [=, this](ComposeQueue queue, QString blobdir, QString error) {
		if (!error.isEmpty() || !queue.id.isEmpty()) {
			finish(key, {}, error.isEmpty()
				? u"This chat already has a compose queue. Review it before adding another batch."_s
				: error, std::move(state->done));
			return;
		}
		state->blobdir = std::move(blobdir);
		stageNext(state);
	});
}

void ComposeQueueStore::stageNext(std::shared_ptr<Staging> state) {
	const auto key = state->key;
	if (state->queue.entries.size() == state->sources.size()) {
		write(key.first, QueueKey(key.second), Encode(key.second, state->queue), [=, this](QString error, bool uncertain) {
			// Publishing may have committed even if readback failed. Keep every
			// pin in that case; a subsequent load resolves the durable queue.
			if (!error.isEmpty() && !uncertain) {
				abandonStage(state, std::move(error));
			} else {
				finish(key, state->queue, std::move(error), std::move(state->done));
			}
		});
		return;
	}
	const auto intent = state->sources[state->queue.entries.size()];
	if (intent.forward) { stageForward(state, *intent.forward); return; }
	const auto source = intent.draft;
	if (source.file.isEmpty()) {
		state->queue.entries.push_back({ NewId(), source, 0 });
		stageNext(state);
		return;
	}
	const auto guard = std::weak_ptr<bool>(_alive);
	// WHY: Core renames blobs to the supplied filename's extension on draft save.
	// Copy under that name first so queue pins keep addressing the same bytes.
	CopyNamedAttachment(source, [=, this](std::shared_ptr<QTemporaryDir> directory, QString path) {
		if (guard.expired()) return;
		if (path.isEmpty()) {
			abandonStage(state, u"Could not prepare private attachment storage. Nothing was queued."_s);
			return;
		}
		call(u"copy_to_blob_dir"_s, { key.first, path }, [=, this, copy = std::move(directory)](QJsonObject reply) {
			const auto error = Error(reply);
			const auto path = reply[u"result"_s].toString();
			const auto info = QFileInfo(path);
			if (!error.isEmpty() || !QDir::isAbsolutePath(path)
				|| info.absolutePath() != QDir(state->blobdir).absolutePath() || !ValidBlob(info.fileName())) {
				abandonStage(state, error.isEmpty() ? u"Core did not confirm the copied attachment."_s : error);
				return;
			}
			const auto id = NewId();
			state->pins.push_back(id);
			write(key.first, PinKey(id), u"$BLOBDIR/"_s + info.fileName(), [=, this](QString error, bool) {
				if (!error.isEmpty()) { abandonStage(state, std::move(error)); return; }
				auto draft = source;
				draft.file = path;
				state->queue.entries.push_back({ id, std::move(draft), 0 });
				stageNext(state);
			});
		});
	});
}

void ComposeQueueStore::stageForward(std::shared_ptr<Staging> state, ForwardSource source) {
	call(u"get_message"_s, { state->key.first, source.message }, [=, this](QJsonObject reply) {
		const auto message = reply[u"result"_s].toObject();
		const auto status = message[u"state"_s].toInt();
		if (!Error(reply).isEmpty() || message[u"id"_s].toInt() != source.message
			|| message[u"chatId"_s].toInt() != source.chat
			|| !(status == 10 || status == 13 || status == 16 || status == 20 || status == 24 || status == 26 || status == 28)
			|| message[u"viewType"_s].toString().isEmpty()) {
			abandonStage(state, u"A forwarded message is unavailable. Nothing was queued."_s);
			return;
		}
		call(u"get_message_ids"_s, { state->key.first, source.chat, false, false }, [=, this](QJsonObject listed) {
			if (!Error(listed).isEmpty() || !listed[u"result"_s].isArray()
				|| !listed[u"result"_s].toArray().contains(source.message)) {
				abandonStage(state, u"A forwarded message is no longer visible. Nothing was queued."_s);
				return;
			}
			state->queue.entries.push_back({ NewId(), ComposeDraft{
				message[u"text"_s].toString(), 0, {}, message[u"fileName"_s].toString(),
				message[u"viewType"_s].toString(),
			}, 0, source });
			stageNext(state);
		});
	});
}

void ComposeQueueStore::abandonStage(std::shared_ptr<Staging> state, QString error, size_t index) {
	// The queue has not been published, so none of these unique pins can be
	// referenced by a durable queue. Cleanup failure retains a blob, not a send.
	if (index == state->pins.size()) {
		finish(state->key, {}, std::move(error), std::move(state->done));
		return;
	}
	write(state->key.first, PinKey(state->pins[index]), QJsonValue(QJsonValue::Null),
		[=, this](QString cleanupError, bool) {
			abandonStage(state, cleanupError.isEmpty() ? error
				: error + u" Some temporary Core storage could not be cleared."_s, index + 1);
		});
}

void ComposeQueueStore::checkpoint(int account, int chat, const QString &queueId,
		const QString &entryId, int messageId, Done done, std::optional<ComposeDraft> prepared) {
	if (messageId <= 0) {
		if (done) done({}, u"The saved Core message ID is invalid."_s);
		return;
	}
	changeFirst({ account, chat }, queueId, entryId, messageId, std::move(done), std::move(prepared));
}

void ComposeQueueStore::removeFirst(int account, int chat, const QString &queueId,
		const QString &entryId, Done done) {
	changeFirst({ account, chat }, queueId, entryId, 0, std::move(done));
}

void ComposeQueueStore::markForward(int account, int chat, const QString &queueId,
		const QString &entryId, ForwardState state, Done done) {
	changeFirst({ account, chat }, queueId, entryId, 0, std::move(done), {}, state);
}

void ComposeQueueStore::changeFirst(Key key, QString queueId, QString entryId, int messageId, Done done,
		std::optional<ComposeDraft> prepared, std::optional<ForwardState> forwardState) {
	if (!begin(key, done)) return;
	read(key, [=, this](ComposeQueue queue, QString, QString error) {
		if (!error.isEmpty() || queue.id != queueId || queue.entries.empty()
			|| queue.entries.front().id != entryId) {
			finish(key, std::move(queue), error.isEmpty()
				? u"The saved compose queue changed. Reload it before continuing."_s : error, done);
			return;
		}
		const auto pinned = !queue.entries.front().draft.file.isEmpty();
		if (forwardState) {
			auto &entry = queue.entries.front();
			if (!entry.forward || (*forwardState != entry.forwardState
				&& !(*forwardState == ForwardState::Attempted && entry.forwardState == ForwardState::Pending)
				&& !(*forwardState == ForwardState::Confirmed && entry.forwardState == ForwardState::Attempted))) {
				finish(key, std::move(queue), u"This forward cannot change to the requested state."_s, done);
				return;
			}
			entry.forwardState = *forwardState;
		} else if (messageId > 0) {
			auto &entry = queue.entries.front();
			if (entry.forward || (entry.messageId && entry.messageId != messageId)) {
				finish(key, std::move(queue), u"This attachment already has a different Core message ID."_s, done);
				return;
			}
			if (prepared) {
				if (prepared->file != entry.draft.file || prepared->text != entry.draft.text
					|| prepared->quotedMessageId != entry.draft.quotedMessageId) {
					finish(key, std::move(queue), u"Core's saved attachment differs from the queued item."_s, done);
					return;
				}
				entry.draft.filename = prepared->filename;
				entry.draft.viewType = prepared->viewType;
			}
			entry.messageId = messageId;
		} else {
			queue.entries.erase(queue.entries.begin());
			if (queue.entries.empty()) queue.id.clear();
		}
		write(key.first, QueueKey(key.second), Encode(key.second, queue), [=, this](QString error, bool) {
			if (!error.isEmpty() || messageId > 0 || forwardState || !pinned) {
				finish(key, queue, std::move(error), done);
				return;
			}
			write(key.first, PinKey(entryId), QJsonValue(QJsonValue::Null), [=, this](QString error, bool) {
				finish(key, queue, std::move(error), done);
			});
		});
	});
}

} // namespace Delta
