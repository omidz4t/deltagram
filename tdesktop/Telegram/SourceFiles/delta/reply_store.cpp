#include "reply_store.h"

#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace Delta {
namespace {

QString Prefix() { return u"ui.deltagram.notification-reply."_s; }
QString ConfigKey(const QString &id) { return Prefix() + id; }

bool ValidId(const QString &id) {
	return id.size() == 32 && std::all_of(id.begin(), id.end(), [](QChar c) {
		return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f');
	});
}

bool Integer(const QJsonValue &value) {
	return value.isDouble() && value.toDouble() >= 0
		&& value.toDouble() <= std::numeric_limits<int>::max()
		&& value.toDouble() == value.toInt();
}

QString Encode(const SavedReply &reply) {
	return QString::fromUtf8(QJsonDocument(QJsonObject{
		{u"version"_s, 1}, {u"id"_s, reply.id}, {u"chat"_s, reply.chat},
		{u"text"_s, reply.text}, {u"state"_s, int(reply.state)}, {u"message"_s, reply.message},
	}).toJson(QJsonDocument::Compact));
}

bool Same(const SavedReply &a, const SavedReply &b) {
	return Encode(a) == Encode(b);
}

QString StorageError() {
	return u"The notification reply could not be saved or checked. Review Saved replies before continuing."_s;
}

QString Uncertain() {
	return u"The notification reply may already have been sent. Check the conversation and Saved replies; it will not be sent again automatically."_s;
}

}

struct ReplyStore::Listing {
	int account = 0;
	std::vector<QString> ids;
	std::map<QString, SavedReply> replies;
	QString error;
	Listed done;
};

ReplyStore::ReplyStore(Call call, std::function<void()> changed)
: _call(std::move(call)), _changed(std::move(changed)) {
}

void ReplyStore::call(QString method, QJsonArray params, DeltaRpc::Callback done) {
	const auto guard = std::weak_ptr<bool>(_alive);
	_call(method, params, [guard, done = std::move(done)](QJsonObject reply) {
		if (!guard.expired()) done(std::move(reply));
	});
}

bool ReplyStore::begin(Key key, const Done &done) {
	if (key.first <= 0 || !ValidId(key.second) || !_busy.insert(key).second) {
		if (done) done(0, u"This saved reply is unavailable or already changing."_s);
		return false;
	}
	return true;
}

void ReplyStore::finish(Key key, int message, QString error, Done done) {
	_busy.erase(key);
	if (_changed) _changed();
	if (done) done(message, std::move(error));
}

bool ReplyStore::hasUnsaved(int account) const {
	return std::any_of(_unsaved.begin(), _unsaved.end(), [=](const auto &entry) {
		return !account || entry.first.first == account;
	});
}

void ReplyStore::read(Key key, ReadDone done) {
	call(u"get_config"_s, {key.first, ConfigKey(key.second)}, [=](QJsonObject response) {
		const auto value = response[u"result"_s];
		if (response.contains(u"error"_s) || !response.contains(u"result"_s)) {
			done({}, StorageError());
			return;
		}
		if (value.isNull()) { done({}, {}); return; }
		const auto object = QJsonDocument::fromJson(value.toString().toUtf8()).object();
		const auto state = object[u"state"_s].toInt(-1);
		const auto message = object[u"message"_s].toInt(-1);
		if (!value.isString() || object[u"version"_s] != QJsonValue(1)
			|| object[u"id"_s] != QJsonValue(key.second) || !ValidId(key.second)
			|| !Integer(object[u"chat"_s]) || object[u"chat"_s].toInt() <= 0
			|| !object[u"text"_s].isString() || object[u"text"_s].toString().trimmed().isEmpty()
			|| !Integer(object[u"state"_s]) || state > int(ReplyState::Sent)
			|| !Integer(object[u"message"_s]) || ((state == int(ReplyState::Sent)) != (message > 0))) {
			done({}, StorageError());
			return;
		}
		done(SavedReply{key.second, object[u"chat"_s].toInt(), object[u"text"_s].toString(),
			ReplyState(state), message, true}, {});
	});
}

void ReplyStore::write(int account, SavedReply reply, std::function<void(bool, QString)> done) {
	call(u"set_config"_s, {account, ConfigKey(reply.id), Encode(reply)}, [=, this](QJsonObject response) {
		const auto acknowledged = !response.contains(u"error"_s)
			&& response.contains(u"result"_s) && response[u"result"_s].isNull();
		read({account, reply.id}, [=](std::optional<SavedReply> saved, QString error) {
			const auto durable = error.isEmpty() && saved && Same(*saved, reply);
			done(durable, (durable && acknowledged) ? QString() : StorageError());
		});
	});
}

void ReplyStore::persist(int account, int chat, QString text, Persisted done) {
	if (account <= 0 || chat <= 0 || text.trimmed().isEmpty()) {
		if (done) done({}, u"This notification reply has no conversation or text."_s);
		return;
	}
	auto reply = SavedReply{QUuid::createUuid().toString(QUuid::Id128), chat, std::move(text)};
	const auto key = Key(account, reply.id);
	if (!begin(key, [&](int, QString error) { if (done) done({}, std::move(error)); })) return;
	_unsaved[key] = reply;
	if (_changed) _changed();
	write(account, reply, [=, this](bool durable, QString error) {
		if (durable) _unsaved.erase(key);
		auto saved = error.isEmpty()
			? std::optional<SavedReply>(reply)
			: std::nullopt;
		finish(key, 0, {}, nullptr);
		if (done) done(std::move(saved), std::move(error));
	});
}

void ReplyStore::submit(int account, int chat, QString text, Done done) {
	persist(account, chat, std::move(text), [=, this](std::optional<SavedReply> saved, QString error) {
		if (!saved) {
			if (done) done(0, std::move(error));
			return;
		}
		if (!begin(Key(account, saved->id), done)) return;
		attempt(account, *saved, done);
	});
}

void ReplyStore::attempt(int account, SavedReply reply, Done done) {
	reply.state = ReplyState::Attempted;
	const auto key = Key(account, reply.id);
	write(account, reply, [=, this](bool, QString error) {
		if (!error.isEmpty()) { finish(key, 0, std::move(error), done); return; }
		call(u"send_msg"_s, {account, reply.chat, QJsonObject{{u"text"_s, reply.text}}},
			[=, this](QJsonObject sent) mutable {
				const auto value = sent[u"result"_s];
				if (sent.contains(u"error"_s) || !Integer(value) || value.toInt() <= 0) {
					finish(key, 0, Uncertain(), done);
					return;
				}
				reply.message = value.toInt();
				call(u"get_message"_s, {account, reply.message}, [=, this](QJsonObject checked) mutable {
					const auto message = checked[u"result"_s].toObject();
					const auto state = message[u"state"_s].toInt();
					if (checked.contains(u"error"_s) || message[u"id"_s] != QJsonValue(reply.message)
						|| message[u"chatId"_s] != QJsonValue(reply.chat) || message[u"text"_s] != QJsonValue(reply.text)
						|| !(state == 20 || state == 24 || state == 26 || state == 28)) {
						finish(key, 0, Uncertain(), done);
						return;
					}
					reply.state = ReplyState::Sent;
					write(account, reply, [=, this](bool, QString error) {
						if (!error.isEmpty()) finish(key, reply.message, std::move(error), done);
						else clear(account, reply, done);
					});
				});
			});
	});
}

void ReplyStore::clear(int account, SavedReply reply, Done done) {
	const auto key = Key(account, reply.id);
	call(u"set_config"_s, {account, ConfigKey(reply.id), QJsonValue(QJsonValue::Null)},
		[=, this](QJsonObject) {
			read(key, [=, this](std::optional<SavedReply> saved, QString error) {
				if (!error.isEmpty() || saved) { finish(key, 0, StorageError(), done); return; }
				_unsaved.erase(key);
				finish(key, reply.message, {}, done);
			});
		});
}

void ReplyStore::send(int account, SavedReply reviewed, Done done) {
	const auto key = Key(account, reviewed.id);
	if (!begin(key, done)) return;
	read(key, [=, this](std::optional<SavedReply> saved, QString error) {
		if (!error.isEmpty()) { finish(key, 0, std::move(error), done); return; }
		if (reviewed.state != ReplyState::Pending || (saved && saved->state != ReplyState::Pending)) {
			finish(key, 0, Uncertain(), done);
		} else if (saved && Same(*saved, reviewed)) {
			_unsaved.erase(key);
			attempt(account, *saved, done);
		} else if (!saved && _unsaved.contains(key) && Same(_unsaved.at(key), reviewed)) {
			write(account, reviewed, [=, this](bool durable, QString error) {
				if (durable) _unsaved.erase(key);
				if (!error.isEmpty()) finish(key, 0, std::move(error), done);
				else attempt(account, reviewed, done);
			});
		} else finish(key, 0, u"The saved reply changed. Review it again before sending."_s, done);
	});
}

void ReplyStore::discard(int account, SavedReply reviewed, Done done) {
	const auto key = Key(account, reviewed.id);
	if (!begin(key, done)) return;
	read(key, [=, this](std::optional<SavedReply> saved, QString error) {
		if (!error.isEmpty()) { finish(key, 0, std::move(error), done); return; }
		if ((!saved && (!_unsaved.contains(key) || Same(_unsaved.at(key), reviewed)))
			|| (saved && Same(*saved, reviewed))) clear(account, reviewed, done);
		else finish(key, 0, u"The saved reply changed. Review it again before discarding."_s, done);
	});
}

void ReplyStore::load(int account, Listed done) {
	if (account <= 0) { done({}, StorageError()); return; }
	auto state = std::make_shared<Listing>();
	state->account = account;
	state->done = std::move(done);
	for (const auto &[key, reply] : _unsaved) {
		if (key.first == account) state->replies.emplace(key.second, reply);
	}
	call(u"get_all_ui_config_keys"_s, {account}, [=, this](QJsonObject listed) {
		if (listed.contains(u"error"_s) || !listed[u"result"_s].isArray()) {
			auto local = std::vector<SavedReply>();
			for (const auto &[id, reply] : state->replies) local.push_back(reply);
			state->done(std::move(local), StorageError());
			return;
		}
		auto seen = std::set<QString>();
		for (const auto &value : listed[u"result"_s].toArray()) {
			const auto key = value.toString();
			if (!value.isString() || (key.startsWith(Prefix())
				&& (!ValidId(key.mid(Prefix().size())) || !seen.insert(key).second))) {
				state->error = StorageError();
				continue;
			}
			if (key.startsWith(Prefix())) state->ids.push_back(key.mid(Prefix().size()));
		}
		listNext(state);
	});
}

void ReplyStore::listNext(std::shared_ptr<Listing> state) {
	if (state->ids.empty()) {
		auto result = std::vector<SavedReply>();
		for (const auto &[id, reply] : state->replies) result.push_back(reply);
		state->done(std::move(result), state->error);
		return;
	}
	const auto id = state->ids.back();
	state->ids.pop_back();
	read({state->account, id}, [=, this](std::optional<SavedReply> reply, QString error) {
		if (!error.isEmpty()) state->error = std::move(error);
		if (reply) {
			state->replies[id] = *reply;
			const auto key = Key(state->account, id);
			if (_unsaved.contains(key) && Same(_unsaved.at(key), *reply)) _unsaved.erase(key);
		}
		listNext(state);
	});
}

}
