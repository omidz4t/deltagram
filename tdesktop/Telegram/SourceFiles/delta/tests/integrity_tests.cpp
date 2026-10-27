#include "delta_rpc.h"
#include "backup_transfer.h"
#include "draft_store.h"
#include "compose_queue.h"
#include "compose_batch.h"
#include "compose_queue_sender.h"
#include "account_scope.h"
#include "account_inventory.h"
#include "message_order.h"
#include "message_visibility.h"
#include "message_download.h"
#include "message_capabilities.h"
#include "history_mutations.h"
#include "media_identity.h"
#include "prepared_attachment.h"
#include "profile_activity.h"
#include "chat_mute.h"
#include "contact_blocking.h"
#include "send_permission.h"
#include "reply_store.h"

#include <QtTest/QtTest>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

QJsonObject CoreCall(DeltaRpc &rpc, const QString &method, const QJsonArray &params) {
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	auto reply = QJsonObject();
	auto completed = false;
	rpc.call(method, params, [&](QJsonObject result) {
		reply = std::move(result);
		completed = true;
		loop.quit();
	});
	if (!completed) {
		timeout.start(10000);
		loop.exec();
	}
	if (!completed) rpc.stop();
	return reply;
}

int BackupCalls(DeltaRpc &rpc, const QString &method, int account = 0) {
	const auto calls = CoreCall(rpc, u"backup_calls"_s, {})[u"result"_s].toArray();
	auto count = 0;
	for (const auto &value : calls) {
		const auto call = value.toObject();
		if (call[u"method"_s] == method
			&& (!account || call[u"params"_s].toArray()[0].toInt() == account)) {
			++count;
		}
	}
	return count;
}

QJsonObject ForwardFixtureSource(int id = 41, int chat = 11) {
	return {
		{u"id"_s, id}, {u"chatId"_s, chat}, {u"state"_s, 16},
		{u"text"_s, u"Forward source %1"_s.arg(id)},
		{u"viewType"_s, u"Text"_s}, {u"fileName"_s, QString()},
	};
}

struct ActivityFixture {
	struct Pending {
		QString method;
		QJsonArray params;
		DeltaRpc::Callback done;
	};
	std::vector<Pending> pending;
	QStringList methods;
	int changes = 0;
	QStringList errors;
	std::unique_ptr<Delta::ProfileActivity> activity;

	ActivityFixture() {
		activity = std::make_unique<Delta::ProfileActivity>(
			[=, this](QString method, QJsonArray params, DeltaRpc::Callback done) {
				methods.push_back(method);
				pending.push_back({std::move(method), std::move(params), std::move(done)});
			}, [=, this] { ++changes; }, [=, this](QString error) { errors.push_back(error); });
	}
	bool reply(QString method, QJsonValue value, int account = 0, bool error = false) {
		const auto i = std::find_if(pending.begin(), pending.end(), [&](const Pending &request) {
			return request.method == method && (!account || request.params[0].toInt() == account);
		});
		if (i == pending.end()) return false;
		auto done = std::move(i->done);
		pending.erase(i);
		done({{error ? u"error"_s : u"result"_s, value}});
		return true;
	}
	bool inventory(QJsonArray ids) {
		if (!reply(u"get_all_account_ids"_s, ids)) return false;
		for (const auto &id : ids) {
			if (!reply(u"is_configured"_s, true, id.toInt())) return false;
		}
		return true;
	}
	void event(int account, QString kind) {
		activity->handleEvent({{u"contextId"_s, account}, {u"event"_s, QJsonObject{{u"kind"_s, kind}}}});
	}
};

struct ReplyFixture {
	using Key = std::pair<int, QString>;
	std::map<Key, QJsonValue> config;
	std::map<int, QJsonObject> messages;
	QStringList methods;
	QString fault;
	int sends = 0;
	int writes = 0;
	DeltaRpc::Callback held;
	std::unique_ptr<Delta::ReplyStore> store;

	ReplyFixture() { restart(); }
	void restart() {
		store = std::make_unique<Delta::ReplyStore>([=, this](QString method, QJsonArray args, DeltaRpc::Callback done) {
			methods.push_back(method);
			const auto reject = [&] { done({{u"error"_s, u"fixture fault"_s}}); };
			const auto result = [&](QJsonValue value) { done({{u"result"_s, value}}); };
			if (method == u"set_config"_s) {
				++writes;
				const auto state = QJsonDocument::fromJson(args[2].toString().toUtf8()).object()[u"state"_s].toInt();
				if (fault == u"reject-save"_s || (fault == u"reject-attempt"_s && state == 1)
					|| (fault == u"reject-clear"_s && args[2].isNull())) { reject(); return; }
				const auto key = Key(args[0].toInt(), args[1].toString());
				if (args[2].isNull()) config.erase(key);
				else config[key] = args[2];
				if (fault == u"hold-save"_s) { held = std::move(done); return; }
				if (fault == u"lost-save-ack"_s) { reject(); return; }
				result(QJsonValue(QJsonValue::Null));
			} else if (method == u"get_config"_s) {
				if (fault == u"reject-read"_s) { reject(); return; }
				const auto key = Key(args[0].toInt(), args[1].toString());
				result(config.contains(key) ? config.at(key) : QJsonValue(QJsonValue::Null));
			} else if (method == u"get_all_ui_config_keys"_s) {
				auto keys = QJsonArray();
				for (const auto &[key, value] : config) if (key.first == args[0].toInt()) keys.append(key.second);
				result(keys);
			} else if (method == u"send_msg"_s) {
				++sends;
				if (fault == u"hold-send"_s) { held = std::move(done); return; }
				if (fault == u"reject-send"_s) { reject(); return; }
				const auto id = 100 + sends;
				messages[id] = {{u"id"_s, id}, {u"chatId"_s, args[1]}, {u"text"_s, args[2].toObject()[u"text"_s]}, {u"state"_s, 20}};
				if (fault == u"lost-send-ack"_s) reject();
				else if (fault == u"malformed-send-ack"_s) result(u"101"_s);
				else result(id);
			} else if (method == u"get_message"_s) {
				if (fault == u"reject-message"_s) reject();
				else result(messages.at(args[1].toInt()));
			} else QFAIL("Unexpected RPC: notification replies must not touch drafts.");
		}, nullptr);
	}
	std::vector<Delta::SavedReply> list(int account = 1) {
		auto result = std::vector<Delta::SavedReply>();
		store->load(account, [&](auto replies, QString) { result = std::move(replies); });
		return result;
	}
};

struct PermissionFixture {
	struct Pending {
		QString method;
		QJsonArray params;
		DeltaRpc::Callback done;
	};
	std::vector<Pending> pending;
	QStringList methods;
	int changes = 0;
	std::unique_ptr<Delta::ChatSendPermissions> permissions;

	PermissionFixture() {
		permissions = std::make_unique<Delta::ChatSendPermissions>(
			[=, this](QString method, QJsonArray params, DeltaRpc::Callback done) {
				methods.push_back(method);
				pending.push_back({ std::move(method), std::move(params), std::move(done) });
			},
			[=, this] { ++changes; });
	}

	bool reply(QJsonValue value, bool error = false) {
		if (pending.empty()) return false;
		auto done = std::move(pending.front().done);
		pending.erase(pending.begin());
		done({{ error ? u"error"_s : u"result"_s, value }});
		return true;
	}

	bool replyEnvelope(QJsonObject envelope) {
		if (pending.empty()) return false;
		auto done = std::move(pending.front().done);
		pending.erase(pending.begin());
		done(std::move(envelope));
		return true;
	}
};

struct AuthorizedReplyFixture {
	ReplyFixture replies;
	PermissionFixture permissions;
	Delta::AccountScope scope;

	void submit(int account, int chat, QString text, Delta::ReplyStore::Done done) {
		Delta::SubmitAuthorizedReply(
			*replies.store,
			*permissions.permissions,
			scope,
			account,
			chat,
			std::move(text),
			std::move(done));
	}
};

QJsonObject FullChatJson(int id, QJsonValue canSend, bool device = false) {
	auto json = QJsonObject{
		{ u"id"_s, id },
		{ u"chatType"_s, u"Single"_s },
		{ u"isDeviceChat"_s, device },
	};
	if (!canSend.isUndefined()) json.insert(u"canSend"_s, canSend);
	return json;
}

QJsonObject RequestFullChatJson(
		int id,
		bool canSend,
		bool request,
		const QString &type = u"Single"_s,
		QJsonArray contacts = QJsonArray{ 12 }) {
	return {
		{ u"id"_s, id },
		{ u"chatType"_s, type },
		{ u"canSend"_s, canSend },
		{ u"isContactRequest"_s, request },
		{ u"isDeviceChat"_s, false },
		{ u"isSelfTalk"_s, false },
		{ u"contactIds"_s, std::move(contacts) },
	};
}

} // namespace

class IntegrityTests final : public QObject {
	Q_OBJECT

private slots:
	void blockedContactListValidation_data() {
		QTest::addColumn<QString>("fault");
		for (const auto fault : {"none", "empty", "error", "not-array", "duplicate", "reserved", "fraction", "unblocked", "missing-state", "missing-name", "bad-avatar"}) {
			QTest::newRow(fault) << QString::fromLatin1(fault);
		}
	}

	void blockedContactListValidation() {
		QFETCH(QString, fault);
		auto scope = Delta::AccountScope();
		auto contact = QJsonObject{{u"id"_s, 12}, {u"isBlocked"_s, true},
			{u"displayName"_s, u"Disposable contact"_s}, {u"address"_s, u"test@example.test"_s},
			{u"profileImage"_s, QJsonValue()}};
		if (fault == u"reserved"_s) contact[u"id"_s] = 1;
		if (fault == u"fraction"_s) contact[u"id"_s] = 12.5;
		if (fault == u"unblocked"_s) contact[u"isBlocked"_s] = false;
		if (fault == u"missing-state"_s) contact.remove(u"isBlocked"_s);
		if (fault == u"missing-name"_s) contact.remove(u"displayName"_s);
		if (fault == u"bad-avatar"_s) contact[u"profileImage"_s] = 42;
		auto list = QJsonArray{contact};
		if (fault == u"duplicate"_s) list.append(contact);
		if (fault == u"empty"_s) list = {};
		auto response = QJsonObject{{u"result"_s, list}};
		if (fault == u"error"_s) response[u"error"_s] = u"offline"_s;
		if (fault == u"not-array"_s) response[u"result"_s] = QJsonObject();
		auto completed = false;
		Delta::ReadCoreBlockedContacts([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(method, u"get_blocked_contacts"_s);
			QCOMPARE(params, QJsonArray{7});
			done(response);
		}, scope, 7, [&](std::vector<Delta::BlockedContact> rows, QString error) {
			const auto valid = fault == u"none"_s || fault == u"empty"_s;
			QCOMPARE(error.isEmpty(), valid);
			QCOMPARE(rows.size(), fault == u"none"_s ? size_t(1) : size_t(0));
			completed = true;
		});
		QVERIFY(completed);
	}

	void contactBlockingReadback_data() {
		QTest::addColumn<QString>("fault");
		QTest::addColumn<bool>("blocking");
		for (const auto blocking : {false, true}) {
			for (const auto fault : {"none", "no-chat", "lost-ack", "rejected", "bad-readback", "already-target", "bad-before", "bad-chat"}) {
				QTest::newRow(qPrintable(QString::number(blocking) + '-' + QString::fromLatin1(fault)))
					<< QString::fromLatin1(fault) << blocking;
			}
		}
	}

	void contactBlockingReadback() {
		QFETCH(QString, fault);
		QFETCH(bool, blocking);
		auto scope = Delta::AccountScope();
		auto state = fault == u"already-target"_s ? blocking : !blocking;
		auto writes = 0;
		auto reads = 0;
		auto completed = false;
		Delta::ChangeCoreContactBlocked([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(params, (QJsonArray{7, 12}));
			if (method == u"get_contact"_s) {
				++reads;
				if ((reads == 1 && fault == u"bad-before"_s) || (reads == 2 && fault == u"bad-readback"_s)) {
					done({{u"result"_s, QJsonObject{{u"id"_s, 12}}}});
				} else done({{u"result"_s, QJsonObject{{u"id"_s, 12}, {u"isBlocked"_s, state}}}});
			} else if (method == u"get_chat_id_by_contact_id"_s) {
				done({{u"result"_s, fault == u"bad-chat"_s ? QJsonValue(u"10"_s) : fault == u"no-chat"_s ? QJsonValue() : QJsonValue(10)}});
			} else {
				QCOMPARE(method, blocking ? u"block_contact"_s : u"unblock_contact"_s);
				++writes;
				if (fault != u"rejected"_s) state = blocking;
				done(fault == u"lost-ack"_s || fault == u"rejected"_s
					? QJsonObject{{u"error"_s, u"interrupted"_s}} : QJsonObject{{u"result"_s, QJsonValue()}});
			}
		}, scope, 7, 12, blocking, [&](int chat, QString error) {
			QCOMPARE(error.isEmpty(), fault == u"none"_s || fault == u"no-chat"_s || fault == u"lost-ack"_s);
			if (error.isEmpty()) QCOMPARE(chat, fault == u"no-chat"_s ? 0 : 10);
			completed = true;
		});
		QVERIFY(completed);
		const auto beforeWrite = fault == u"already-target"_s || fault == u"bad-before"_s || fault == u"bad-chat"_s;
		QCOMPARE(writes, beforeWrite ? 0 : 1);
		QCOMPARE(reads, beforeWrite ? 1 : 2);
	}

	void contactBlockingCancelsOnProfileChange_data() {
		QTest::addColumn<int>("stage");
		for (auto i = 1; i <= 4; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
	}

	void contactBlockingCancelsOnProfileChange() {
		QFETCH(int, stage);
		auto scope = Delta::AccountScope();
		auto calls = 0;
		DeltaRpc::Callback held;
		auto response = QJsonObject();
		Delta::ChangeCoreContactBlocked([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(params, (QJsonArray{7, 12}));
			++calls;
			response = method == u"get_contact"_s
				? QJsonObject{{u"result"_s, QJsonObject{{u"id"_s, 12}, {u"isBlocked"_s, calls > 1}}}}
				: QJsonObject{{u"result"_s, method == u"get_chat_id_by_contact_id"_s ? QJsonValue(10) : QJsonValue()}};
			if (calls == stage) held = std::move(done);
			else done(response);
		}, scope, 7, 12, true, [](int, QString) { QFAIL("Blocking callback escaped its profile"); });
		QVERIFY(bool(held));
		scope.invalidate();
		held(response);
		QCOMPARE(calls, stage);
	}

	void blockedContactListRejectsInvalidAndStaleRequests() {
		auto scope = Delta::AccountScope();
		auto calls = 0;
		DeltaRpc::Callback held;
		const auto request = [&](QString, QJsonArray, DeltaRpc::Callback done) { ++calls; held = std::move(done); };
		auto failures = 0;
		Delta::ReadCoreBlockedContacts(request, scope, 0, [&](std::vector<Delta::BlockedContact>, QString error) {
			QVERIFY(!error.isEmpty()); ++failures;
		});
		for (const auto contact : {0, 1, 9}) {
			Delta::ChangeCoreContactBlocked(request, scope, 7, contact, true, [&](int, QString error) {
				QVERIFY(!error.isEmpty()); ++failures;
			});
		}
		QCOMPARE(calls, 0);
		QCOMPARE(failures, 4);
		Delta::ReadCoreBlockedContacts(request, scope, 7, [](std::vector<Delta::BlockedContact>, QString) {
			QFAIL("Blocked list escaped its profile");
		});
		scope.invalidate();
		held({{u"result"_s, QJsonArray()}});
		QCOMPARE(calls, 1);
	}

	void realCoreBlockingPreservesDraftsAcrossRestart() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		auto directory = QTemporaryDir();
		QVERIFY(directory.isValid());
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, directory.path());
		auto accounts = std::vector<int>();
		auto contacts = std::vector<int>();
		auto chats = std::vector<int>();
		for (auto i = 0; i != 2; ++i) {
			const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
			const auto contact = CoreCall(*rpc, u"create_contact"_s,
				{account, u"blocking-fixture@example.test"_s, u"Disposable contact"_s})[u"result"_s].toInt();
			const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, contact})[u"result"_s].toInt();
			QVERIFY(account > 0 && contact > 9 && chat > 0);
			accounts.push_back(account); contacts.push_back(contact); chats.push_back(chat);
			QVERIFY(!CoreCall(*rpc, u"misc_set_draft"_s,
				{account, chat, u"Keep profile draft %1"_s.arg(i), QJsonValue(), QJsonValue(), QJsonValue(), QJsonValue()}).contains(u"error"_s));
		}
		QCOMPARE(contacts[0], contacts[1]);
		auto scope = Delta::AccountScope();
		const auto request = [=](QString method, QJsonArray params, DeltaRpc::Callback done) {
			rpc->call(method, params, std::move(done));
		};
		const auto change = [&](bool blocked) {
			auto completed = false;
			Delta::ChangeCoreContactBlocked(request, scope, accounts[0], contacts[0], blocked, [&](int, QString error) {
				QVERIFY2(error.isEmpty(), qPrintable(error)); completed = true;
			});
			QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
		};
		change(true);
		rpc->stop();
		rpc->start(program, directory.path());
		for (auto i = 0; i != 2; ++i) {
			QCOMPARE(Delta::CoreContactBlocked(CoreCall(*rpc, u"get_contact"_s, {accounts[i], contacts[i]}), contacts[i]),
				std::optional<bool>(i == 0));
			const auto draft = CoreCall(*rpc, u"get_draft"_s, {accounts[i], chats[i]})[u"result"_s].toObject();
			QCOMPARE(draft[u"text"_s].toString(), u"Keep profile draft %1"_s.arg(i));
			auto completed = false;
			Delta::ReadCoreBlockedContacts(request, scope, accounts[i], [&](std::vector<Delta::BlockedContact> rows, QString error) {
				QVERIFY2(error.isEmpty(), qPrintable(error));
				QCOMPARE(rows.size(), i == 0 ? size_t(1) : size_t(0)); completed = true;
			});
			QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
			const auto list = CoreCall(*rpc, u"get_chatlist_entries"_s,
				{accounts[i], QJsonValue(), QJsonValue(), QJsonValue()})[u"result"_s].toArray();
			QCOMPARE(list.contains(chats[i]), i == 1);
		}
		change(false);
		QCOMPARE(CoreCall(*rpc, u"get_chat_id_by_contact_id"_s, {accounts[0], contacts[0]})[u"result"_s].toInt(), chats[0]);
		QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {accounts[0], chats[0]})[u"result"_s].toObject()[u"text"_s].toString(), u"Keep profile draft 0"_s);
		rpc->stop();
	}

	void notificationReplySendsWithoutTouchingComposer() {
		auto fixture = ReplyFixture();
		auto completed = false;
		fixture.store->submit(1, 12, u"reply text"_s, [&](int id, QString error) {
			QVERIFY(id > 0); QVERIFY(error.isEmpty()); completed = true;
		});
		QVERIFY(completed);
		QCOMPARE(fixture.sends, 1);
		QVERIFY(fixture.config.empty());
		QVERIFY(!fixture.store->hasUnsaved());
	}

	void notificationReplyPersistWithoutSendLeavesPending() {
		auto fixture = ReplyFixture();
		std::optional<Delta::SavedReply> saved;
		fixture.store->persist(1, 12, u"saved without send"_s, [&](auto reply, QString error) {
			saved = std::move(reply);
			QVERIFY(error.isEmpty());
		});
		QVERIFY(saved.has_value());
		QCOMPARE(fixture.sends, 0);
		QCOMPARE(saved->text, u"saved without send"_s);
		QCOMPARE(saved->state, Delta::ReplyState::Pending);
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].text, u"saved without send"_s);
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyDelayedAllowSendsOnce() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		auto message = 0;
		auto error = QString();
		fixture.submit(1, 12, u"inline reply"_s, [&](int id, QString err) {
			message = id;
			error = err;
			++completed;
		});
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(fixture.permissions.pending.size(), size_t(1));
		QCOMPARE(fixture.permissions.methods, QStringList{ u"get_full_chat_by_id"_s });
		const auto pending = fixture.replies.list();
		QCOMPARE(pending.size(), size_t(1));
		QCOMPARE(pending[0].state, Delta::ReplyState::Pending);
		QVERIFY(pending[0].durable);
		QCOMPARE(pending[0].text, u"inline reply"_s);
		QVERIFY(fixture.permissions.reply(FullChatJson(12, true)));
		QCOMPARE(fixture.replies.sends, 1);
		QCOMPARE(completed, 1);
		QVERIFY(message > 0);
		QVERIFY(error.isEmpty());
		QVERIFY(fixture.replies.config.empty());
		QVERIFY(!fixture.permissions.reply(FullChatJson(12, true)));
	}

	void notificationReplyDeniedKeepsPendingWithoutSend() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		fixture.submit(1, 12, u"blocked reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error, Delta::kCoreSendDeniedKeepReply);
			++completed;
		});
		QVERIFY(fixture.permissions.reply(FullChatJson(12, false, true)));
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(completed, 1);
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].text, u"blocked reply"_s);
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyContactRequestKeepsPendingWithoutSend() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		fixture.submit(1, 12, u"request reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error, Delta::kCoreSendDeniedKeepReply);
			++completed;
		});
		QVERIFY(fixture.permissions.reply(RequestFullChatJson(12, true, true)));
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(completed, 1);
		QVERIFY(fixture.permissions.permissions->allowed(1, 12));
		QVERIFY(!fixture.permissions.permissions->sendable(1, 12));
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].text, u"request reply"_s);
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyPermissionErrorKeepsPendingWithoutSend() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		fixture.submit(1, 12, u"offline reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error, Delta::kCoreSendUnknownKeepReply);
			++completed;
		});
		QVERIFY(fixture.permissions.reply(u"offline"_s, true));
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(completed, 1);
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyMalformedEnvelopeKeepsPendingWithoutSend() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		fixture.submit(1, 12, u"wrapped reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error, Delta::kCoreSendUnknownKeepReply);
			++completed;
		});
		QVERIFY(fixture.permissions.replyEnvelope({
			{ u"id"_s, 12 },
			{ u"canSend"_s, true },
			{ u"result"_s, QJsonArray{} },
		}));
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(completed, 1);
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyStaleAccountKeepsPendingWithoutSend() {
		auto fixture = AuthorizedReplyFixture();
		auto completed = 0;
		fixture.submit(1, 12, u"stale reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error, Delta::kCoreSendReplyAccountChanged);
			++completed;
		});
		fixture.scope.invalidate();
		QVERIFY(fixture.permissions.reply(FullChatJson(12, true)));
		QCOMPARE(fixture.replies.sends, 0);
		QCOMPARE(completed, 1);
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Pending);
		QVERIFY(replies[0].durable);
	}

	void notificationReplyDoesNotRetryAfterAmbiguousSend() {
		auto fixture = AuthorizedReplyFixture();
		fixture.replies.fault = u"reject-send"_s;
		auto completed = 0;
		fixture.submit(1, 12, u"ambiguous reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
			++completed;
		});
		QVERIFY(fixture.permissions.reply(FullChatJson(12, true)));
		QCOMPARE(fixture.replies.sends, 1);
		QCOMPARE(completed, 1);
		fixture.replies.fault.clear();
		const auto replies = fixture.replies.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Attempted);
		fixture.replies.store->send(1, replies[0], [](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
		});
		QCOMPARE(fixture.replies.sends, 1);
	}

	void notificationReplyUncertainSendNeverRetries_data() {
		QTest::addColumn<QString>("fault");
		for (const auto &fault : {"reject-send", "lost-send-ack", "malformed-send-ack", "reject-message"}) {
			QTest::newRow(fault) << QString::fromLatin1(fault);
		}
	}

	void notificationReplyUncertainSendNeverRetries() {
		QFETCH(QString, fault);
		auto fixture = ReplyFixture();
		fixture.fault = fault;
		fixture.store->submit(1, 12, u"reply text"_s, [](int id, QString error) {
			QCOMPARE(id, 0); QVERIFY(!error.isEmpty());
		});
		QCOMPARE(fixture.sends, 1);
		fixture.restart();
		fixture.fault.clear();
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].text, u"reply text"_s);
		QCOMPARE(replies[0].state, Delta::ReplyState::Attempted);
		QVERIFY(replies[0].durable);
		fixture.store->send(1, replies[0], [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		QCOMPARE(fixture.sends, 1);
		fixture.store->discard(1, replies[0], [](int, QString error) { QVERIFY(error.isEmpty()); });
		QVERIFY(fixture.config.empty());
		QCOMPARE(fixture.messages.size(), fault == u"reject-send"_s ? size_t(0) : size_t(1));
	}

	void notificationReplyStorageFailureKeepsText_data() {
		QTest::addColumn<QString>("fault");
		QTest::addColumn<bool>("unsaved");
		QTest::newRow("rejected") << u"reject-save"_s << true;
		QTest::newRow("lost-ack") << u"lost-save-ack"_s << false;
		QTest::newRow("unreadable") << u"reject-read"_s << true;
		QTest::newRow("attempt-write-rejected") << u"reject-attempt"_s << false;
	}

	void notificationReplyStorageFailureKeepsText() {
		QFETCH(QString, fault);
		QFETCH(bool, unsaved);
		auto fixture = ReplyFixture();
		fixture.fault = fault;
		fixture.store->submit(1, 12, u"reply text"_s, [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		QCOMPARE(fixture.sends, 0);
		QCOMPARE(fixture.store->hasUnsaved(), unsaved);
		QCOMPARE(fixture.store->hasUnsaved(1), unsaved);
		QVERIFY(!fixture.store->hasUnsaved(2));
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].text, u"reply text"_s);
		fixture.fault.clear();
		fixture.store->send(1, replies[0], [](int id, QString error) { QVERIFY(id > 0); QVERIFY(error.isEmpty()); });
		QCOMPARE(fixture.sends, 1);
		QVERIFY(!fixture.store->hasUnsaved());
	}

	void notificationReplyRestartWhileSendPending() {
		auto fixture = ReplyFixture();
		fixture.fault = u"hold-send"_s;
		auto callbacks = 0;
		fixture.store->submit(1, 12, u"interrupted reply"_s, [&](int, QString) { ++callbacks; });
		QVERIFY(!fixture.store->hasUnsaved());
		fixture.restart();
		fixture.held({{u"result"_s, 101}});
		QCOMPARE(callbacks, 0);
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Attempted);
		QCOMPARE(replies[0].text, u"interrupted reply"_s);
		QCOMPARE(fixture.sends, 1);
	}

	void notificationReplyPendingWriteBlocksQuitAndConcurrentSend() {
		auto fixture = ReplyFixture();
		fixture.fault = u"hold-save"_s;
		fixture.store->submit(1, 12, u"reply text"_s, nullptr);
		QVERIFY(fixture.store->hasUnsaved());
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		fixture.store->send(1, replies[0], [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		fixture.store->discard(1, replies[0], [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		QCOMPARE(fixture.sends, 0);
		fixture.fault.clear();
		fixture.held({{u"result"_s, QJsonValue(QJsonValue::Null)}});
		QCOMPARE(fixture.sends, 1);
	}

	void notificationReplyAccountsAndStaleReviewStayIsolated() {
		auto fixture = ReplyFixture();
		fixture.fault = u"lost-save-ack"_s;
		fixture.store->submit(1, 12, u"first reply"_s, nullptr);
		fixture.store->submit(2, 12, u"other profile"_s, nullptr);
		const auto first = fixture.list(1);
		const auto second = fixture.list(2);
		QCOMPARE(first.size(), size_t(1)); QCOMPARE(second.size(), size_t(1));
		QCOMPARE(second[0].text, u"other profile"_s);
		fixture.fault.clear();
		fixture.store->send(2, first[0], [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		auto changed = first[0]; changed.text = u"stale edit"_s;
		fixture.store->send(1, changed, [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		fixture.store->discard(1, changed, [](int, QString error) { QVERIFY(!error.isEmpty()); });
		QCOMPARE(fixture.sends, 0);
		QCOMPARE(fixture.config.size(), size_t(2));
	}

	void notificationReplyMalformedEntryKeepsOtherRecoveryVisible() {
		auto fixture = ReplyFixture();
		fixture.fault = u"lost-save-ack"_s;
		fixture.store->submit(1, 12, u"valid reply"_s, nullptr);
		fixture.config[{1, u"ui.deltagram.notification-reply."_s + QString(32, u'a')}] = u"broken"_s;
		fixture.config[{1, u"ui.deltagram.notification-reply.invalid"_s}] = u"broken"_s;
		fixture.store->load(1, [](auto replies, QString error) {
			QVERIFY(!error.isEmpty()); QCOMPARE(replies.size(), size_t(1));
			QCOMPARE(replies[0].text, u"valid reply"_s);
		});
		QCOMPARE(fixture.sends, 0);
	}

	void notificationReplyConfirmedSendDoesNotRetryFailedCleanup() {
		auto fixture = ReplyFixture();
		fixture.fault = u"reject-clear"_s;
		fixture.store->submit(1, 12, u"sent reply"_s, [](int, QString error) { QVERIFY(!error.isEmpty()); });
		fixture.restart();
		const auto replies = fixture.list();
		QCOMPARE(replies.size(), size_t(1));
		QCOMPARE(replies[0].state, Delta::ReplyState::Sent);
		fixture.store->send(1, replies[0], [](int id, QString error) { QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); });
		QCOMPARE(fixture.sends, 1);
		fixture.fault.clear();
		fixture.store->discard(1, replies[0], [](int, QString error) { QVERIFY(error.isEmpty()); });
		QCOMPARE(fixture.messages.size(), size_t(1));
	}

	void realCoreNotificationReplySurvivesRestartWithComposer() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		auto directory = QTemporaryDir();
		QVERIFY(directory.isValid());
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, directory.path());
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		QVERIFY(!CoreCall(*rpc, u"misc_set_draft"_s,
			{account, chat, u"existing composer"_s, QJsonValue(), QJsonValue(), QJsonValue(), QJsonValue()}).contains(u"error"_s));
		auto sends = 0;
		const auto call = [&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			if (method == u"send_msg"_s) { ++sends; done({{u"error"_s, u"interrupted"_s}}); }
			else rpc->call(method, params, std::move(done));
		};
		auto store = std::make_unique<Delta::ReplyStore>(call, nullptr);
		auto completed = false;
		store->submit(account, chat, u"recover notification reply"_s, [&](int id, QString error) {
			QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(sends, 1);
		store.reset();
		rpc->stop();
		rpc->start(program, directory.path());
		store = std::make_unique<Delta::ReplyStore>(call, nullptr);
		completed = false;
		store->load(account, [&](auto replies, QString error) {
			QVERIFY(error.isEmpty()); QCOMPARE(replies.size(), size_t(1));
			QCOMPARE(replies[0].state, Delta::ReplyState::Attempted);
			QCOMPARE(replies[0].text, u"recover notification reply"_s);
			completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject()[u"text"_s].toString(), u"existing composer"_s);
		QCOMPARE(sends, 1);
		rpc->stop();
	}

	void chatMuteReadsRequireExplicitBoolean_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::addColumn<int>("expected");
		QTest::newRow("muted") << QJsonObject{{u"result"_s, true}} << 1;
		QTest::newRow("unmuted") << QJsonObject{{u"result"_s, false}} << 0;
		QTest::newRow("missing") << QJsonObject() << -1;
		QTest::newRow("null") << QJsonObject{{u"result"_s, QJsonValue(QJsonValue::Null)}} << -1;
		QTest::newRow("number") << QJsonObject{{u"result"_s, 0}} << -1;
		QTest::newRow("string") << QJsonObject{{u"result"_s, u"false"_s}} << -1;
		QTest::newRow("error-and-false") << QJsonObject{{u"error"_s, QJsonValue(QJsonValue::Null)}, {u"result"_s, false}} << -1;
	}

	void chatMuteReadsRequireExplicitBoolean() {
		QFETCH(QJsonObject, reply);
		QFETCH(int, expected);
		auto scope = Delta::AccountScope();
		auto completed = 0;
		Delta::ReadCoreChatMute([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(method, u"is_chat_muted"_s);
			QCOMPARE(params, (QJsonArray{2, 10}));
			done(reply);
		}, scope, 2, 10, [&](std::optional<bool> muted) {
			QCOMPARE(muted ? int(*muted) : -1, expected);
			++completed;
		});
		QCOMPARE(completed, 1);
	}

	void chatMuteReadsRejectInvalidAndStaleRequests() {
		auto scope = Delta::AccountScope();
		auto pending = DeltaRpc::Callback();
		auto calls = 0;
		auto completed = 0;
		const auto request = [&](QString, QJsonArray, DeltaRpc::Callback done) {
			++calls;
			pending = std::move(done);
		};
		const auto done = [&](std::optional<bool> muted) { QVERIFY(!muted); ++completed; };
		Delta::ReadCoreChatMute(request, scope, 0, 10, done);
		Delta::ReadCoreChatMute(request, scope, 2, 0, done);
		QCOMPARE(calls, 0);
		QCOMPARE(completed, 2);
		Delta::ReadCoreChatMute(request, scope, 2, 10, done);
		QCOMPARE(calls, 1);
		scope.invalidate();
		pending({{u"result"_s, false}});
		QCOMPARE(completed, 2);
	}

	void realCoreMuteReadAfterExpiry() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		auto directory = QTemporaryDir();
		QVERIFY(directory.isValid());
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, directory.path());
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		const auto set = CoreCall(*rpc, u"set_chat_mute_duration"_s,
			{account, chat, QJsonObject{{u"kind"_s, u"Until"_s}, {u"duration"_s, 2}}});
		QVERIFY(!set.contains(u"error"_s));
		auto scope = Delta::AccountScope();
		const auto read = [&](bool expected) {
			auto completed = false;
			Delta::ReadCoreChatMute([=](QString method, QJsonArray params, DeltaRpc::Callback done) {
				rpc->call(method, params, std::move(done));
			}, scope, account, chat, [&](std::optional<bool> muted) {
				completed = true;
				QVERIFY(muted.has_value());
				QCOMPARE(*muted, expected);
			});
			QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
		};
		read(true);
		QTest::qWait(2100);
		read(false);
		rpc->stop();
	}

	void chatMuteRejectsInvalidAndStaleRequests() {
		auto scope = Delta::AccountScope();
		auto calls = 0;
		auto completed = 0;
		auto pending = DeltaRpc::Callback();
		const auto request = [&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(method, u"set_chat_mute_duration"_s);
			QCOMPARE(params, (QJsonArray{2, 10, QJsonObject{{u"kind"_s, u"Forever"_s}}}));
			++calls;
			pending = std::move(done);
		};
		const auto failure = [&](QString error) { QVERIFY(!error.isEmpty()); ++completed; };
		Delta::SetCoreChatMute(request, scope, 0, 10, -1, failure);
		Delta::SetCoreChatMute(request, scope, 2, 0, -1, failure);
		Delta::SetCoreChatMute(request, scope, 2, 10, -2, failure);
		QCOMPARE(calls, 0);
		QCOMPARE(completed, 3);
		Delta::SetCoreChatMute(request, scope, 2, 10, -1, failure);
		QCOMPARE(calls, 1);
		scope.invalidate();
		pending({{u"result"_s, QJsonValue(QJsonValue::Null)}});
		QCOMPARE(completed, 3);
	}

	void chatMuteRejectsMalformedAcknowledgements_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("missing") << QJsonObject();
		QTest::newRow("false") << QJsonObject{{u"result"_s, false}};
		QTest::newRow("object") << QJsonObject{{u"result"_s, QJsonObject()}};
		QTest::newRow("error") << QJsonObject{{u"error"_s, u"rejected"_s}};
		QTest::newRow("error-and-null") << QJsonObject{{u"error"_s, QJsonValue(QJsonValue::Null)}, {u"result"_s, QJsonValue(QJsonValue::Null)}};
	}

	void chatMuteRejectsMalformedAcknowledgements() {
		QFETCH(QJsonObject, reply);
		auto scope = Delta::AccountScope();
		auto calls = 0;
		auto completed = false;
		Delta::SetCoreChatMute([&](QString, QJsonArray, DeltaRpc::Callback done) {
			++calls;
			done(reply);
		}, scope, 2, 10, 0, [&](QString error) { QVERIFY(!error.isEmpty()); completed = true; });
		QVERIFY(completed);
		QCOMPARE(calls, 1);
	}

	void realCoreMuteIsolationAcrossRestart() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		auto directory = QTemporaryDir();
		QVERIFY(directory.isValid());
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, directory.path());
		const auto first = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto second = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		QVERIFY(first > 0 && second > 0 && first != second);
		const auto chatA = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {first, 1})[u"result"_s].toInt();
		const auto chatB = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {second, 1})[u"result"_s].toInt();
		QVERIFY(chatA > 0 && chatB > 0);
		QCOMPARE(chatA, chatB);
		auto scope = Delta::AccountScope();
		const auto set = [&](int account, int chat, int seconds) {
			auto completed = false;
			auto error = QString();
			Delta::SetCoreChatMute([=](QString method, QJsonArray params, DeltaRpc::Callback done) {
				rpc->call(method, params, std::move(done));
			}, scope, account, chat, seconds, [&](QString failure) { error = failure; completed = true; });
			QTRY_VERIFY_WITH_TIMEOUT(completed, 5000);
			QVERIFY2(error.isEmpty(), qPrintable(error));
		};
		set(first, chatA, -1);
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {first, chatA})[u"result"_s], QJsonValue(true));
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {second, chatB})[u"result"_s], QJsonValue(false));
		set(second, chatB, 3600);
		rpc->stop();
		rpc->start(program, directory.path());
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {first, chatA})[u"result"_s], QJsonValue(true));
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {second, chatB})[u"result"_s], QJsonValue(true));
		set(first, chatA, 0);
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {first, chatA})[u"result"_s], QJsonValue(false));
		QCOMPARE(CoreCall(*rpc, u"is_chat_muted"_s, {second, chatB})[u"result"_s], QJsonValue(true));
		rpc->stop();
	}

	void profileActivityBackgroundStartup() {
		auto fixture = ActivityFixture();
		fixture.activity->start();
		QVERIFY(fixture.reply(u"start_io_for_all_accounts"_s, QJsonValue(QJsonValue::Null)));
		QVERIFY(fixture.reply(u"get_all_account_ids"_s, QJsonArray{1, 2, 3}));
		QVERIFY(fixture.reply(u"is_configured"_s, false, 3));
		QVERIFY(fixture.reply(u"is_configured"_s, true, 2));
		QVERIFY(fixture.reply(u"is_configured"_s, true, 1));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42}, 1));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41}, 2));
		QCOMPARE(fixture.activity->unread(1), 2);
		QCOMPARE(fixture.activity->unread(2), 1);
		QCOMPARE(fixture.activity->unread(3), 0);
		QCOMPARE(fixture.activity->totalUnread(), 3);
		QVERIFY(fixture.pending.empty());
		fixture.event(2, u"IncomingMsg"_s);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 44}, 2));
		QCOMPARE(fixture.activity->totalUnread(), 4);
		QCOMPARE(fixture.activity->unread(1), 2);
		QVERIFY(!fixture.methods.contains(u"select_account"_s));
		QVERIFY(!fixture.methods.contains(u"markseen_msgs"_s));
		QVERIFY(fixture.errors.empty());
	}

	void profileActivityStaleRead() {
		auto fixture = ActivityFixture();
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({1, 2}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41}, 1));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42}, 2));
		fixture.event(2, u"IncomingMsg"_s);
		fixture.event(2, u"MsgsNoticed"_s);
		const auto changes = fixture.changes;
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42, 43}, 2));
		QCOMPARE(fixture.changes, changes);
		QCOMPARE(fixture.activity->unread(2), 2);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{}, 2));
		QCOMPARE(fixture.activity->totalUnread(), 1);
		QVERIFY(fixture.pending.empty());
	}

	void profileActivityRemovedAndReusedAccount() {
		auto fixture = ActivityFixture();
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({1, 2}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41}, 1));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{}, 2));
		fixture.event(1, u"IncomingMsg"_s);
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({2}));
		QCOMPARE(fixture.activity->totalUnread(), 0);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{}, 2));
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({1, 2}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42, 43}, 1));
		QCOMPARE(fixture.activity->unread(1), 0);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{90}, 1));
		QCOMPARE(fixture.activity->unread(1), 1);
	}

	void profileActivityReadFailure_data() {
		QTest::addColumn<QJsonValue>("value");
		QTest::addColumn<bool>("error");
		QTest::newRow("error") << QJsonValue(u"rejected"_s) << true;
		QTest::newRow("null") << QJsonValue(QJsonValue::Null) << false;
		QTest::newRow("duplicate") << QJsonValue(QJsonArray{41, 41}) << false;
		QTest::newRow("string") << QJsonValue(QJsonArray{u"41"_s}) << false;
		QTest::newRow("fraction") << QJsonValue(QJsonArray{1.5}) << false;
		QTest::newRow("negative") << QJsonValue(QJsonArray{-1}) << false;
	}

	void profileActivityReadFailure() {
		QFETCH(QJsonValue, value);
		QFETCH(bool, error);
		auto fixture = ActivityFixture();
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({1}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42}, 1));
		fixture.event(1, u"MsgsNoticed"_s);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, value, 1, error));
		QCOMPARE(fixture.activity->unread(1), 2);
		QCOMPARE(fixture.errors.size(), 1);
		fixture.event(1, u"ChatModified"_s);
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{}, 1));
		QCOMPARE(fixture.activity->unread(1), 0);
	}

	void profileActivityInventoryAndLifetime() {
		auto fixture = ActivityFixture();
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({1}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41}, 1));
		fixture.activity->refreshProfiles();
		fixture.activity->refreshProfiles();
		QVERIFY(fixture.inventory({2}));
		QCOMPARE(fixture.activity->unread(1), 1);
		QVERIFY(fixture.reply(u"get_all_account_ids"_s, QJsonArray{1, 1}));
		QCOMPARE(fixture.activity->unread(1), 1);
		QCOMPARE(fixture.errors.size(), 1);
		fixture.event(1, u"IncomingMsg"_s);
		fixture.activity.reset();
		const auto changes = fixture.changes;
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41, 42}, 1));
		QCOMPARE(fixture.changes, changes);
	}

	void profileActivityStartFailureAndAccountsEvent() {
		auto fixture = ActivityFixture();
		fixture.activity->start();
		QVERIFY(fixture.reply(u"start_io_for_all_accounts"_s, false));
		QCOMPARE(fixture.errors.size(), 1);
		QVERIFY(fixture.inventory({}));
		fixture.event(0, u"AccountsChanged"_s);
		QVERIFY(fixture.reply(u"start_io_for_all_accounts"_s, QJsonValue(QJsonValue::Null)));
		QVERIFY(fixture.inventory({2}));
		QVERIFY(fixture.reply(u"get_fresh_msgs"_s, QJsonArray{41}, 2));
		QCOMPARE(fixture.activity->totalUnread(), 1);
		fixture.activity->handleEvent({{u"event"_s, QJsonObject{{u"kind"_s, u"IncomingMsg"_s}}}});
		QVERIFY(fixture.pending.empty());
	}

	void combinedBatchPublication_data() {
		QTest::addColumn<QString>("phase");
		for (const auto phase : { "fresh", "rejected", "uncertain", "changed-source", "changed-state", "invalid-source" }) {
			QTest::newRow(phase) << QString::fromLatin1(phase);
		}
	}

	void combinedBatchPublication() {
		QFETCH(QString, phase);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		CoreCall(*rpc, u"set_source_message"_s, {2, ForwardFixtureSource(), phase != u"invalid-source"_s});
		CoreCall(*rpc, u"queue_fault"_s, {phase == u"rejected"_s ? u"queue-reject-publish"_s
			: phase == u"fresh"_s || phase == u"invalid-source"_s ? u"queue-normal"_s : u"queue-unknown-publish"_s});
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 2, {});
		QVERIFY(batch->preparedForward(1, {11, 41}));
		QCOMPARE(batch->state(), Delta::ComposeBatch::State::Preparing);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(batch->preparedText(0, {u"Introduction"_s, 0, {}, {}, u"Text"_s}));
		QTRY_VERIFY(batch->state() != Delta::ComposeBatch::State::Saving);
		QCOMPARE(batch->state(), phase == u"fresh"_s ? Delta::ComposeBatch::State::Saved : Delta::ComposeBatch::State::Failed);
		if (phase != u"fresh"_s) {
			CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
			CoreCall(*rpc, u"set_source_message"_s, {2, ForwardFixtureSource(), true});
			if (phase.startsWith(u"changed-"_s)) {
				auto ledger = QJsonDocument::fromJson(CoreCall(*rpc, u"get_config"_s,
					{2, u"ui.deltagram.compose.queue.10"_s})[u"result"_s].toString().toUtf8()).object();
				auto entries = ledger[u"entries"_s].toArray();
				auto entry = entries[1].toObject();
				auto forward = entry[u"forward"_s].toObject();
				forward[phase == u"changed-source"_s ? u"message"_s : u"state"_s] = phase == u"changed-source"_s ? 42 : 1;
				entry[u"forward"_s] = forward;
				entries[1] = entry;
				ledger[u"entries"_s] = entries;
				CoreCall(*rpc, u"set_config"_s, {2, u"ui.deltagram.compose.queue.10"_s,
					QString::fromUtf8(QJsonDocument(ledger).toJson(QJsonDocument::Compact))});
			}
			QVERIFY(batch->retrySave());
			QTRY_VERIFY(batch->state() != Delta::ComposeBatch::State::Saving);
			QCOMPARE(batch->state(), phase.startsWith(u"changed-"_s)
				? Delta::ComposeBatch::State::Failed : Delta::ComposeBatch::State::Saved);
			QCOMPARE(batch->reconciled(), phase == u"uncertain"_s);
		}
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().isEmpty());
	}

	void forwardSourceIsolation_data() {
		QTest::addColumn<QString>("phase");
		for (const auto phase : { "wrong-account", "wrong-chat", "draft", "hidden" }) {
			QTest::newRow(phase) << QString::fromLatin1(phase);
		}
	}

	void forwardSourceIsolation() {
		QFETCH(QString, phase);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto source = ForwardFixtureSource();
		if (phase == u"draft"_s) source[u"state"_s] = 19;
		if (phase == u"wrong-chat"_s) source[u"chatId"_s] = 12;
		CoreCall(*rpc, u"set_source_message"_s, {phase == u"wrong-account"_s ? 3 : 2, source, phase != u"hidden"_s});
		Delta::ComposeQueueStore queues(rpc);
		auto completed = false;
		queues.stagePlan(2, 10, {{{}, Delta::ForwardSource{11, 41}}},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(!error.isEmpty()); QVERIFY(queue.entries.empty()); completed = true;
			});
		QTRY_VERIFY(completed);
		QVERIFY(CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject().isEmpty());
		QVERIFY(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().isEmpty());
	}

	void forwardInterruptedReply() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		CoreCall(*rpc, u"set_source_message"_s, {2, ForwardFixtureSource(), true});
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stagePlan(2, 10, {{{}, Delta::ForwardSource{11, 41}}},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
			});
		QTRY_VERIFY(completed);
		CoreCall(*rpc, u"forward_fault"_s, {u"hold"_s});
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		completed = false;
		sender->sendNext(2, 10, saved.id, saved.entries.front().id,
			[&](int, Delta::ComposeQueue, QString) { completed = true; });
		QTRY_COMPARE(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().size(), 1);
		QVERIFY(sender->busy(2, 10));
		sender.reset();
		CoreCall(*rpc, u"release"_s, {});
		QVERIFY(!completed);
		sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		sender->sendNext(2, 10, saved.id, saved.entries.front().id,
			[&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QVERIFY(!error.isEmpty());
				QCOMPARE(queue.entries.front().forwardState, Delta::ForwardState::Attempted);
				completed = true;
			});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().size(), 1);
	}

	void forwardQueueRecovery_data() {
		QTest::addColumn<QString>("phase");
		for (const auto phase : { "fresh", "rejected", "lost-ack", "confirmation-rejected",
			"attempted", "confirmed", "attempt-rejected", "attempt-unknown", "source-changed", "source-hidden" }) {
			QTest::newRow(phase) << QString::fromLatin1(phase);
		}
	}

	void forwardQueueRecovery() {
		QFETCH(QString, phase);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		const auto source = ForwardFixtureSource();
		CoreCall(*rpc, u"set_source_message"_s, {2, source, true});
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stagePlan(2, 10, {{ {}, Delta::ForwardSource{11, 41} }},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
			});
		QTRY_VERIFY(completed);
		QCOMPARE(saved.entries.size(), size_t(1));
		QVERIFY(saved.entries.front().forward.has_value());
		if (phase == u"attempted"_s || phase == u"confirmed"_s) {
			for (const auto state : { Delta::ForwardState::Attempted, Delta::ForwardState::Confirmed }) {
				completed = false;
				queues->markForward(2, 10, saved.id, saved.entries.front().id, state,
					[&](Delta::ComposeQueue queue, QString error) {
						QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
					});
				QTRY_VERIFY(completed);
				if (phase == u"attempted"_s) break;
			}
		}
		if (phase == u"attempt-rejected"_s || phase == u"attempt-unknown"_s) {
			CoreCall(*rpc, u"queue_fault"_s, {phase == u"attempt-rejected"_s
				? u"queue-reject-publish"_s : u"queue-unknown-publish"_s});
		}
		auto changed = source;
		if (phase == u"source-changed"_s) changed[u"text"_s] = u"Edited after selection"_s;
		CoreCall(*rpc, u"set_source_message"_s, {2, changed, phase != u"source-hidden"_s});
		CoreCall(*rpc, u"forward_fault"_s, {phase});
		const auto beforeDraft = CoreCall(*rpc, u"inspect_draft"_s, {2, 10}).value(u"result"_s);
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		completed = false;
		const auto success = phase == u"fresh"_s || phase == u"confirmed"_s;
		sender->sendNext(2, 10, saved.id, saved.entries.front().id,
			[&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QCOMPARE(error.isEmpty(), success);
				if (success) QVERIFY(queue.entries.empty());
				completed = true;
			});
		QTRY_VERIFY(completed);
		const auto called = phase == u"fresh"_s || phase == u"rejected"_s
			|| phase == u"lost-ack"_s || phase == u"confirmation-rejected"_s;
		QCOMPARE(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().size(), called ? 1 : 0);
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s], beforeDraft);
		sender.reset(); drafts.reset(); queues.reset();
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
		CoreCall(*rpc, u"forward_fault"_s, {u"normal"_s});
		CoreCall(*rpc, u"set_source_message"_s, {2, source, true});
		queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		completed = false;
		queues->load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		if (!success) {
			const auto uncertain = saved.entries.front().forwardState == Delta::ForwardState::Attempted;
			const auto id = saved.id;
			const auto entryId = saved.entries.front().id;
			completed = false;
			sender->sendNext(2, 10, id, entryId, [&](int, Delta::ComposeQueue, QString error) {
				QCOMPARE(error.isEmpty(), !uncertain); completed = true;
			});
			QTRY_VERIFY(completed);
			QCOMPARE(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().size(), int(called) + int(!uncertain));
			if (uncertain) {
				completed = false;
				sender->discardNext(2, 10, id, entryId, [&](int sent, Delta::ComposeQueue queue, QString error) {
					QCOMPARE(sent, 0); QVERIFY(error.isEmpty()); QVERIFY(queue.entries.empty()); completed = true;
				});
				QTRY_VERIFY(completed);
				QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s], beforeDraft);
			}
		}
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void combinedTextAndForwardQueue_data() {
		QTest::addColumn<QString>("type");
		for (const auto type : { "Text", "File", "Voice" }) QTest::newRow(type) << QString::fromLatin1(type);
	}

	void combinedTextAndForwardQueue() {
		QFETCH(QString, type);
		QTemporaryDir temporary;
		QFile file(temporary.filePath(type == u"Voice"_s ? u"recording.ogg"_s : u"attachment.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("prepared attachment fixture") > 0);
		file.close();
		const auto prefix = Delta::ComposeDraft{u"Introduction"_s, 0,
			type == u"Text"_s ? QString() : file.fileName(),
			type == u"Text"_s ? QString() : QFileInfo(file).fileName(), type};
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		CoreCall(*rpc, u"set_source_message"_s, {2, ForwardFixtureSource(), true});
		CoreCall(*rpc, u"set_source_message"_s, {2, ForwardFixtureSource(42, 12), true});
		CoreCall(*rpc, u"remove_draft"_s, {2, 10});
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		Delta::ComposeQueueSender sender(rpc, queues, drafts);
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stagePlan(2, 10, {
			{prefix, {}},
			{{}, Delta::ForwardSource{11, 41}}, {{}, Delta::ForwardSource{12, 42}},
		}, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		for (auto index = 0; index != 3; ++index) {
			completed = false;
			sender.sendNext(2, 10, saved.id, saved.entries.front().id,
				[&](int id, Delta::ComposeQueue queue, QString error) {
					QVERIFY(error.isEmpty()); QCOMPARE(id > 0, index == 0);
					saved = std::move(queue); completed = true;
				});
			QTRY_VERIFY(completed);
			QCOMPARE(saved.entries.size(), size_t(2 - index));
			QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 1);
			QCOMPARE(CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray().size(), index);
		}
		const auto calls = CoreCall(*rpc, u"forward_calls"_s, {})[u"result"_s].toArray();
		QCOMPARE(calls[0].toArray(), QJsonArray({2, QJsonArray{41}, 10}));
		QCOMPARE(calls[1].toArray(), QJsonArray({2, QJsonArray{42}, 10}));
	}

	void recordingBytesSurviveStorageFailure_data() {
		QTest::addColumn<QString>("fault");
		for (const auto fault : { "queue-reject-copy", "queue-reject-pin",
			"queue-reject-publish", "queue-unknown-publish" }) {
			QTest::newRow(fault) << QString::fromLatin1(fault);
		}
	}

	void recordingBytesSurviveStorageFailure() {
		QFETCH(QString, fault);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, fault);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		const auto expected = QByteArray("encoded recording bytes");
		auto recorded = expected;
		QVERIFY(batch->preparedBytes(0, {{}, 41, {}, u"recording.ogg"_s, u"Voice"_s}, recorded));
		recorded.clear();
		QVERIFY(!batch->preparedBytes(0, {{}, 0, {}, u"replacement.ogg"_s, u"Voice"_s}, "other"));
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(batch->canRetrySave());
		const auto path = batch->selection().front().file;
		QFile retained(path);
		QVERIFY(retained.open(QIODevice::ReadOnly));
		QCOMPARE(retained.readAll(), expected);
		retained.close();
		const auto publicAccess = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
			| QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
		QCOMPARE(int(QFileInfo(path).permissions()) & int(publicAccess), 0);
		QCOMPARE(int(QFileInfo(QFileInfo(path).absolutePath()).permissions()) & int(publicAccess), 0);
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
		QVERIFY(batch->retrySave());
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Saved);
		QCOMPARE(batch->reconciled(), fault == u"queue-unknown-publish"_s);
		QVERIFY(!QFileInfo::exists(path));
		QVERIFY(!QFileInfo::exists(QFileInfo(path).absolutePath()));
		QCOMPARE(batch->queue().entries.front().draft.viewType, u"Voice"_s);
		QCOMPARE(batch->queue().entries.front().draft.quotedMessageId, 41);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void recordingBytesRetryLocalWrite() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-reject-copy"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QTemporaryDir temporary;
		const auto directory = temporary.filePath(u"missing"_s);
		QVERIFY(batch->preparedBytes(0, {{}, 0, {}, u"recording.mp4"_s, u"Video"_s}, "encoded video", directory));
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(batch->selection().front().file.isEmpty());
		QVERIFY(batch->canRetrySave());
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(QDir().mkdir(directory));
		QVERIFY(batch->retrySave());
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		const auto path = batch->selection().front().file;
		QFile retained(path);
		QVERIFY(retained.open(QIODevice::ReadOnly));
		QCOMPARE(retained.readAll(), QByteArray("encoded video"));
		retained.close();
		QVERIFY(batch->discard());
		QVERIFY(!QFileInfo::exists(path));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void recordingBytesDiscardBeforeWriteCompletes() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		const auto weak = std::weak_ptr<Delta::ComposeBatch>(batch);
		QVERIFY(batch->preparedBytes(0, {{}, 0, {}, u"recording.ogg"_s, u"Voice"_s}, "encoded voice"));
		QVERIFY(batch->discard());
		batch.reset();
		QTRY_VERIFY(weak.expired());
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void realCoreRecordingBytesSurviveRestart() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		const auto accounts = temporary.filePath(u"accounts"_s);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, accounts);
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, account, chat, 1, {});
		const auto bytes = QByteArray("recording storage fixture, not a codec test");
		QVERIFY(batch->preparedBytes(0, {{}, 0, {}, u"recording.ogg"_s, u"Voice"_s}, bytes));
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Saved);
		auto saved = batch->queue();
		QVERIFY(!QFileInfo::exists(batch->selection().front().file));
		batch.reset();
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		auto completed = false;
		sender->sendNext(account, chat, saved.id, saved.entries.front().id,
			[&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QVERIFY(!error.isEmpty());
				QCOMPARE(queue.entries.size(), size_t(1));
				QVERIFY(queue.entries.front().messageId > 0);
				saved = std::move(queue); completed = true;
			});
		QTRY_VERIFY(completed);
		sender.reset(); drafts.reset(); queues.reset(); rpc->stop();
		rpc->start(program, accounts);
		queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		completed = false;
		queues->load(account, chat, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.id, saved.id);
			QCOMPARE(queue.entries.size(), size_t(1));
			QCOMPARE(queue.entries.front().messageId, saved.entries.front().messageId);
			QCOMPARE(queue.entries.front().draft.viewType, u"Voice"_s);
			QFile file(queue.entries.front().draft.file);
			QVERIFY(file.open(QIODevice::ReadOnly));
			QCOMPARE(file.readAll(), bytes);
			completed = true;
		});
		QTRY_VERIFY(completed);
		const auto draft = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
		QCOMPARE(draft[u"id"_s].toInt(), saved.entries.front().messageId);
		QCOMPARE(draft[u"state"_s].toInt(), 19);
		QCOMPARE(draft[u"viewType"_s].toString(), u"Voice"_s);
		QCOMPARE(draft[u"fileName"_s].toString(), u"recording.ogg"_s);
		QFile file(draft[u"file"_s].toString());
		QVERIFY(file.open(QIODevice::ReadOnly));
		QCOMPARE(file.readAll(), bytes);
	}

	void queueInspectionDoesNotBlockMutation() {
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.txt"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("queue inspection fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto saved = Delta::ComposeQueue();
		auto staged = false;
		queues->stage(2, 10, {{u"Caption"_s, 0, file.fileName(), u"source.txt"_s, u"File"_s}},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); saved = std::move(queue); staged = true;
			});
		QTRY_VERIFY(staged);
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-hold-inspection"_s});
		auto inspected = false;
		queues->inspect(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.id, saved.id);
			QCOMPARE(queue.entries.size(), size_t(1));
			inspected = true;
		});
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		QVERIFY(!inspected);
		QVERIFY(!queues->busy(2, 10));
		auto removed = false;
		queues->removeFirst(2, 10, saved.id, saved.entries.front().id,
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); QVERIFY(queue.entries.empty()); removed = true;
			});
		QTRY_VERIFY(removed);
		QVERIFY(!inspected);
		const auto writes = CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt();
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(inspected);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writes);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		Delta::ComposeQueueSender sender(rpc, queues, drafts);
		auto rejected = false;
		sender.sendNext(2, 10, saved.id, saved.entries.front().id,
			[&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); QVERIFY(queue.entries.empty()); rejected = true;
			});
		QTRY_VERIFY(rejected);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void settledDraftReloadWaitsForFreshRead() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"hold-second-draft"_s);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto initialRead = false;
		drafts.load(2, 10, [&](Delta::ComposeDraft, QString error) {
			QVERIFY(error.isEmpty()); initialRead = true;
		});
		auto reloaded = false;
		drafts.settle(2, 10, [&](QString error) {
			QVERIFY(error.isEmpty());
			drafts.reload(2, 10, [&](Delta::ComposeDraft draft, QString error) {
				QVERIFY(error.isEmpty());
				QCOMPARE(draft.text, u"Fresh draft"_s);
				reloaded = true;
			});
		});
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		QVERIFY(initialRead);
		QVERIFY(!reloaded);
		CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, u"Fresh draft"_s,
			QJsonValue(), QJsonValue(), 0, u"Text"_s});
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(reloaded);
	}

	void batchSelectionSurvivesPreparationFailure() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		QVERIFY(file->write("prepared first image") > 0);
		QVERIFY(file->flush());
		const auto selected = std::vector<Delta::ComposeDraft>{
			{u"First caption"_s, 41, u"/original/first.png"_s, u"first.png"_s, u"Image"_s},
			{u"Second caption"_s, 42, u"/original/second.mp4"_s, u"second.mp4"_s, u"Video"_s},
		};
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, selected, {});
		QVERIFY(batch->canDiscard());
		QVERIFY(!batch->canRetrySave());
		QVERIFY(batch->prepared(0, {u"First caption"_s, 41, file->fileName(), u"first.jpg"_s, u"Image"_s}, file));
		QVERIFY(batch->failed(1, u"Video conversion failed"_s));
		QCOMPARE(batch->selection().size(), size_t(2));
		QCOMPARE(batch->selection()[0].text, selected[0].text);
		QCOMPARE(batch->selection()[0].filename, u"first.jpg"_s);
		QCOMPARE(batch->selection()[1].text, selected[1].text);
		QCOMPARE(batch->selection()[1].quotedMessageId, 42);
		QCOMPARE(batch->selection()[1].file, selected[1].file);
		QVERIFY(batch->canDiscard());
		QVERIFY(!batch->canRetrySave());
		QVERIFY(batch->discard());
		QVERIFY(!batch->canDiscard());
		QVERIFY(!batch->prepared(1, selected[1], file));
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void draftSettlesAfterPendingWrites_data() {
		QTest::addColumn<bool>("rejected");
		QTest::newRow("accepted") << false;
		QTest::newRow("rejected") << true;
	}

	void draftSettlesAfterPendingWrites() {
		QFETCH(bool, rejected);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"hold-first-save"_s);
		Delta::DraftStore drafts(rpc, [](QString) {});
		drafts.save(2, 10, u"First caption"_s, 41);
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		drafts.save(2, 10, u"Updated caption"_s, 42);
		auto completed = false;
		drafts.settle(2, 10, [&](QString error) {
			QCOMPARE(error.isEmpty(), !rejected);
			QCOMPARE(drafts.busy(2, 10), rejected);
			completed = true;
		});
		auto otherCompleted = false;
		drafts.settle(3, 10, [&](QString error) { QVERIFY(error.isEmpty()); otherCompleted = true; });
		QVERIFY(otherCompleted);
		QVERIFY(!completed);
		if (rejected) CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-reject-save"_s});
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(completed);
		const auto draft = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		QCOMPARE(draft[u"text"_s].toString(), rejected ? u"First caption"_s : u"Updated caption"_s);
		QCOMPARE(draft[u"quote"_s].toObject()[u"messageId"_s].toInt(), rejected ? 41 : 42);
		completed = false;
		drafts.settle(2, 10, [&](QString error) { QCOMPARE(error.isEmpty(), !rejected); completed = true; });
		QVERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 2);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void batchPreparationOrderingAndLifetime() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-hold-checkpoint"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto files = std::vector<std::shared_ptr<QTemporaryFile>>();
		auto paths = QStringList();
		for (auto i = 0; i != 3; ++i) {
			auto file = std::make_shared<QTemporaryFile>();
			QVERIFY(file->open());
			QVERIFY(file->write("prepared attachment") > 0);
			QVERIFY(file->flush());
			paths.push_back(file->fileName());
			files.push_back(std::move(file));
		}
		auto completed = false;
		auto saved = Delta::ComposeQueue();
		auto batch = std::shared_ptr<Delta::ComposeBatch>();
		auto observed = std::weak_ptr<Delta::ComposeBatch>();
		batch = Delta::ComposeBatch::Create(queues, 2, 10, 3, [&] {
			if (const auto value = observed.lock(); value->state() == Delta::ComposeBatch::State::Saved) {
				saved = value->queue();
				completed = true;
			}
		});
		observed = batch;
		const auto prepare = [&](size_t index) {
			return batch->prepared(index, {
				u"Caption %1"_s.arg(index), int(41 + index), paths[index],
				u"Attachment %1"_s.arg(index), index == 1 ? u"Image"_s : u"File"_s,
			}, files[index]);
		};
		QVERIFY(prepare(2));
		QVERIFY(prepare(0));
		QVERIFY(!prepare(2));
		QVERIFY(!batch->prepared(3, {}, files[0]));
		QVERIFY(!batch->retrySave());
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(prepare(1));
		QCOMPARE(batch->state(), Delta::ComposeBatch::State::Saving);
		QVERIFY(!batch->discard());
		QVERIFY(!batch->retrySave());
		QVERIFY(!batch->failed(0, u"Late failure"_s));
		files.clear();
		batch.reset();
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		QVERIFY(!observed.expired());
		for (const auto &path : paths) QVERIFY(QFileInfo::exists(path));
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(completed);
		QTRY_VERIFY(observed.expired());
		for (const auto &path : paths) QVERIFY(!QFileInfo::exists(path));
		QCOMPARE(saved.entries.size(), size_t(3));
		for (auto i = 0; i != 3; ++i) {
			const auto &draft = saved.entries[i].draft;
			QCOMPARE(draft.text, u"Caption %1"_s.arg(i));
			QCOMPARE(draft.quotedMessageId, 41 + i);
			QCOMPARE(draft.filename, u"Attachment %1"_s.arg(i));
			QCOMPARE(draft.viewType, i == 1 ? u"Image"_s : u"File"_s);
			QCOMPARE(draft.file, u"/fixture/blobs/2/Attachment %1"_s.arg(i));
		}
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 4);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void batchStorageRecovery_data() {
		QTest::addColumn<QString>("fault");
		for (const auto fault : { "queue-reject-copy", "queue-reject-pin",
			"queue-reject-publish", "queue-unknown-publish" }) {
			QTest::newRow(fault) << QString::fromLatin1(fault);
		}
	}

	void batchStorageRecovery() {
		QFETCH(QString, fault);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, fault);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		QVERIFY(file->write("retained prepared bytes") > 0);
		QVERIFY(file->flush());
		const auto path = file->fileName();
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QVERIFY(batch->prepared(0, {u"Keep caption"_s, 41, path, u"image.jpg"_s, u"Image"_s}, file));
		file.reset();
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(!batch->error().isEmpty());
		QVERIFY(QFileInfo::exists(path));
		const auto uncertain = fault == u"queue-unknown-publish"_s;
		if (uncertain) {
			QVERIFY(!batch->discard());
			QVERIFY(batch->retrySave());
			QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
			QVERIFY(QFileInfo::exists(path));
		}
		const auto writes = CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt();
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
		QVERIFY(batch->retrySave());
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Saved);
		QCOMPARE(batch->reconciled(), uncertain);
		QVERIFY(!QFileInfo::exists(path));
		QCOMPARE(batch->queue().entries.front().draft.text, u"Keep caption"_s);
		QCOMPARE(batch->queue().entries.front().draft.quotedMessageId, 41);
		const auto copies = CoreCall(*rpc, u"queue_copies"_s, {})[u"result"_s].toArray();
		QVERIFY(!copies.isEmpty());
		const auto publicAccess = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
			| QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
		for (const auto &value : copies) {
			const auto copy = value.toObject();
			QCOMPARE(copy[u"size"_s].toInt(), int(QByteArray("retained prepared bytes").size()));
			QCOMPARE(copy[u"permissions"_s].toInt() & int(publicAccess), 0);
			QCOMPARE(copy[u"directoryPermissions"_s].toInt() & int(publicAccess), 0);
			const auto copiedPath = copy[u"path"_s].toString();
			QCOMPARE(QFileInfo(copiedPath).fileName(), u"image.jpg"_s);
			QVERIFY(!QFileInfo::exists(copiedPath));
			QVERIFY(!QFileInfo::exists(QFileInfo(copiedPath).absolutePath()));
		}
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writes + (uncertain ? 0 : 2));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(!batch->retrySave());
		QVERIFY(!batch->discard());
	}

	void batchPreparationFailureAndDiscard() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		QVERIFY(!Delta::ComposeBatch::Create(queues, 0, 10, 2, {}));
		QVERIFY(!Delta::ComposeBatch::Create(queues, 2, 0, 2, {}));
		QVERIFY(!Delta::ComposeBatch::Create(queues, 2, 10, 0, {}));
		QVERIFY(!Delta::ComposeBatch::Create(nullptr, 2, 10, 2, {}));
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		QVERIFY(file->write("kept until discard") > 0);
		QVERIFY(file->flush());
		const auto path = file->fileName();
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 3, {});
		QVERIFY(batch->failed(1, u"Conversion failed"_s));
		QVERIFY(batch->prepared(2, {u"Caption"_s, 41, path, u"file.bin"_s, u"File"_s}, file));
		file.reset();
		QCOMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(!batch->failed(1, u"Duplicate completion"_s));
		QVERIFY(!batch->retrySave());
		QVERIFY(QFileInfo::exists(path));
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QVERIFY(batch->discard());
		QVERIFY(!QFileInfo::exists(path));
		QCOMPARE(batch->state(), Delta::ComposeBatch::State::Discarded);
		QVERIFY(!batch->prepared(0, {}, {}));
		QVERIFY(!batch->failed(0, u"Late completion"_s));
		QVERIFY(!batch->discard());
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void batchUncertainPublicationChanged_data() {
		QTest::addColumn<QString>("change");
		for (const auto change : { "id", "text", "quote", "blob", "filename", "viewType", "messageId", "empty" }) {
			QTest::newRow(change) << QString::fromLatin1(change);
		}
	}

	void batchUncertainPublicationChanged() {
		QFETCH(QString, change);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-unknown-publish"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		QVERIFY(file->write("uncertain prepared bytes") > 0);
		QVERIFY(file->flush());
		const auto path = file->fileName();
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QVERIFY(batch->prepared(0, {u"Caption"_s, 41, path, u"file.bin"_s, u"File"_s}, file));
		file.reset();
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
		const auto key = u"ui.deltagram.compose.queue.10"_s;
		const auto before = CoreCall(*rpc, u"get_config"_s, {2, key}).value(u"result"_s);
		auto object = QJsonDocument::fromJson(before.toString().toUtf8()).object();
		if (change == u"id"_s) object[change] = QString(32, u'a');
		else {
			auto entry = object[u"entries"_s].toArray()[0].toObject();
			entry[change] = (change == u"quote"_s || change == u"messageId"_s)
				? QJsonValue(300) : QJsonValue(u"changed"_s);
			object[u"entries"_s] = QJsonArray{entry};
		}
		CoreCall(*rpc, u"set_config"_s, {2, key, change == u"empty"_s
			? QJsonValue(QJsonValue::Null)
			: QJsonValue(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)))});
		const auto writes = CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt();
		QVERIFY(batch->retrySave());
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(!batch->discard());
		QVERIFY(QFileInfo::exists(path));
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writes);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void batchConflictingQueueNotAdopted() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		QVERIFY(file->write("two separate selections") > 0);
		QVERIFY(file->flush());
		const auto path = file->fileName();
		auto first = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QVERIFY(first->prepared(0, {u"First selection"_s, 41, path, u"file.bin"_s, u"File"_s}, file));
		QTRY_COMPARE(first->state(), Delta::ComposeBatch::State::Saved);
		auto second = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QVERIFY(second->prepared(0, {u"Second selection"_s, 0, path, u"file.bin"_s, u"File"_s}, file));
		file.reset();
		QTRY_COMPARE(second->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(second->queue().id.isEmpty());
		QVERIFY(QFileInfo::exists(path));
		QVERIFY(second->retrySave());
		QTRY_COMPARE(second->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(QFileInfo::exists(path));
		QVERIFY(second->discard());
		QVERIFY(!QFileInfo::exists(path));
		auto completed = false;
		queues->load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.id, first->queue().id);
			QCOMPARE(queue.entries.front().draft.text, u"First selection"_s);
			completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 2);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void batchInvalidPreparation_data() {
		QTest::addColumn<QString>("invalid");
		for (const auto invalid : { "owner", "quote", "path", "empty" }) {
			QTest::newRow(invalid) << QString::fromLatin1(invalid);
		}
	}

	void batchInvalidPreparation() {
		QFETCH(QString, invalid);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto file = std::make_shared<QTemporaryFile>();
		QVERIFY(file->open());
		if (invalid != u"empty"_s) {
			QVERIFY(file->write("prepared bytes") > 0);
			QVERIFY(file->flush());
		}
		auto batch = Delta::ComposeBatch::Create(queues, 2, 10, 1, {});
		QVERIFY(batch->prepared(0, {u"Caption"_s, invalid == u"quote"_s ? -1 : 0,
			invalid == u"path"_s ? QString() : file->fileName(), u"file.bin"_s, u"File"_s},
			invalid == u"owner"_s ? nullptr : file));
		QCOMPARE(batch->state(), Delta::ComposeBatch::State::Failed);
		QVERIFY(!batch->retrySave());
		QVERIFY(batch->discard());
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 0);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void queueDiscard_data() {
		QTest::addColumn<QString>("phase");
		QTest::addColumn<QString>("fault");
		for (const auto phase : { "matching", "uncheckpointed", "newer", "outgoing", "empty" }) {
			QTest::newRow(phase) << QString::fromLatin1(phase) << QString();
		}
		for (const auto fault : {
			"handoff-discard-rejected", "handoff-discard-lost-ack",
			"handoff-discard-unreadable", "queue-reject-publish", "handoff-save-unreadable",
		}) QTest::newRow(fault) << u"matching"_s << QString::fromLatin1(fault);
	}

	void queueDiscard() {
		QFETCH(QString, phase);
		QFETCH(QString, fault);
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("discard fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		Delta::ComposeQueueSender sender(rpc, queues, drafts);
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stage(2, 10, {
			{u"Discard this"_s, 41, file.fileName(), u"source.bin"_s, u"File"_s},
			{u"Keep this"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s},
		}, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		const auto first = saved.entries.front();
		auto knownId = 0;
		if (phase != u"empty"_s) {
			CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, first.draft.text, first.draft.file,
				first.draft.filename, 41, first.draft.viewType});
			knownId = CoreCall(*rpc, u"get_draft"_s, {2, 10})[u"result"_s].toObject()[u"id"_s].toInt();
			if (phase != u"uncheckpointed"_s) {
				completed = false;
				queues->checkpoint(2, 10, saved.id, first.id, knownId,
					[&](Delta::ComposeQueue queue, QString error) {
						QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
					});
				QTRY_VERIFY(completed);
			}
			if (phase == u"outgoing"_s) CoreCall(*rpc, u"misc_send_draft"_s, {2, 10});
			if (phase == u"newer"_s) {
				CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, u"Keep newer draft"_s,
					u"/core/newer.bin"_s, u"newer.bin"_s, 73, u"File"_s});
			}
		} else CoreCall(*rpc, u"remove_draft"_s, {2, 10});
		const auto before = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		const auto removals = CoreCall(*rpc, u"removals"_s, {})[u"result"_s].toInt();
		CoreCall(*rpc, fault.startsWith(u"queue-"_s) ? u"queue_fault"_s : u"handoff_fault"_s, {fault});
		const auto success = fault.isEmpty() || fault == u"handoff-discard-lost-ack"_s;
		completed = false;
		sender.discardNext(2, 10, saved.id, first.id, [&](int id, Delta::ComposeQueue queue, QString error) {
			QCOMPARE(id, 0);
			QCOMPARE(error.isEmpty(), success);
			if (success) {
				QCOMPARE(queue.entries.size(), size_t(1));
				QCOMPARE(queue.entries.front().draft.text, u"Keep this"_s);
			}
			completed = true;
		});
		auto duplicateRejected = false;
		sender.sendNext(2, 10, saved.id, first.id, [&](int id, Delta::ComposeQueue, QString error) {
			QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); duplicateRejected = true;
		});
		QVERIFY(duplicateRejected);
		QTRY_VERIFY(completed);
		if (!success) {
			CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
			CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-ok"_s});
			completed = false;
			sender.discardNext(2, 10, saved.id, first.id, [&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QVERIFY(error.isEmpty());
				QCOMPARE(queue.entries.size(), size_t(1)); completed = true;
			});
			QTRY_VERIFY(completed);
		}
		QCOMPARE(CoreCall(*rpc, u"removals"_s, {})[u"result"_s].toInt(), removals
			+ ((phase == u"matching"_s || phase == u"uncheckpointed"_s)
				? (fault == u"handoff-discard-rejected"_s ? 2 : 1) : 0));
		const auto after = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		if (phase == u"newer"_s) QCOMPARE(after, before);
		else QVERIFY(after.isEmpty());
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), phase == u"outgoing"_s ? 1 : 0);
		if (phase == u"outgoing"_s) {
			QCOMPARE(CoreCall(*rpc, u"get_message"_s, {2, knownId})[u"result"_s].toObject()[u"state"_s].toInt(), 20);
		}
		completed = false;
		sender.discardNext(2, 10, saved.id, first.id, [&](int, Delta::ComposeQueue, QString error) {
			QVERIFY(!error.isEmpty()); completed = true;
		});
		QTRY_VERIFY(completed);
	}

	void draftReloadPreservesCurrentOperation() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"handoff-ok"_s);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		Delta::ComposeQueueSender sender(rpc, queues, drafts);
		auto loaded = false;
		drafts->load(2, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty()); QCOMPARE(draft.text, u"account-2"_s); loaded = true;
		});
		QTRY_VERIFY(loaded);
		CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-hold-save"_s});
		drafts->save(2, 10, u"Current composer text"_s, 73);
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		QVERIFY(drafts->busy(2, 10));
		auto rejected = 0;
		drafts->reload(2, 10, [&](Delta::ComposeDraft, QString error) {
			QVERIFY(!error.isEmpty()); ++rejected;
		});
		sender.sendNext(2, 10, u"reviewed queue"_s, u"reviewed entry"_s,
			[&](int id, Delta::ComposeQueue, QString error) {
				QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); ++rejected;
			});
		QCOMPARE(rejected, 2);
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(!drafts->busy(2, 10));
		CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-ok"_s});
		CoreCall(*rpc, u"misc_set_draft"_s,
			{2, 10, u"Core current draft"_s, u"/core/retained.bin"_s, u"retained.bin"_s, 81, u"File"_s});
		loaded = false;
		drafts->reload(2, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(draft.text, u"Core current draft"_s);
			QCOMPARE(draft.quotedMessageId, 81);
			QCOMPARE(draft.file, u"/core/retained.bin"_s);
			loaded = true;
		});
		QTRY_VERIFY(loaded);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 2);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
	}

	void queueSenderRecovery_data() {
		QTest::addColumn<QString>("phase");
		for (const auto phase : { "fresh", "draft", "outgoing", "outgoing-normalized",
			"outgoing-normalized-failed", "outgoing-normalized-delivered", "outgoing-normalized-read",
			"outgoing-text", "outgoing-quote", "outgoing-file", "outgoing-chat", "outgoing-id",
			"outgoing-state", "uncheckpointed", "changed", "missing", "new-text", "new-quote" }) {
			QTest::newRow(phase) << QString::fromLatin1(phase);
		}
	}

	void queueSenderRecovery() {
		QFETCH(QString, phase);
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("batch recovery fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto saved = Delta::ComposeQueue();
		auto loaded = false;
		queues->stage(2, 10, {
			{u"First caption"_s, 41, file.fileName(), u"source.bin"_s, u"File"_s},
			{u"Second caption"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s},
		}, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); loaded = true;
		});
		QTRY_VERIFY(loaded);
		CoreCall(*rpc, u"remove_draft"_s, {2, 10});
		auto knownId = 0;
		if (phase == u"new-text"_s || phase == u"new-quote"_s) {
			CoreCall(*rpc, u"misc_set_draft"_s, {2, 10,
				phase == u"new-text"_s ? u"Newer text"_s : u"First caption"_s,
				QJsonValue(), QJsonValue(), phase == u"new-quote"_s ? 42 : 41, u"Text"_s});
		} else if (phase != u"fresh"_s) {
			const auto first = saved.entries.front();
			const auto &draft = first.draft;
			CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, draft.text, draft.file, draft.filename, 41, draft.viewType});
			knownId = CoreCall(*rpc, u"get_draft"_s, {2, 10})[u"result"_s].toObject()[u"id"_s].toInt();
			QVERIFY(knownId > 0);
			if (phase != u"uncheckpointed"_s) {
				loaded = false;
				queues->checkpoint(2, 10, saved.id, first.id, knownId, [&](Delta::ComposeQueue queue, QString error) {
					QVERIFY(error.isEmpty()); saved = std::move(queue); loaded = true;
				});
				QTRY_VERIFY(loaded);
			}
			if (phase.startsWith(u"outgoing"_s)) {
				CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-recode-send"_s});
				CoreCall(*rpc, u"misc_send_draft"_s, {2, 10});
				auto changes = QJsonObject();
				if (phase == u"outgoing"_s) changes[u"file"_s] = draft.file;
				if (phase == u"outgoing-text"_s) changes[u"text"_s] = u"Changed caption"_s;
				if (phase == u"outgoing-quote"_s) changes[u"quote"_s] = QJsonObject{{u"messageId"_s, 42}};
				if (phase == u"outgoing-file"_s) changes[u"file"_s] = QJsonValue();
				if (phase == u"outgoing-chat"_s) changes[u"chatId"_s] = 11;
				if (phase == u"outgoing-id"_s) changes[u"id"_s] = knownId + 1;
				if (phase == u"outgoing-state"_s) changes[u"state"_s] = 16;
				if (phase == u"outgoing-normalized-failed"_s) changes[u"state"_s] = 24;
				if (phase == u"outgoing-normalized-delivered"_s) changes[u"state"_s] = 26;
				if (phase == u"outgoing-normalized-read"_s) changes[u"state"_s] = 28;
				CoreCall(*rpc, u"amend_sent_message"_s, {knownId, changes});
			}
			if (phase == u"missing"_s) CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-missing-message"_s});
			if (phase == u"changed"_s) {
				CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, u"Other draft"_s, u"/core/other.bin"_s, u"other.bin"_s, 0, u"File"_s});
			}
		}
		const auto before = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		Delta::ComposeQueueSender sender(rpc, queues, drafts);
		const auto success = phase != u"changed"_s && phase != u"missing"_s
			&& phase != u"new-text"_s && phase != u"new-quote"_s
			&& (!phase.startsWith(u"outgoing-"_s) || phase.startsWith(u"outgoing-normalized"_s));
		auto completed = false;
		auto after = Delta::ComposeQueue();
		sender.sendNext(2, 10, saved.id, saved.entries.front().id, [&](int id, Delta::ComposeQueue queue, QString error) {
			QCOMPARE(error.isEmpty(), success);
			QCOMPARE(id > 0, success);
			if (success && knownId) QCOMPARE(id, knownId);
			after = std::move(queue); completed = true;
		});
		QVERIFY(sender.busy(2, 10));
		auto duplicateRejected = false;
		sender.sendNext(2, 10, saved.id, saved.entries.front().id, [&](int id, Delta::ComposeQueue, QString error) {
			QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); duplicateRejected = true;
		});
		QVERIFY(duplicateRejected);
		QTRY_VERIFY(completed);
		QVERIFY(!sender.busy(2, 10));
		QCOMPARE(after.entries.size(), success ? size_t(1) : size_t(2));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(),
			(success || phase.startsWith(u"outgoing"_s)) ? 1 : 0);
		if (success) QCOMPARE(after.entries.front().draft.text, u"Second caption"_s);
		else QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject(), before);
		if (success) {
			completed = false;
			sender.sendNext(2, 10, saved.id, saved.entries.front().id,
				[&](int id, Delta::ComposeQueue, QString error) {
					QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); completed = true;
				});
			QTRY_VERIFY(completed);
			QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 1);
		}
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {1, 10})[u"result"_s]
			.toObject()[u"text"_s].toString(), u"account-1"_s);
	}

	void queueSenderFailures_data() {
		QTest::addColumn<QString>("fault");
		for (const auto fault : {
			"handoff-reject-save", "handoff-reject-send", "handoff-reject-removal", "handoff-recode-reject-removal",
			"handoff-uncertain-removal", "queue-reject-publish", "queue-unknown-publish",
		}) QTest::newRow(fault) << QString::fromLatin1(fault);
	}

	void queueSenderFailures() {
		QFETCH(QString, fault);
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("batch failure fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stage(2, 10, {
			{u"First caption"_s, 41, file.fileName(), u"source.bin"_s, u"File"_s},
			{u"Second caption"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s},
		}, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		CoreCall(*rpc, u"remove_draft"_s, {2, 10});
		CoreCall(*rpc, fault.startsWith(u"queue-"_s) ? u"queue_fault"_s : u"handoff_fault"_s, {fault});
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		completed = false;
		sender->sendNext(2, 10, saved.id, saved.entries.front().id, [&](int id, Delta::ComposeQueue, QString error) {
			QVERIFY(!error.isEmpty());
			QCOMPARE(id > 0, fault.endsWith(u"removal"_s));
			completed = true;
		});
		QTRY_VERIFY(completed);
		QVERIFY(!drafts->sending(2, 10));
		sender.reset();
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-normal"_s});
		CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-ok"_s});
		completed = false;
		queues->load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		const auto removed = fault == u"handoff-uncertain-removal"_s;
		QCOMPARE(saved.entries.size(), removed ? size_t(1) : size_t(2));
		sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		completed = false;
		sender->sendNext(2, 10, saved.id, saved.entries.front().id, [&](int id, Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); QVERIFY(id > 0);
			QCOMPARE(queue.entries.size(), removed ? size_t(0) : size_t(1));
			completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(),
			(fault == u"handoff-reject-send"_s || removed) ? 2 : 1);
	}

	void queueSenderClosedDuringCheckpoint() {
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("checkpoint lifetime") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stage(2, 10, {{u"Caption"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s}},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
			});
		QTRY_VERIFY(completed);
		CoreCall(*rpc, u"remove_draft"_s, {2, 10});
		CoreCall(*rpc, u"queue_fault"_s, {u"queue-hold-checkpoint"_s});
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		sender->sendNext(2, 10, saved.id, saved.entries.front().id, [](int, Delta::ComposeQueue, QString) {
			QFAIL("A closed sender invoked its callback");
		});
		QTRY_VERIFY(CoreCall(*rpc, u"is_held"_s, {})[u"result"_s].toBool());
		QVERIFY(drafts->sending(2, 10));
		sender.reset();
		queues.reset();
		CoreCall(*rpc, u"release"_s, {});
		QTRY_VERIFY(!drafts->sending(2, 10));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		completed = false;
		queues->load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.entries.size(), size_t(1));
			QVERIFY(queue.entries.front().messageId > 0);
			completed = true;
		});
		QTRY_VERIFY(completed);
	}

	void realCoreQueueSenderRestart() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.txt"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		const auto bytes = QByteArray("real Core queue recovery\n");
		QCOMPARE(file.write(bytes), bytes.size());
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		const auto accounts = temporary.filePath(u"accounts"_s);
		rpc->start(program, accounts);
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
		auto sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues->stage(account, chat, {
			{u"First file"_s, 0, file.fileName(), u"source.txt"_s, u"File"_s},
			{u"Second file"_s, 0, file.fileName(), u"source.txt"_s, u"File"_s},
		}, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		QVERIFY(file.remove());
		auto draftId = 0;
		for (auto attempt = 0; attempt != 2; ++attempt) {
			completed = false;
			sender->sendNext(account, chat, saved.id, saved.entries.front().id, [&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0);
				QVERIFY(!error.isEmpty());
				QCOMPARE(queue.entries.size(), size_t(2));
				QVERIFY(queue.entries.front().messageId > 0);
				if (draftId) QCOMPARE(queue.entries.front().messageId, draftId);
				draftId = queue.entries.front().messageId;
				saved = std::move(queue); completed = true;
			});
			QTRY_VERIFY(completed);
			const auto draft = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
			QCOMPARE(draft[u"id"_s].toInt(), draftId);
			QCOMPARE(draft[u"text"_s].toString(), u"First file"_s);
			QCOMPARE(draft[u"state"_s].toInt(), 19);
			for (const auto &entry : saved.entries) {
				QFile stored(entry.draft.file);
				QVERIFY(stored.open(QIODevice::ReadOnly));
				QCOMPARE(stored.readAll(), bytes);
			}
			if (!attempt) {
				sender.reset(); drafts.reset(); queues.reset(); rpc->stop();
				rpc->start(program, accounts);
				queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
				drafts = std::make_shared<Delta::DraftStore>(rpc, [](QString) {});
				sender = std::make_unique<Delta::ComposeQueueSender>(rpc, queues, drafts);
			}
		}
		completed = false;
		sender->discardNext(account, chat, saved.id, saved.entries.front().id,
			[&](int id, Delta::ComposeQueue queue, QString error) {
				QCOMPARE(id, 0); QVERIFY(error.isEmpty());
				QCOMPARE(queue.entries.size(), size_t(1));
				QCOMPARE(queue.entries.front().draft.text, u"Second file"_s);
				QFile stored(queue.entries.front().draft.file);
				QVERIFY(stored.open(QIODevice::ReadOnly));
				QCOMPARE(stored.readAll(), bytes);
				completed = true;
			});
		QTRY_VERIFY(completed);
		QVERIFY(CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].isNull());
	}

	void individualMessageLookup_data() {
		QTest::addColumn<QString>("mode");
		for (const auto mode : {
			u"visible"_s, u"wrong-id"_s, u"wrong-chat"_s, u"draft"_s,
			u"hidden"_s, u"read-error"_s, u"list-error"_s,
		}) {
			QTest::newRow(qPrintable(mode)) << u"lookup-"_s + mode;
		}
	}

	void individualMessageLookup() {
		QFETCH(QString, mode);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, mode);
		Delta::AccountScope scope;
		auto completed = false;
		Delta::ReadVisibleMessage(rpc, scope, 7, 10, 80, [&](QJsonObject message) {
			QCOMPARE(!message.isEmpty(), mode == u"lookup-visible"_s);
			if (!message.isEmpty()) {
				QCOMPARE(message[u"id"_s].toInt(), 80);
				QCOMPARE(message[u"text"_s].toString(), u"quote from account 7"_s);
			}
			completed = true;
		});
		QTRY_VERIFY(completed);
		rpc->stop();
	}

	void individualMessageLookupCancelsAcrossAccounts() {
		for (const auto holdList : { false, true }) {
			auto rpc = std::make_shared<DeltaRpc>();
			rpc->start(RPC_FIXTURE_PATH, u"lookup-hold-list"_s);
			Delta::AccountScope scope;
			Delta::ReadVisibleMessage(rpc, scope, 7, 0, 80, [](QJsonObject) {
				QFAIL("An individual message escaped its account");
			});
			if (holdList) {
				QTRY_COMPARE(CoreCall(*rpc, u"lookup_calls"_s, {})[u"result"_s].toArray().size(), 2);
			}
			scope.invalidate();
			QVERIFY(CoreCall(*rpc, u"release"_s, {}).contains(u"result"_s));
			rpc->stop();
		}
	}

	void attachmentCheckpoint_data() {
		QTest::addColumn<QString>("decision");
		QTest::newRow("accepted-once") << u"accept"_s;
		QTest::newRow("rejected") << u"reject"_s;
		QTest::newRow("owner-destroyed") << u"destroy"_s;
	}

	void attachmentCheckpoint() {
		QFETCH(QString, decision);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"handoff-accepted"_s);
		auto drafts = std::make_unique<Delta::DraftStore>(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto resolved = std::function<void(QString)>();
		auto completions = 0;
		auto knownId = 0;
		drafts->sendAttachment(2, 10,
			{ u"checkpoint caption"_s, 41, u"/fixture/source.bin"_s, u"source.bin"_s, u"File"_s },
			[&](int id, QString error) {
				QCOMPARE(id, decision == u"accept"_s ? knownId : 0);
				QCOMPARE(error.isEmpty(), decision == u"accept"_s);
				++completions;
			}, {}, [&](int id, Delta::ComposeDraft prepared, std::function<void(QString)> next) {
				QCOMPARE(prepared.text, u"checkpoint caption"_s);
				QCOMPARE(prepared.quotedMessageId, 41);
				knownId = id;
				resolved = std::move(next);
			});
		QTRY_VERIFY(bool(resolved));
		QVERIFY(knownId > 0);
		QVERIFY(drafts->sending(2, 10));
		QVERIFY(!drafts->preparingSend()); // The file is already a Core-owned draft.
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		if (decision == u"destroy"_s) drafts.reset();
		resolved(decision == u"reject"_s ? u"Checkpoint failed"_s : QString());
		resolved({}); // A late or duplicated continuation must never send twice.
		if (decision != u"destroy"_s) QTRY_COMPARE(completions, 1);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), decision == u"accept"_s ? 1 : 0);
		if (decision == u"destroy"_s) QCOMPARE(completions, 0);
	}

	void resumeAttachmentChecksIdentity_data() {
		QTest::addColumn<QString>("changed");
		for (const auto name : { "none", "id", "file", "caption", "quote", "filename", "type" }) {
			QTest::newRow(name) << QString::fromLatin1(name);
		}
	}

	void resumeAttachmentChecksIdentity() {
		QFETCH(QString, changed);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"handoff-accepted"_s);
		auto target = Delta::ComposeDraft{ u"resume caption"_s, 41, u"/core/source.bin"_s, u"source.bin"_s, u"File"_s };
		CoreCall(*rpc, u"misc_set_draft"_s, {2, 10, target.text, target.file, target.filename, 41, target.viewType});
		const auto original = CoreCall(*rpc, u"get_draft"_s, {2, 10})[u"result"_s].toObject();
		const auto id = original[u"id"_s].toInt();
		QVERIFY(id > 0);
		if (changed == u"file"_s) target.file += u".changed"_s;
		if (changed == u"caption"_s) target.text += u" changed"_s;
		if (changed == u"quote"_s) target.quotedMessageId = 42;
		if (changed == u"filename"_s) target.filename += u".changed"_s;
		if (changed == u"type"_s) target.viewType = u"Image"_s;
		Delta::DraftStore drafts(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto completed = false;
		drafts.resumeAttachment(2, 10, id + (changed == u"id"_s ? 1 : 0), target, [&](int sent, QString error) {
			QCOMPARE(sent, changed == u"none"_s ? id : 0);
			QCOMPARE(error.isEmpty(), changed == u"none"_s);
			completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), 1);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), changed == u"none"_s ? 1 : 0);
		if (changed != u"none"_s) QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {2, 10})[u"result"_s].toObject(), original);
	}

	void queueCheckpointFailureThenResume() {
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("checkpoint fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		Delta::ComposeQueueStore queues(rpc);
		Delta::DraftStore drafts(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		queues.stage(2, 10, {{ u"queued"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s }},
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
			});
		QTRY_VERIFY(completed);
		completed = false;
		drafts.sendAttachment(2, 10, saved.entries.front().draft, [&](int id, QString error) {
			QCOMPARE(id, 0); QVERIFY(!error.isEmpty()); completed = true;
		}, {}, [&](int id, Delta::ComposeDraft prepared, std::function<void(QString)> next) {
			CoreCall(*rpc, u"queue_fault"_s, { u"queue-unknown-publish"_s });
			queues.checkpoint(2, 10, saved.id, saved.entries.front().id, id,
				[next](Delta::ComposeQueue, QString error) { next(std::move(error)); }, prepared);
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		CoreCall(*rpc, u"queue_fault"_s, { u"queue-normal"_s });
		completed = false;
		queues.load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		const auto entry = saved.entries.front();
		QVERIFY(entry.messageId > 0);
		const auto writes = CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt();
		completed = false;
		drafts.resumeAttachment(2, 10, entry.messageId, entry.draft, [&](int id, QString error) {
			QCOMPARE(id, entry.messageId); QVERIFY(error.isEmpty()); completed = true;
		});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 1);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writes);
	}

	void composeQueueStorage_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<bool>("success");
		QTest::addColumn<int>("storedKeys");
		QTest::addColumn<int>("writeCount");
		QTest::newRow("accepted") << u"queue-normal"_s << true << 3 << 3;
		QTest::newRow("lost-write-replies") << u"queue-lost-ack"_s << true << 3 << 3;
		QTest::newRow("pin-rejected") << u"queue-reject-pin"_s << false << 0 << 2;
		QTest::newRow("publication-rejected") << u"queue-reject-publish"_s << false << 0 << 5;
		QTest::newRow("publication-uncertain") << u"queue-unknown-publish"_s << false << 3 << 3;
	}

	void composeQueueStorage() {
		QFETCH(QString, mode);
		QFETCH(bool, success);
		QFETCH(int, storedKeys);
		QFETCH(int, writeCount);
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		auto sources = std::vector<Delta::ComposeDraft>();
		for (auto i = 0; i != 2; ++i) {
			const auto name = u"attachment-%1.bin"_s.arg(i);
			QFile file(temporary.filePath(name));
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write("queue fixture") > 0);
			file.close();
			sources.push_back({ u"caption %1"_s.arg(i), 41 + i, file.fileName(), name, u"File"_s });
		}
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, mode);
		Delta::ComposeQueueStore queues(rpc);
		auto completed = false;
		auto saved = Delta::ComposeQueue();
		queues.stage(2, 10, sources, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY2(error.isEmpty() == success, qPrintable(error));
			saved = std::move(queue);
			completed = true;
		});
		QVERIFY(queues.busy(2, 10));
		auto duplicateRejected = false;
		queues.stage(2, 10, sources, [&](Delta::ComposeQueue, QString error) {
			duplicateRejected = !error.isEmpty();
		});
		QVERIFY(duplicateRejected);
		QTRY_VERIFY(completed);
		QVERIFY(!queues.busy(2, 10));
		QCOMPARE(CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject().size(), storedKeys);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writeCount);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		if (!success) return;
		QCOMPARE(saved.entries.size(), size_t(2));
		QCOMPARE(saved.entries[0].draft.text, sources[0].text);
		QCOMPARE(saved.entries[0].draft.quotedMessageId, 41);
		QCOMPARE(saved.entries[1].draft.filename, sources[1].filename);
		QVERIFY(saved.entries[0].draft.file != sources[0].file);
		completed = false;
		queues.load(3, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QVERIFY(queue.entries.empty());
			completed = true;
		});
		QTRY_VERIFY(completed);
		completed = false;
		queues.stage(2, 10, sources, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(!error.isEmpty());
			QVERIFY(queue.id.isEmpty());
			completed = true;
		});
		QTRY_VERIFY(completed);
		completed = false;
		auto canonical = saved.entries[0].draft;
		canonical.filename = u"normalized.vcf"_s;
		canonical.viewType = u"Vcard"_s;
		queues.checkpoint(2, 10, saved.id, saved.entries[0].id, 300,
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(error.isEmpty());
				QCOMPARE(queue.entries[0].messageId, 300);
				QCOMPARE(queue.entries[0].draft.filename, canonical.filename);
				QCOMPARE(queue.entries[0].draft.viewType, canonical.viewType);
				completed = true;
			}, canonical);
		QTRY_VERIFY(completed);
		completed = false;
		queues.checkpoint(2, 10, saved.id, saved.entries[0].id, 301,
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(!error.isEmpty());
				QCOMPARE(queue.entries[0].messageId, 300);
				completed = true;
			});
		QTRY_VERIFY(completed);
		completed = false;
		queues.removeFirst(2, 10, saved.id, saved.entries[1].id,
			[&](Delta::ComposeQueue queue, QString error) {
				QVERIFY(!error.isEmpty());
				QCOMPARE(queue.entries.size(), size_t(2));
				completed = true;
			});
		QTRY_VERIFY(completed);
		for (const auto &entry : saved.entries) {
			completed = false;
			queues.removeFirst(2, 10, saved.id, entry.id, [&](Delta::ComposeQueue, QString error) {
				QVERIFY(error.isEmpty());
				completed = true;
			});
			QTRY_VERIFY(completed);
		}
		QVERIFY(CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject().isEmpty());
	}

	void composeQueueRemovalRecovery_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<bool>("removed");
		QTest::addColumn<bool>("confirmed");
		QTest::newRow("rejected") << u"queue-reject-publish"_s << false << false;
		QTest::newRow("reply-lost") << u"queue-lost-ack"_s << true << true;
		QTest::newRow("readback-lost") << u"queue-unknown-publish"_s << true << false;
	}

	void composeQueueRemovalRecovery() {
		QFETCH(QString, mode);
		QFETCH(bool, removed);
		QFETCH(bool, confirmed);
		QTemporaryDir temporary;
		QFile file(temporary.filePath(u"source.bin"_s));
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write("queue fixture") > 0);
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		Delta::ComposeQueueStore queues(rpc);
		auto saved = Delta::ComposeQueue();
		auto completed = false;
		const auto source = Delta::ComposeDraft{ u"caption"_s, 0, file.fileName(), u"source.bin"_s, u"File"_s };
		queues.stage(2, 10, { source, source }, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty()); saved = std::move(queue); completed = true;
		});
		QTRY_VERIFY(completed);
		CoreCall(*rpc, u"queue_fault"_s, { mode });
		completed = false;
		queues.removeFirst(2, 10, saved.id, saved.entries.front().id, [&](Delta::ComposeQueue, QString error) {
			QCOMPARE(error.isEmpty(), confirmed); completed = true;
		});
		QTRY_VERIFY(completed);
		if (!confirmed) {
			const auto stored = CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject();
			QVERIFY(stored.contains(u"2:ui.deltagram.compose.blob."_s + saved.entries.front().id));
		}
		CoreCall(*rpc, u"queue_fault"_s, { u"queue-normal"_s });
		completed = false;
		queues.load(2, 10, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.entries.size(), removed ? size_t(1) : size_t(2));
			completed = true;
		});
		QTRY_VERIFY(completed);
		completed = false;
		queues.cleanup(2, [&](QString error) { QVERIFY(error.isEmpty()); completed = true; });
		QVERIFY(queues.busy(2, 10));
		QTRY_VERIFY(completed);
		const auto stored = CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject();
		QCOMPARE(stored.size(), removed ? 2 : 3);
		QCOMPARE(stored.contains(u"2:ui.deltagram.compose.blob."_s + saved.entries.front().id), !removed);
		QVERIFY(stored.contains(u"2:ui.deltagram.compose.blob."_s + saved.entries.back().id));
	}

	void composeQueueCleanupPreservesUnreadableQueues() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"queue-normal"_s);
		const auto pin = u"ui.deltagram.compose.blob.0123456789abcdef0123456789abcdef"_s;
		CoreCall(*rpc, u"set_config"_s, {2, pin, u"$BLOBDIR/keep.bin"_s});
		CoreCall(*rpc, u"set_config"_s, {3, pin, u"$BLOBDIR/alternate.bin"_s});
		CoreCall(*rpc, u"set_config"_s, {2, u"ui.deltagram.compose.queue.10"_s, u"unreadable"_s});
		Delta::ComposeQueueStore queues(rpc);
		auto completed = false;
		queues.cleanup(2, [&](QString error) { QVERIFY(!error.isEmpty()); completed = true; });
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject().size(), 3);
		CoreCall(*rpc, u"set_config"_s, {2, u"ui.deltagram.compose.queue.10"_s, QJsonValue(QJsonValue::Null)});
		completed = false;
		queues.cleanup(2, [&](QString error) { QVERIFY(error.isEmpty()); completed = true; });
		QTRY_VERIFY(completed);
		const auto stored = CoreCall(*rpc, u"queue_config"_s, {})[u"result"_s].toObject();
		QCOMPARE(stored.size(), 1);
		QVERIFY(stored.contains(u"3:"_s + pin));
	}

	void realCorePreparedBatchRestart_data() {
		QTest::addColumn<QString>("mode");
		for (const auto mode : { "different-bytes", "same-bytes", "different-extensions" }) {
			QTest::newRow(mode) << QString::fromLatin1(mode);
		}
	}

	void realCorePreparedBatchRestart() {
		QFETCH(QString, mode);
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		auto sources = std::vector<Delta::ComposeDraft>();
		auto owners = std::vector<std::shared_ptr<QTemporaryFile>>();
		for (auto i = 0; i != 2; ++i) {
			const auto name = u"source-%1"_s.arg(i)
				+ ((i == 1 && mode == u"different-extensions"_s) ? u".txt"_s : u".bin"_s);
			auto file = std::make_shared<QTemporaryFile>(temporary.filePath(name + u"-XXXXXX"_s));
			QVERIFY(file->open());
			QVERIFY(file->write(QByteArray("durable batch ")
				+ QByteArray::number(mode == u"different-bytes"_s ? i : 0)) > 0);
			QVERIFY(file->flush());
			sources.push_back({ u"caption %1"_s.arg(i), 0, file->fileName(), name, u"File"_s });
			owners.push_back(std::move(file));
		}
		auto rpc = std::make_shared<DeltaRpc>();
		const auto accounts = temporary.filePath(u"accounts"_s);
		rpc->start(program, accounts);
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		auto queues = std::make_shared<Delta::ComposeQueueStore>(rpc);
		auto batch = Delta::ComposeBatch::Create(queues, account, chat, 2, {});
		QVERIFY(batch->prepared(1, sources[1], owners[1]));
		QVERIFY(batch->prepared(0, sources[0], owners[0]));
		owners.clear();
		QTRY_COMPARE(batch->state(), Delta::ComposeBatch::State::Saved);
		const auto saved = batch->queue();
		auto completed = false;
		QCOMPARE(saved.entries.size(), size_t(2));
		for (const auto &source : sources) QVERIFY(!QFileInfo::exists(source.file));
		const auto &first = saved.entries.front().draft;
		QVERIFY(!CoreCall(*rpc, u"misc_set_draft"_s, {account, chat, first.text, first.file,
			first.filename, QJsonValue(), first.viewType}).contains(u"error"_s));
		const auto draft = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
		QCOMPARE(draft[u"file"_s].toString(), first.file);
		const auto id = draft[u"id"_s].toInt();
		QVERIFY(id > 0);
		completed = false;
		queues->checkpoint(account, chat, saved.id, saved.entries.front().id, id,
			[&](Delta::ComposeQueue, QString error) { QVERIFY(error.isEmpty()); completed = true; });
		QTRY_VERIFY(completed);
		rpc->stop();
		rpc->start(program, accounts);
		Delta::ComposeQueueStore reopened(rpc);
		completed = false;
		reopened.load(account, chat, [&](Delta::ComposeQueue queue, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(queue.id, saved.id);
			QCOMPARE(queue.entries.size(), size_t(2));
			QCOMPARE(queue.entries.front().messageId, id);
			for (auto i = 0; i != 2; ++i) {
				const auto &entry = queue.entries[i];
				QCOMPARE(entry.draft.text, sources[i].text);
				QCOMPARE(entry.draft.filename, sources[i].filename);
				QFile file(entry.draft.file);
				QVERIFY(file.open(QIODevice::ReadOnly));
				QCOMPARE(file.readAll(), QByteArray("durable batch ")
					+ QByteArray::number(mode == u"different-bytes"_s ? i : 0));
			}
			completed = true;
		});
		QTRY_VERIFY(completed);
		for (const auto &entry : saved.entries) {
			completed = false;
			reopened.removeFirst(account, chat, saved.id, entry.id,
				[&](Delta::ComposeQueue, QString error) { QVERIFY(error.isEmpty()); completed = true; });
			QTRY_VERIFY(completed);
			QVERIFY(CoreCall(*rpc, u"get_config"_s,
				{account, u"ui.deltagram.compose.blob."_s + entry.id})[u"result"_s].isNull());
		}
		QVERIFY(CoreCall(*rpc, u"get_config"_s,
			{account, u"ui.deltagram.compose.queue.%1"_s.arg(chat)})[u"result"_s].isNull());
		rpc->stop();
	}

	void preparedAttachmentPreservesChosenRepresentation() {
		const auto original = QByteArray("original clipboard image with metadata");
		const auto parts = std::vector<QByteArray>{ "resized ", "photo without metadata" };
		QCOMPARE(Delta::PreparedAttachmentBytes(true, original, parts),
			QByteArray("resized photo without metadata"));
		QCOMPARE(Delta::PreparedAttachmentBytes(false, original, parts), original);
		QCOMPARE(Delta::PreparedAttachmentBytes(true, {}, parts),
			QByteArray("resized photo without metadata"));
		QCOMPARE(Delta::PreparedAttachmentBytes(false, {}, parts),
			QByteArray("resized photo without metadata"));
		QCOMPARE(Delta::PreparedAttachmentBytes(true, original, {}), original);
		QVERIFY(Delta::PreparedAttachmentBytes(true, {}, {}).isEmpty());
	}

	void backupCancellation_data() {
		QTest::addColumn<bool>("early");
		QTest::newRow("cancel-during-preparation") << true;
		QTest::newRow("cancel-after-qr") << false;
	}

	void backupCancellation() {
		QFETCH(bool, early);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"backup"_s);
		Delta::BackupTransfers transfers(rpc);
		auto ready = 0;
		auto completed = 0;
		const auto cancel = transfers.start(3, [&](QString svg, QString error) {
			QCOMPARE(svg, u"<svg/>"_s);
			QVERIFY(error.isEmpty());
			++ready;
		}, [&](QString) { ++completed; });
		QVERIFY(cancel);
		if (early) { cancel(); cancel(); }
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 0);
		auto duplicateRejected = false;
		const auto duplicate = transfers.start(3, {}, [&](QString error) {
			duplicateRejected = !error.isEmpty();
		});
		QVERIFY(!duplicate);
		QVERIFY(duplicateRejected);
		CoreCall(*rpc, u"release_backup_qr"_s, {3});
		if (!early) {
			QTRY_COMPARE(ready, 1);
			cancel();
			cancel();
		}
		QTRY_COMPARE(completed, 1);
		QCOMPARE(ready, early ? 0 : 1);
		QCOMPARE(BackupCalls(*rpc, u"provide_backup"_s), 1);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s, 3), 1);
		QCOMPARE(BackupCalls(*rpc, u"create_qr_svg"_s), early ? 0 : 1);
		const auto next = transfers.start(3, {}, [&](QString) { ++completed; });
		QVERIFY(next);
		QCOMPARE(BackupCalls(*rpc, u"provide_backup"_s), 2);
		CoreCall(*rpc, u"finish_backup"_s, {3});
		QTRY_COMPARE(completed, 2);
		cancel(); // An old dialog cannot stop a later attempt.
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 1);
		rpc->stop();
	}

	void backupOwnerDestructionAndAccountIsolation() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"backup"_s);
		auto completed = 0;
		{
			Delta::BackupTransfers transfers(rpc);
			const auto first = transfers.start(3, {}, [&](QString) { ++completed; });
			const auto second = transfers.start(2, {}, [&](QString) { ++completed; });
			QVERIFY(first && second);
			first();
			CoreCall(*rpc, u"release_backup_qr"_s, {3});
			QTRY_COMPARE(completed, 1);
			QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s, 2), 0);
		}
		// Destruction cancels the still-preparing transfer even after its handle is gone.
		CoreCall(*rpc, u"release_backup_qr"_s, {2});
		QTRY_COMPARE(completed, 2);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s, 3), 1);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s, 2), 1);
		QCOMPARE(BackupCalls(*rpc, u"create_qr_svg"_s), 0);
		rpc->stop();
	}

	void backupIgnoresLateReadAfterCompletion() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"backup"_s);
		Delta::BackupTransfers transfers(rpc);
		auto ready = 0;
		auto completed = 0;
		const auto old = transfers.start(3, [&](QString, QString) { ++ready; },
			[&](QString) { ++completed; });
		old();
		CoreCall(*rpc, u"finish_backup"_s, {3});
		QTRY_COMPARE(completed, 1);
		const auto next = transfers.start(3, [&](QString, QString) { ++ready; },
			[&](QString) { ++completed; });
		CoreCall(*rpc, u"release_backup_qr"_s, {3}); // Late result for old attempt.
		QCOMPARE(BackupCalls(*rpc, u"create_qr_svg"_s), 0);
		old();
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 0);
		CoreCall(*rpc, u"release_backup_qr"_s, {3});
		QTRY_COMPARE(ready, 1);
		next();
		QTRY_COMPARE(completed, 2);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 1);
		rpc->stop();
	}

	void backupCancellationSurvivesReadTimeout() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"backup"_s);
		Delta::BackupTransfers transfers(rpc);
		auto ready = 0;
		auto completed = 0;
		const auto cancel = transfers.start(3, [&](QString, QString) { ++ready; },
			[&](QString) { ++completed; });
		cancel();
		CoreCall(*rpc, u"error_backup_qr"_s, {3});
		QTRY_COMPARE(BackupCalls(*rpc, u"get_backup_qr"_s), 2);
		QCOMPARE(BackupCalls(*rpc, u"provide_backup"_s), 1);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 0);
		CoreCall(*rpc, u"release_backup_qr"_s, {3});
		QTRY_COMPARE(completed, 1);
		QCOMPARE(ready, 0);
		QCOMPARE(BackupCalls(*rpc, u"stop_ongoing_process"_s), 1);
		rpc->stop();
	}

	void backupCoreFailureCompletesOnce() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, u"backup"_s);
		Delta::BackupTransfers transfers(rpc);
		auto completed = 0;
		const auto cancel = transfers.start(3, {}, [&](QString error) {
			QVERIFY(!error.isEmpty());
			++completed;
		});
		cancel();
		QCOMPARE(BackupCalls(*rpc, u"provide_backup"_s), 1);
		CoreCall(*rpc, u"crash"_s, {});
		QTRY_COMPARE(completed, 1);
		QVERIFY(!rpc->running());
		cancel();
		QCOMPARE(completed, 1);
		rpc->stop();
	}

	void draftHandoffFailures_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<bool>("success");
		QTest::addColumn<int>("sends");
		QTest::addColumn<bool>("attachment");
		QTest::newRow("accepted") << u"handoff-ok"_s << true << 1 << false;
		QTest::newRow("accepted-reply-lost") << u"handoff-lost-ack"_s << true << 1 << false;
		QTest::newRow("save-rejected") << u"handoff-reject-save"_s << false << 0 << false;
		QTest::newRow("send-rejected") << u"handoff-reject-send"_s << false << 1 << false;
		QTest::newRow("attachment-preserved") << u"attachment"_s << false << 0 << false;
		QTest::newRow("file-accepted") << u"handoff-ok"_s << true << 1 << true;
		QTest::newRow("file-accepted-reply-lost") << u"handoff-lost-ack"_s << true << 1 << true;
	}

	void draftHandoffFailures() {
		QFETCH(QString, mode);
		QFETCH(bool, success);
		QFETCH(int, sends);
		QFETCH(bool, attachment);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(QCoreApplication::applicationDirPath() + u"/delta_rpc_fixture"_s, mode);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto completed = false;
		auto duplicateRejected = false;
		const auto done = [&](int id, QString error) {
			QCOMPARE(id > 0, success);
			QCOMPARE(error.isEmpty(), success);
			completed = true;
		};
		if (attachment) {
			drafts.sendAttachment(2, 10, Delta::ComposeDraft{
				u"Keep this message"_s, 0, u"/fixture/upload.bin"_s, u"upload.bin"_s, u"File"_s,
			}, done);
		} else {
			drafts.sendText(2, 10, u"Keep this message"_s, 0, done);
		}
		drafts.sendText(2, 10, u"Duplicate"_s, 0, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
			duplicateRejected = true;
		});
		QVERIFY(duplicateRejected);
		drafts.save(2, 10, {}, 0);
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), sends);
		const auto draft = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		if (success) {
			QVERIFY(draft.isEmpty());
		} else if (sends) {
			QCOMPARE(draft[u"text"_s].toString(), u"Keep this message"_s);
		} else {
			QCOMPARE(draft[u"text"_s].toString(), u"account-2"_s);
		}
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {1, 10})[u"result"_s]
			.toObject()[u"text"_s].toString(), u"account-1"_s);
		rpc->stop();
	}

	void attachmentRemoval_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<QString>("expectedFile");
		QTest::addColumn<bool>("success");
		QTest::newRow("remove-file-keep-caption") << u"attachment"_s << u"/fixture/attachment.bin"_s << true;
		QTest::newRow("remove-reply-lost") << u"discard-lost-ack"_s << u"/fixture/attachment.bin"_s << true;
		QTest::newRow("remove-rejected") << u"reject"_s << u"/fixture/attachment.bin"_s << false;
		QTest::newRow("stale-file-preserved") << u"attachment"_s << u"/fixture/older.bin"_s << false;
	}

	void attachmentRemoval() {
		QFETCH(QString, mode);
		QFETCH(QString, expectedFile);
		QFETCH(bool, success);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(QCoreApplication::applicationDirPath() + u"/delta_rpc_fixture"_s, mode);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto completed = false;
		drafts.discardAttachment(2, 10, expectedFile, [&](bool removed) {
			QCOMPARE(removed, success);
			completed = true;
		});
		QVERIFY(drafts.preparingSend());
		drafts.save(2, 10, {}, 0);
		QTRY_VERIFY(completed);
		QVERIFY(!drafts.sending(2, 10));
		auto loaded = false;
		drafts.load(2, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(draft.file.isEmpty(), success);
			QCOMPARE(draft.text, u"account-2"_s);
			loaded = true;
		});
		QTRY_VERIFY(loaded);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {1, 10})[u"result"_s]
			.toObject()[u"file"_s].toString(), u"/fixture/attachment.bin"_s);
		rpc->stop();
	}

	void attachmentSaveReplyLost() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(QCoreApplication::applicationDirPath() + u"/delta_rpc_fixture"_s,
			u"handoff-save-lost-ack"_s);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto completed = false;
		drafts.sendAttachment(2, 10, Delta::ComposeDraft{
			u"Caption"_s, 0, u"/picker/upload.bin"_s, u"upload.bin"_s, u"File"_s,
		}, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
			completed = true;
		});
		QTRY_VERIFY(completed);
		auto loaded = false;
		drafts.load(2, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(draft.file, u"/core/managed.bin"_s);
			QCOMPARE(draft.text, u"Caption"_s);
			loaded = true;
		});
		QTRY_VERIFY(loaded);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		rpc->stop();
	}

	void preparedAttachmentOwnership_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<bool>("owned");
		QTest::addColumn<bool>("sent");
		QTest::addColumn<int>("sends");
		QTest::newRow("accepted") << u"handoff-ok"_s << true << true << 1;
		QTest::newRow("send-rejected") << u"handoff-reject-send"_s << true << false << 1;
		QTest::newRow("send-reply-lost") << u"handoff-lost-ack"_s << true << true << 1;
		QTest::newRow("save-rejected") << u"handoff-reject-save"_s << false << false << 0;
		QTest::newRow("save-reply-lost") << u"handoff-save-lost-ack"_s << true << false << 0;
		QTest::newRow("different-quote") << u"handoff-wrong-quote"_s << false << false << 0;
	}

	void preparedAttachmentOwnership() {
		QFETCH(QString, mode);
		QFETCH(bool, owned);
		QFETCH(bool, sent);
		QFETCH(int, sends);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, mode);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto order = QStringList();
		auto completed = false;
		drafts.sendAttachment(2, 10, Delta::ComposeDraft{
			u"Prepared caption"_s, 73, u"/picker/image.jpg"_s, u"image.jpg"_s, u"Image"_s,
		}, [&](int id, QString error) {
			QCOMPARE(id > 0, sent);
			QCOMPARE(error.isEmpty(), sent);
			order.push_back(u"finished"_s);
			completed = true;
		}, [&](Delta::ComposeDraft saved) {
			QVERIFY(!drafts.preparingSend());
			QVERIFY(drafts.sending(2, 10));
			QCOMPARE(saved.text, u"Prepared caption"_s);
			QCOMPARE(saved.quotedMessageId, 73);
			QVERIFY(!saved.file.isEmpty());
			order.push_back(u"owned"_s);
		});
		QVERIFY(drafts.preparingSend());
		QTRY_VERIFY(completed);
		QCOMPARE(order, owned
			? QStringList({u"owned"_s, u"finished"_s})
			: QStringList({u"finished"_s}));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), sends);
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {1, 10})[u"result"_s]
			.toObject()[u"text"_s].toString(), u"account-1"_s);
		rpc->stop();
	}

	void preparedAttachmentRetry_data() {
		QTest::addColumn<QString>("mode");
		QTest::addColumn<bool>("rejectAgain");
		QTest::addColumn<bool>("replaceDraft");
		QTest::newRow("retry-after-rejection") << u"handoff-reject-save"_s << false << false;
		QTest::newRow("repeated-rejection") << u"handoff-reject-save"_s << true << false;
		QTest::newRow("different-draft-preserved") << u"handoff-reject-save"_s << false << true;
		QTest::newRow("unconfirmed-save-preserved") << u"handoff-save-unreadable"_s << false << false;
	}

	void preparedAttachmentRetry() {
		QFETCH(QString, mode);
		QFETCH(bool, rejectAgain);
		QFETCH(bool, replaceDraft);
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(RPC_FIXTURE_PATH, mode);
		Delta::DraftStore drafts(rpc, [](QString) {});
		const auto target = Delta::ComposeDraft{
			u"Retained caption"_s, 73, u"/picker/image.jpg"_s, u"image.jpg"_s, u"Image"_s,
		};
		auto completed = 0;
		auto owned = 0;
		drafts.sendAttachment(2, 10, target, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
			++completed;
		}, [&](Delta::ComposeDraft) { ++owned; });
		QTRY_COMPARE(completed, 1);
		QCOMPARE(owned, 0);
		QVERIFY(!drafts.sending(2, 10));
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), 0);
		if (!rejectAgain) CoreCall(*rpc, u"handoff_fault"_s, {u"handoff-ok"_s});
		if (replaceDraft) {
			CoreCall(*rpc, u"misc_set_draft"_s, {
				2, 10, u"Another caption"_s, u"/core/other.bin"_s, u"other.bin"_s, 81, u"File"_s,
			});
		}
		const auto before = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		const auto writes = CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt();
		const auto success = !rejectAgain && !replaceDraft && mode == u"handoff-reject-save"_s;
		auto sentId = 0;
		drafts.sendAttachment(2, 10, target, [&](int id, QString error) {
			QCOMPARE(id > 0, success);
			QCOMPARE(error.isEmpty(), success);
			sentId = id;
			++completed;
		}, [&](Delta::ComposeDraft saved) {
			QCOMPARE(saved.text, target.text);
			QCOMPARE(saved.quotedMessageId, target.quotedMessageId);
			QCOMPARE(saved.file, target.file);
			++owned;
		});
		auto duplicateRejected = false;
		drafts.sendAttachment(2, 10, target, [&](int id, QString error) {
			QCOMPARE(id, 0);
			QVERIFY(!error.isEmpty());
			duplicateRejected = true;
		});
		QVERIFY(duplicateRejected);
		QTRY_COMPARE(completed, 2);
		QCOMPARE(owned, success ? 1 : 0);
		QCOMPARE(CoreCall(*rpc, u"sends"_s, {})[u"result"_s].toInt(), success ? 1 : 0);
		QCOMPARE(CoreCall(*rpc, u"writes"_s, {})[u"result"_s].toInt(), writes + ((success || rejectAgain) ? 1 : 0));
		const auto after = CoreCall(*rpc, u"inspect_draft"_s, {2, 10})[u"result"_s].toObject();
		if (success) {
			QVERIFY(after.isEmpty());
			const auto sent = CoreCall(*rpc, u"get_message"_s, {2, sentId})[u"result"_s].toObject();
			QCOMPARE(sent[u"text"_s].toString(), target.text);
			QCOMPARE(sent[u"quote"_s].toObject()[u"messageId"_s].toInt(), target.quotedMessageId);
			QCOMPARE(sent[u"file"_s].toString(), target.file);
		} else {
			QCOMPARE(after, before);
		}
		QCOMPARE(CoreCall(*rpc, u"inspect_draft"_s, {1, 10})[u"result"_s]
			.toObject()[u"text"_s].toString(), u"account-1"_s);
		rpc->stop();
	}

	void realCoreRejectedDraftHandoff() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		auto rpc = std::make_shared<DeltaRpc>();
		const auto accounts = temporary.filePath(u"accounts"_s);
		rpc->start(program, accounts);
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		Delta::DraftStore drafts(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto completed = false;
		drafts.sendText(account, chat, u"Durable Core handoff"_s, 0, [&](int id, QString error) {
			// An unconfigured account cannot send, even to Saved Messages.
			QVERIFY(!error.isEmpty());
			QCOMPARE(id, 0);
			completed = true;
		});
		QVERIFY(drafts.sending(account, chat));
		drafts.save(account, chat, {}, 0); // A stale UI clear cannot erase the pending draft.
		QTRY_VERIFY(completed);
		QVERIFY(!drafts.sending(account, chat));
		rpc->stop();
		rpc->start(program, accounts);
		const auto persisted = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
		QCOMPARE(persisted[u"text"_s].toString(), u"Durable Core handoff"_s);
		QCOMPARE(persisted[u"state"_s].toInt(), 19);
		rpc->stop();
	}

	void realCoreRejectedAttachmentHandoff() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		const auto original = temporary.filePath(u"attachment.txt"_s);
		auto file = QFile(original);
		QVERIFY(file.open(QIODevice::WriteOnly));
		const auto bytes = QByteArray("Core owns the recoverable attachment bytes.\n");
		QCOMPARE(file.write(bytes), bytes.size());
		file.close();
		auto rpc = std::make_shared<DeltaRpc>();
		const auto accounts = temporary.filePath(u"accounts"_s);
		rpc->start(program, accounts);
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto chat = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {account, 1})[u"result"_s].toInt();
		QVERIFY(account > 0 && chat > 0);
		Delta::DraftStore drafts(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto completed = false;
		auto owned = false;
		drafts.sendAttachment(account, chat,
			Delta::ComposeDraft{ u"Recoverable attachment caption"_s, 0, original, u"attachment.txt"_s, u"File"_s },
			[&](int id, QString error) {
				QCOMPARE(id, 0); // This account has no configured transport.
				QVERIFY(!error.isEmpty());
				QVERIFY(owned);
				completed = true;
			}, [&](Delta::ComposeDraft prepared) {
				QVERIFY(!prepared.file.isEmpty() && prepared.file != original);
				QFile saved(prepared.file);
				QVERIFY(saved.open(QIODevice::ReadOnly));
				QCOMPARE(saved.readAll(), bytes);
				// The input can disappear before send completion: Core already
				// owns the bytes needed for restart recovery.
				QVERIFY(QFile::remove(original));
				owned = true;
			});
		QVERIFY(drafts.preparingSend());
		drafts.save(account, chat, {}, 0); // A stale donor clear cannot erase this file.
		QTRY_VERIFY(completed);
		QVERIFY(!drafts.preparingSend());
		auto recovered = Delta::ComposeDraft();
		drafts.load(account, chat, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			recovered = std::move(draft);
		});
		QTRY_VERIFY(!recovered.file.isEmpty());
		QVERIFY(recovered.file != original);
		QVERIFY(!QFile::exists(original));
		rpc->stop();
		rpc->start(program, accounts);
		const auto persisted = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
		QCOMPARE(persisted[u"text"_s].toString(), u"Recoverable attachment caption"_s);
		QCOMPARE(persisted[u"state"_s].toInt(), 19);
		QCOMPARE(persisted[u"fileName"_s].toString(), u"attachment.txt"_s);
		QCOMPARE(persisted[u"file"_s].toString(), recovered.file);
		QFile saved(recovered.file);
		QVERIFY(saved.open(QIODevice::ReadOnly));
		QCOMPARE(saved.readAll(), bytes);
		completed = false;
		drafts.resumeAttachment(account, chat, persisted[u"id"_s].toInt(), recovered,
			[&](int id, QString error) {
				QCOMPARE(id, 0); // Still no configured transport; preserve this ID.
				QVERIFY(!error.isEmpty());
				completed = true;
			});
		QTRY_VERIFY(completed);
		QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject()[u"id"_s],
			persisted[u"id"_s]);
		auto removed = false;
		drafts.discardAttachment(account, chat, recovered.file, [&](bool success) {
			QVERIFY(success);
			removed = true;
		});
		QTRY_VERIFY(removed);
		const auto caption = CoreCall(*rpc, u"get_draft"_s, {account, chat})[u"result"_s].toObject();
		QVERIFY(caption[u"file"_s].toString().isEmpty());
		QCOMPARE(caption[u"text"_s].toString(), u"Recoverable attachment caption"_s);
		rpc->stop();
	}

	void editingFollowsCoreCapabilities() {
		const auto editable = QJsonObject{
			{u"fromId"_s, 1}, {u"isInfo"_s, false}, {u"hasHtml"_s, false},
			{u"viewType"_s, u"Text"_s}, {u"text"_s, u"Original"_s},
		};
		QVERIFY(Delta::CanEditCoreMessage(editable));
		QVERIFY(!Delta::CanEditCoreMessage({}));
		for (const auto &field : {u"fromId"_s, u"isInfo"_s, u"hasHtml"_s,
				u"viewType"_s, u"text"_s}) {
			auto missing = editable;
			missing.remove(field);
			QVERIFY(!Delta::CanEditCoreMessage(missing));
		}
		for (const auto &change : {
				QJsonObject{{u"fromId"_s, 10}},
				QJsonObject{{u"isInfo"_s, true}},
				QJsonObject{{u"hasHtml"_s, true}},
				QJsonObject{{u"hasHtml"_s, u"false"_s}},
				QJsonObject{{u"viewType"_s, u"Call"_s}},
				QJsonObject{{u"text"_s, u""_s}}}) {
			auto message = editable;
			message.insert(change.begin().key(), change.begin().value());
			QVERIFY(!Delta::CanEditCoreMessage(message));
		}
		auto caption = editable;
		caption.insert(u"viewType"_s, u"File"_s);
		QVERIFY(Delta::CanEditCoreMessage(caption));
	}

	void accountDiscoveryRequiresConfirmedState() {
		for (const auto reply : {QJsonObject{{u"result"_s, QJsonArray()}},
				QJsonObject{{u"result"_s, QJsonValue()}}, QJsonObject{{u"error"_s, u"unavailable"_s}}}) {
			auto completed = false;
			Delta::ReadAccountInventory([&](QString method, QJsonArray, DeltaRpc::Callback done) {
				QCOMPARE(method, u"get_all_account_ids"_s);
				done(reply);
			}, [&](Delta::AccountInventory result) {
				QCOMPARE(result.error.isEmpty(), reply[u"result"_s].isArray());
				QVERIFY(result.configured.empty());
				QCOMPARE(result.unconfigured, 0);
				completed = true;
			});
			QVERIFY(completed);
		}
		const auto replies = QVector<QJsonObject>{
			{{u"result"_s, false}}, {{u"result"_s, true}},
			{{u"result"_s, QJsonValue()}}, {{u"result"_s, 0}},
			{{u"result"_s, u"false"_s}}, {},
			{{u"result"_s, false}, {u"error"_s, u"read failed"_s}},
		};
		for (auto index = 0; index != replies.size(); ++index) {
			auto pending = QMap<int, DeltaRpc::Callback>();
			auto result = std::optional<Delta::AccountInventory>();
			auto completed = 0;
			Delta::ReadAccountInventory([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
				if (method == u"get_all_account_ids"_s) {
					done(QJsonObject{{u"result"_s, QJsonArray{9, 2}}});
				} else {
					QCOMPARE(method, u"is_configured"_s);
					QCOMPARE(params.size(), 1);
					pending.insert(params[0].toInt(), std::move(done));
				}
			}, [&](Delta::AccountInventory inventory) {
				result = std::move(inventory);
				++completed;
			});
			QCOMPARE(pending.size(), 2);
			pending[2](QJsonObject{{u"result"_s, false}});
			QVERIFY(!result.has_value());
			pending[9](replies[index]);
			QCOMPARE(completed, 1);
			QVERIFY(result.has_value());
			if (index >= 2) {
				QVERIFY(!result->error.isEmpty());
				QVERIFY(result->configured.empty());
				QCOMPARE(result->unconfigured, 0);
			} else {
				QVERIFY(result->error.isEmpty());
				QCOMPARE(result->unconfigured, index == 0 ? 9 : 2);
				QCOMPARE(result->configured, index == 0 ? QVector<int>() : QVector<int>{9});
			}
		}
		for (const auto ids : {QJsonArray{0}, QJsonArray{-1}, QJsonArray{9, 9},
				QJsonArray{1.5}, QJsonArray{u"9"_s}}) {
			auto completed = false;
			Delta::ReadAccountInventory([&](QString method, QJsonArray, DeltaRpc::Callback done) {
				QCOMPARE(method, u"get_all_account_ids"_s);
				done(QJsonObject{{u"result"_s, ids}});
			}, [&](Delta::AccountInventory result) {
				QVERIFY(!result.error.isEmpty());
				QVERIFY(result.configured.empty());
				QCOMPARE(result.unconfigured, 0);
				completed = true;
			});
			QVERIFY(completed);
		}
	}

	void downloadsRespectCoreStateAndAccount() {
		for (const auto state : {u"Available"_s, u"Failure"_s, u"Done"_s,
				u"InProgress"_s, u"Undecipherable"_s, u"unknown"_s,
				u"reject-read"_s, u"reject-write"_s}) {
			auto rpc = std::make_shared<DeltaRpc>();
			rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"download-"_s + state);
			Delta::AccountScope scope;
			auto completed = false;
			auto error = QString();
			Delta::DownloadCoreMessage(rpc, scope, 7, 99, [&](QString result) {
				error = result;
				completed = true;
			});
			QTRY_VERIFY(completed);
			const auto accepted = Delta::CanDownloadMessageState(state);
			QCOMPARE(error.isEmpty(), accepted || state == u"Done"_s || state == u"InProgress"_s);
			const auto calls = CoreCall(*rpc, u"download_requests"_s, {})[u"result"_s].toArray();
			QCOMPARE(calls.size(), accepted || state == u"reject-write"_s ? 2 : 1);
			for (const auto &call : calls) {
				QCOMPARE(call.toObject()[u"params"_s].toArray(), (QJsonArray{7, 99}));
			}
			if (calls.size() == 2) {
				QCOMPARE(calls.last().toObject()[u"method"_s].toString(), u"download_full_message"_s);
			}
			rpc->stop();
		}
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"download-Available"_s);
		Delta::AccountScope scope;
		Delta::DownloadCoreMessage(rpc, scope, 7, 99, [](QString) {
			QFAIL("A download callback escaped its account");
		});
		scope.invalidate();
		const auto calls = CoreCall(*rpc, u"download_requests"_s, {})[u"result"_s].toArray();
		QCOMPARE(calls.size(), 1);
		rpc->stop();
	}

	void eventsOnlyExposeVisibleHistory() {
		for (const auto mode : {u"normal"_s, u"history-reject-list"_s}) {
			auto rpc = std::make_shared<DeltaRpc>();
			rpc->start(u"" RPC_FIXTURE_PATH ""_s, mode);
			Delta::AccountScope scope;
			auto completed = 0;
			Delta::CheckMessageVisibility(rpc, scope, 1, 10, 80, [&](bool visible) {
				QCOMPARE(visible, mode == u"normal"_s);
				++completed;
			});
			Delta::CheckMessageVisibility(rpc, scope, 1, 10, 999, [&](bool visible) {
				QVERIFY(!visible);
				++completed;
			});
			QTRY_COMPARE(completed, 2);
			Delta::CheckMessageVisibility(rpc, scope, 1, 10, 80, [&](bool) {
				QFAIL("An old account event escaped cancellation");
			});
			scope.invalidate();
			QVERIFY(CoreCall(*rpc, u"history_state"_s, {}).contains(u"result"_s));
			rpc->stop();
		}
	}

	void attachmentCacheIdentityIncludesAccount() {
		const auto first = Delta::MediaIdentity(1, 12);
		const auto second = Delta::MediaIdentity(2, 12);
		QVERIFY(first != second);
		auto cache = QHash<quint64, QByteArray>();
		cache.insert(first, "primary attachment");
		cache.insert(second, "alternate attachment");
		QCOMPARE(cache.value(Delta::MediaIdentity(1, 12)), QByteArray("primary attachment"));
		QCOMPARE(cache.value(Delta::MediaIdentity(2, 12)), QByteArray("alternate attachment"));
		QVERIFY(Delta::MediaIdentity(1, INT32_MAX) != Delta::MediaIdentity(2, 1));
		QVERIFY(Delta::MediaIdentity(INT32_MAX, 1) != Delta::MediaIdentity(1, INT32_MAX));
		QVERIFY(Delta::MediaIdentity(INT32_MAX, INT32_MAX) <= INT64_MAX);
		QCOMPARE(Delta::MediaIdentity(0, 12), std::uint64_t(0));
		QCOMPARE(Delta::MediaIdentity(1, -1), std::uint64_t(0));
	}

	void senderPeerIdentityDoesNotFitCoreIntIds() {
		const auto sender = Delta::kContactUserIdBase + 12;
		QVERIFY(Delta::CoreIdFitsInt(12));
		QVERIFY(Delta::CoreIdFitsInt(std::numeric_limits<std::int32_t>::max()));
		QVERIFY(!Delta::CoreIdFitsInt(0));
		QVERIFY(!Delta::CoreIdFitsInt(
			std::uint64_t(std::numeric_limits<std::int32_t>::max()) + 1));
		QVERIFY(!Delta::CoreIdFitsInt(sender));
		QCOMPARE(static_cast<std::int32_t>(sender), std::int32_t(12));
		QVERIFY(Delta::IsSenderUserId(sender));
		QVERIFY(Delta::IsSenderUserId(Delta::kContactUserIdBase));
		QVERIFY(!Delta::IsSenderUserId(12));
		QVERIFY(!Delta::IsSenderUserId(0));
		QVERIFY(!Delta::IsSenderUserId(Delta::kContactUserIdBase - 1));
		QVERIFY(Delta::CoreChatUserId(12).has_value());
		QCOMPARE(*Delta::CoreChatUserId(12), 12);
		QVERIFY(Delta::CoreChatUserId(
			std::numeric_limits<std::int32_t>::max()).has_value());
		QVERIFY(!Delta::CoreChatUserId(sender).has_value());
		QVERIFY(!Delta::CoreChatUserId(0).has_value());
		QVERIFY(!Delta::CoreChatUserId(
			std::uint64_t(std::numeric_limits<std::int32_t>::max()) + 1)
			.has_value());
	}

	void coreJsonMarksDeviceTalkFromVerifiedFields() {
		QVERIFY(Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"isDeviceTalk"_s, true }}));
		QVERIFY(Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"isDeviceChat"_s, true }}));
		QVERIFY(Delta::CoreJsonMarksDeviceTalk(QJsonObject{
			{ u"isDeviceTalk"_s, false },
			{ u"isDeviceChat"_s, true },
		}));
		QVERIFY(!Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"isDeviceTalk"_s, false }}));
		QVERIFY(!Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"isDeviceChat"_s, false }}));
		QVERIFY(!Delta::CoreJsonMarksDeviceTalk(QJsonObject()));
		QVERIFY(!Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"chatType"_s, u"Single"_s }}));
		QVERIFY(!Delta::CoreJsonMarksDeviceTalk(
			QJsonObject{{ u"isDeviceTalk"_s, 1 }}));
	}

	void coreChatMetadataMatchesIdAndKnownTypes() {
		const auto matching = QJsonObject{
			{ u"id"_s, 12 },
			{ u"chatType"_s, u"Single"_s },
			{ u"isDeviceTalk"_s, true },
		};
		QVERIFY(Delta::CoreJsonIdMatches(matching, 12));
		QVERIFY(Delta::CoreJsonIdMatches(
			QJsonObject{{ u"id"_s, std::numeric_limits<std::int32_t>::max() }},
			std::uint64_t(std::numeric_limits<std::int32_t>::max())));
		QVERIFY(!Delta::CoreJsonIdMatches(matching, 13));
		QVERIFY(!Delta::CoreJsonIdMatches(QJsonObject{{ u"id"_s, 0 }}, 0));
		QVERIFY(!Delta::CoreJsonIdMatches(QJsonObject{{ u"id"_s, -1 }}, 12));
		QVERIFY(!Delta::CoreJsonIdMatches(QJsonObject{{ u"id"_s, 12.5 }}, 12));
		QVERIFY(!Delta::CoreJsonIdMatches(QJsonObject{{ u"id"_s, u"12"_s }}, 12));
		QVERIFY(!Delta::CoreJsonIdMatches(QJsonObject(), 12));
		QVERIFY(!Delta::CoreJsonIdMatches(matching, 0));
		QVERIFY(!Delta::CoreJsonIdMatches(
			matching, Delta::kContactUserIdBase + 12));
		const auto known = [](const QString &name) {
			return Delta::CoreJsonKnownChatType(QJsonValue(name));
		};
		QVERIFY(known(u"Single"_s) == Delta::CoreChatType::Single);
		QVERIFY(known(u"Group"_s) == Delta::CoreChatType::Group);
		QVERIFY(known(u"Mailinglist"_s) == Delta::CoreChatType::Mailinglist);
		QVERIFY(known(u"OutBroadcast"_s) == Delta::CoreChatType::OutBroadcast);
		QVERIFY(known(u"InBroadcast"_s) == Delta::CoreChatType::InBroadcast);
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue()).has_value());
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue(u"single"_s)).has_value());
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue(u"Foo"_s)).has_value());
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue(QString())).has_value());
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue(1)).has_value());
		QVERIFY(!Delta::CoreJsonKnownChatType(QJsonValue(true)).has_value());
		const auto single = Delta::CoreChatMembershipFromJson(matching, 12);
		QVERIFY(single.has_value());
		QVERIFY(single->direct.has_value());
		QVERIFY(single->group.has_value());
		QCOMPARE(*single->direct, true);
		QCOMPARE(*single->group, false);
		QVERIFY(single->deviceTalk);
		const auto group = Delta::CoreChatMembershipFromJson(
			QJsonObject{
				{ u"id"_s, 12 },
				{ u"chatType"_s, u"Group"_s },
				{ u"isDeviceChat"_s, false },
			},
			12);
		QVERIFY(group.has_value());
		QVERIFY(group->direct.has_value());
		QVERIFY(group->group.has_value());
		QCOMPARE(*group->direct, false);
		QCOMPARE(*group->group, true);
		QVERIFY(!group->deviceTalk);
		for (const auto &name : { u"Mailinglist"_s, u"OutBroadcast"_s,
				u"InBroadcast"_s }) {
			const auto other = Delta::CoreChatMembershipFromJson(
				QJsonObject{{ u"id"_s, 12 }, { u"chatType"_s, name }}, 12);
			QVERIFY(other.has_value());
			QVERIFY(other->direct.has_value());
			QVERIFY(other->group.has_value());
			QCOMPARE(*other->direct, false);
			QCOMPARE(*other->group, false);
			QVERIFY(!other->deviceTalk);
		}
		const auto unknown = Delta::CoreChatMembershipFromJson(
			QJsonObject{
				{ u"id"_s, 12 },
				{ u"chatType"_s, u"Foo"_s },
				{ u"isDeviceChat"_s, true },
			},
			12);
		QVERIFY(unknown.has_value());
		QVERIFY(!unknown->direct.has_value());
		QVERIFY(!unknown->group.has_value());
		QVERIFY(unknown->deviceTalk);
		const auto absent = Delta::CoreChatMembershipFromJson(
			QJsonObject{{ u"id"_s, 12 }}, 12);
		QVERIFY(absent.has_value());
		QVERIFY(!absent->direct.has_value());
		QVERIFY(!absent->group.has_value());
		QVERIFY(!absent->deviceTalk);
		QVERIFY(!Delta::CoreChatMembershipFromJson(matching, 13).has_value());
		QVERIFY(!Delta::CoreChatMembershipFromJson(
			QJsonObject{
				{ u"chatType"_s, u"Single"_s },
				{ u"isDeviceTalk"_s, true },
			},
			12).has_value());
	}

	void authorOverrideStaysPerMessageAndDoesNotRenameContact() {
		const auto contact = QJsonObject{
			{ u"displayName"_s, u"GitHub"_s },
			{ u"address"_s, u"notifications@github.com"_s },
		};
		auto max = QJsonObject{
			{ u"fromId"_s, 42 },
			{ u"sender"_s, contact },
			{ u"overrideSenderName"_s, u"Max Mustermann"_s },
			{ u"text"_s, u"hello"_s },
			{ u"subject"_s, u"Re: mailing list"_s },
		};
		auto github = max;
		github.insert(u"overrideSenderName"_s, u"Github"_s);
		const auto contactName = contact.value(u"displayName"_s).toString();
		QCOMPARE(contactName, u"GitHub"_s);
		QCOMPARE(Delta::CoreAuthorDisplayName(
			contactName,
			Delta::CoreOverrideSenderName(max)), u"~Max Mustermann"_s);
		QCOMPARE(Delta::CoreAuthorDisplayName(
			contactName,
			Delta::CoreOverrideSenderName(github)), u"~Github"_s);
		QVERIFY(Delta::CoreAuthorDisplayName(
			contactName,
			Delta::CoreOverrideSenderName(max))
			!= Delta::CoreAuthorDisplayName(
				contactName,
				Delta::CoreOverrideSenderName(github)));
		QCOMPARE(max.value(u"text"_s).toString(), u"hello"_s);
		QCOMPARE(github.value(u"text"_s).toString(), u"hello"_s);
		QCOMPARE(max.value(u"subject"_s).toString(), u"Re: mailing list"_s);
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject()),
			QString());
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, QJsonValue() },
			}),
			QString());
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, QJsonValue(QJsonValue::Null) },
			}),
			QString());
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, 1 },
			}),
			QString());
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, true },
			}),
			QString());
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, QString() },
			}),
			QString());
		QCOMPARE(
			Delta::CoreAuthorDisplayName(contactName, QString()),
			contactName);
		QCOMPARE(
			Delta::CoreAuthorDisplayName(contactName, u"~Max"_s),
			u"~~Max"_s);
		QCOMPARE(
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, u" "_s },
			}),
			u" "_s);
		QCOMPARE(Delta::CoreAuthorDisplayName(contactName, u" "_s), u"~ "_s);
		QVERIFY(Delta::CoreHasOriginalMsgId(
			QJsonObject{{ u"originalMsgId"_s, 7 }}));
		QVERIFY(!Delta::CoreHasOriginalMsgId(
			QJsonObject{{ u"originalMsgId"_s, 0 }}));
		QVERIFY(!Delta::CoreHasOriginalMsgId(
			QJsonObject{{ u"originalMsgId"_s, -1 }}));
		QVERIFY(!Delta::CoreHasOriginalMsgId(
			QJsonObject{{ u"originalMsgId"_s, 7.5 }}));
		QVERIFY(!Delta::CoreHasOriginalMsgId(
			QJsonObject{{ u"originalMsgId"_s, u"7"_s }}));
		QVERIFY(!Delta::CoreHasOriginalMsgId(QJsonObject()));
		QVERIFY(!Delta::CoreHasOriginalMsgId(QJsonObject{
			{ u"originalMsgId"_s, QJsonValue(QJsonValue::Null) },
		}));
		const auto show = [](Delta::CoreAuthorVisibility v) {
			return Delta::CoreShouldShowAuthor(v);
		};
		auto singleOverride = Delta::CoreAuthorVisibility();
		singleOverride.chatType = Delta::CoreChatType::Single;
		singleOverride.hasOverride = true;
		singleOverride.incoming = true;
		QVERIFY(show(singleOverride));
		auto singlePlain = Delta::CoreAuthorVisibility();
		singlePlain.chatType = Delta::CoreChatType::Single;
		singlePlain.incoming = true;
		QVERIFY(!show(singlePlain));
		auto singleOriginal = Delta::CoreAuthorVisibility();
		singleOriginal.chatType = Delta::CoreChatType::Single;
		singleOriginal.originalMsgId = true;
		singleOriginal.incoming = true;
		QVERIFY(show(singleOriginal));
		auto selfTalk = Delta::CoreAuthorVisibility();
		selfTalk.chatType = Delta::CoreChatType::Single;
		selfTalk.selfTalk = true;
		selfTalk.incoming = true;
		QVERIFY(show(selfTalk));
		auto mailing = Delta::CoreAuthorVisibility();
		mailing.chatType = Delta::CoreChatType::Mailinglist;
		mailing.incoming = true;
		QVERIFY(show(mailing));
		auto group = Delta::CoreAuthorVisibility();
		group.chatType = Delta::CoreChatType::Group;
		group.incoming = true;
		QVERIFY(show(group));
		auto broadcast = Delta::CoreAuthorVisibility();
		broadcast.chatType = Delta::CoreChatType::OutBroadcast;
		broadcast.incoming = true;
		QVERIFY(show(broadcast));
		auto unknownOverride = Delta::CoreAuthorVisibility();
		unknownOverride.hasOverride = true;
		unknownOverride.incoming = true;
		QVERIFY(show(unknownOverride));
		auto unknownPlain = Delta::CoreAuthorVisibility();
		unknownPlain.incoming = true;
		QVERIFY(!show(unknownPlain));
		auto info = Delta::CoreAuthorVisibility();
		info.hasOverride = true;
		info.incoming = true;
		info.info = true;
		QVERIFY(!show(info));
		auto outgoing = Delta::CoreAuthorVisibility();
		outgoing.chatType = Delta::CoreChatType::Mailinglist;
		outgoing.hasOverride = true;
		QVERIFY(!show(outgoing));
		const auto quote = QJsonObject{
			{ u"authorDisplayName"_s, u"notifications@github.com"_s },
			{ u"overrideSenderName"_s, u"Github"_s },
			{ u"text"_s, u"quoted body"_s },
			{ u"messageId"_s, 9 },
		};
		QCOMPARE(Delta::CoreOverrideSenderName(quote), u"Github"_s);
		QCOMPARE(Delta::CoreAuthorDisplayName(
			quote.value(u"authorDisplayName"_s).toString(),
			Delta::CoreOverrideSenderName(quote)), u"~Github"_s);
		QCOMPARE(quote.value(u"text"_s).toString(), u"quoted body"_s);
		QCOMPARE(quote.value(u"messageId"_s).toInt(), 9);
		const auto unresolved = QJsonObject{
			{ u"authorDisplayName"_s, u"GitHub"_s },
		};
		QCOMPARE(Delta::CoreAuthorDisplayName(
			unresolved.value(u"authorDisplayName"_s).toString(),
			Delta::CoreOverrideSenderName(unresolved)), u"GitHub"_s);
	}

	void coreAuthorPresentationsAttachAtGroupingGate() {
		const auto contact = u"GitHub"_s;
		auto max = QJsonObject{
			{ u"fromId"_s, 42 },
			{ u"sender"_s, QJsonObject{
				{ u"displayName"_s, contact },
			} },
			{ u"overrideSenderName"_s, u"Max Mustermann"_s },
		};
		auto github = max;
		github.insert(u"overrideSenderName"_s, u"Github"_s);
		auto plain = max;
		plain.remove(u"overrideSenderName"_s);
		const auto sameGithub = github;
		const auto overrideOf = [](const QJsonObject &json) {
			return Delta::CoreOverrideSenderName(json);
		};
		QVERIFY(Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(max), contact, overrideOf(max)));
		QVERIFY(Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(github), contact, overrideOf(sameGithub)));
		QVERIFY(Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(plain), contact, QString()));
		QVERIFY(Delta::CoreAuthorPresentationsAttach(
			contact, QString(), contact, QString()));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(max), contact, overrideOf(github)));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(max), contact, overrideOf(plain)));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(github), contact, overrideOf(plain)));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact, overrideOf(plain), contact, overrideOf(github)));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact, QString(), contact, contact));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			contact,
			Delta::CoreOverrideSenderName(QJsonObject{
				{ u"overrideSenderName"_s, u" "_s },
			}),
			contact,
			QString()));
		QVERIFY(!Delta::CoreAuthorPresentationsAttach(
			u"~Max"_s, QString(), u"~Max"_s, u"Max"_s));
		QCOMPARE(
			Delta::CoreAuthorDisplayName(u"~Max"_s, QString()),
			Delta::CoreAuthorDisplayName(u"~Max"_s, u"Max"_s));
		QCOMPARE(
			Delta::CoreAuthorDisplayName(contact, overrideOf(max)),
			u"~Max Mustermann"_s);
		QCOMPARE(
			Delta::CoreAuthorDisplayName(contact, overrideOf(github)),
			u"~Github"_s);
		QCOMPARE(
			Delta::CoreAuthorDisplayName(contact, overrideOf(plain)),
			contact);
		QCOMPARE(contact, u"GitHub"_s);
	}

	void coreChatCanSendRequiresExactIdAndBoolean_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::addColumn<int>("expected");
		QTest::newRow("true") << QJsonObject{{ u"result"_s, FullChatJson(10, true) }} << 1;
		QTest::newRow("false") << QJsonObject{{ u"result"_s, FullChatJson(10, false, true) }} << 0;
		QTest::newRow("unwrapped") << FullChatJson(10, true) << -1;
		QTest::newRow("malformed-wrapper") << QJsonObject{
			{ u"id"_s, 10 },
			{ u"canSend"_s, true },
			{ u"result"_s, QJsonArray{} },
		} << -1;
		QTest::newRow("result-array") << QJsonObject{{ u"result"_s, QJsonArray{} }} << -1;
		QTest::newRow("missing") << QJsonObject{{ u"result"_s, FullChatJson(10, QJsonValue()) }} << -1;
		QTest::newRow("null") << QJsonObject{{ u"result"_s, FullChatJson(10, QJsonValue(QJsonValue::Null)) }} << -1;
		QTest::newRow("number") << QJsonObject{{ u"result"_s, FullChatJson(10, 1) }} << -1;
		QTest::newRow("string") << QJsonObject{{ u"result"_s, FullChatJson(10, u"true"_s) }} << -1;
		QTest::newRow("error") << QJsonObject{{ u"error"_s, u"offline"_s }, { u"result"_s, FullChatJson(10, true) }} << -1;
		QTest::newRow("id-mismatch") << QJsonObject{{ u"result"_s, FullChatJson(11, true) }} << -1;
		QTest::newRow("string-id") << QJsonObject{{ u"result"_s, QJsonObject{{ u"id"_s, u"10"_s }, { u"canSend"_s, true }} }} << -1;
		QTest::newRow("fraction-id") << QJsonObject{{ u"result"_s, QJsonObject{{ u"id"_s, 10.5 }, { u"canSend"_s, true }} }} << -1;
		QTest::newRow("basic-chat") << QJsonObject{{ u"result"_s, QJsonObject{{ u"id"_s, 10 }, { u"chatType"_s, u"Single"_s }} }} << -1;
	}

	void coreChatCanSendRequiresExactIdAndBoolean() {
		QFETCH(QJsonObject, reply);
		QFETCH(int, expected);
		auto scope = Delta::AccountScope();
		auto completed = 0;
		Delta::ReadCoreChatCanSend([&](QString method, QJsonArray params, DeltaRpc::Callback done) {
			QCOMPARE(method, u"get_full_chat_by_id"_s);
			QCOMPARE(params, (QJsonArray{ 2, 10 }));
			done(reply);
		}, scope, 2, 10, [&](std::optional<bool> canSend) {
			QCOMPARE(canSend ? int(*canSend) : -1, expected);
			QCOMPARE(Delta::CoreSendAllowed(canSend), expected == 1);
			++completed;
		});
		QCOMPARE(completed, 1);
	}

	void coreChatCanSendDoesNotInferFromDeviceOrType() {
		const auto deviceTrue = Delta::CoreFullChatCanSend(FullChatJson(10, true, true), 10);
		QVERIFY(deviceTrue.has_value());
		QVERIFY(*deviceTrue);
		const auto deviceFalse = Delta::CoreFullChatCanSend(FullChatJson(10, false, true), 10);
		QVERIFY(deviceFalse.has_value());
		QVERIFY(!*deviceFalse);
		QVERIFY(Delta::CoreFullChatCanSend(FullChatJson(10, true), 10) == std::optional<bool>(true));
		QVERIFY(!Delta::CoreChatCanSend(FullChatJson(10, true), 10).has_value());
		QVERIFY(!Delta::CoreChatCanSend(QJsonObject{
			{ u"id"_s, 10 },
			{ u"canSend"_s, true },
			{ u"result"_s, QJsonArray{} },
		}, 10).has_value());
		QVERIFY(!Delta::CoreFullChatCanSend(QJsonObject{
			{ u"id"_s, 10 },
			{ u"isDeviceChat"_s, true },
			{ u"chatType"_s, u"Single"_s },
		}, 10).has_value());
		QVERIFY(!Delta::CoreFullChatCanSend(QJsonObject{
			{ u"id"_s, 10 },
			{ u"chatType"_s, u"InBroadcast"_s },
		}, 10).has_value());
		QVERIFY(!Delta::CoreSendAllowed(std::nullopt));
		QVERIFY(!Delta::CoreSendAllowed(false));
		QVERIFY(Delta::CoreSendAllowed(true));
		QCOMPARE(Delta::CoreSendUnavailable(std::nullopt), Delta::kCoreSendUnknown);
		QCOMPARE(Delta::CoreSendUnavailable(std::nullopt, true), Delta::kCoreSendUnknownKeepReply);
		QCOMPARE(Delta::CoreSendUnavailable(false), Delta::kCoreSendDenied);
		QCOMPARE(Delta::CoreSendUnavailable(false, true), Delta::kCoreSendDeniedKeepReply);
		QCOMPARE(
			Delta::CoreSendUnavailable(true, std::optional<bool>(true), false),
			Delta::kCoreSendDenied);
		QCOMPARE(
			Delta::CoreSendUnavailable(true, std::optional<bool>(true), true),
			Delta::kCoreSendDeniedKeepReply);
		QCOMPARE(
			Delta::CoreSendUnavailable(true, std::optional<bool>(false), false),
			Delta::kCoreSendUnknown);
		QCOMPARE(Delta::CoreEventChatId(QJsonObject{{ u"chatId"_s, 10 }}), 10);
		QCOMPARE(Delta::CoreEventChatId(QJsonObject{{ u"chat_id"_s, 11 }}), 11);
		QCOMPARE(Delta::CoreEventChatId(QJsonObject{{ u"chatId"_s, 10 }, { u"chat_id"_s, 11 }}), 10);
		QCOMPARE(Delta::CoreEventChatId(QJsonObject{{ u"chatId"_s, 0 }}), 0);
		QCOMPARE(Delta::CoreEventChatId(QJsonObject{{ u"chat_id"_s, u"10"_s }}), 0);
	}

	void chatSendPermissionsCoalesceDuplicateReads() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		auto last = std::optional<bool>();
		const auto done = [&](std::optional<bool> value) { last = value; ++completed; };
		fixture.permissions->observe(scope, 2, 10, done);
		fixture.permissions->observe(scope, 2, 10, done);
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(fixture.pending.size(), size_t(1));
		QCOMPARE(fixture.pending.front().params, (QJsonArray{ 2, 10 }));
		QVERIFY(fixture.permissions->reading(2, 10));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QCOMPARE(completed, 0);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 2);
		QVERIFY(last == std::optional<bool>(true));
		QVERIFY(fixture.permissions->allowed(2, 10));
		QCOMPARE(fixture.changes, 1);
	}

	void chatSendPermissionsMetadataNotificationRefreshesWithoutPublishingPayload() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QVERIFY(fixture.permissions->known(2, 10) == std::optional<bool>(false));
		const auto untracked = FullChatJson(10, true);
		QVERIFY(Delta::CoreFullChatCanSend(untracked, 10) == std::optional<bool>(true));
		fixture.methods.clear();
		fixture.permissions->refreshObserved(scope, 2, 10, 10);
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.permissions->known(2, 10) == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QVERIFY(fixture.permissions->known(2, 10) == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsUntrackedMetadataCannotBeatInFlightObserver() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QCOMPARE(fixture.pending.size(), size_t(1));
		fixture.permissions->refresh(scope, 2, 10);
		const auto untracked = FullChatJson(10, true);
		QVERIFY(Delta::CoreFullChatCanSend(untracked, 10) == std::optional<bool>(true));
		fixture.permissions->refreshObserved(scope, 2, 10, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(!fixture.permissions->known(2, 10).has_value());
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.reply(untracked));
		QCOMPARE(completed, 0);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QCOMPARE(completed, 1);
		QVERIFY(last == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsOldReadDoesNotCompleteNewWaiter() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		fixture.permissions->forget(2, 10);
		auto completed = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QCOMPARE(fixture.pending.size(), size_t(2));
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 0);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.permissions->reading(2, 10));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QCOMPARE(completed, 1);
		QVERIFY(last == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsMalformedEnvelopeDoesNotAuthorize() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			QVERIFY(!value.has_value());
			++completed;
		});
		QVERIFY(fixture.replyEnvelope({
			{ u"id"_s, 10 },
			{ u"canSend"_s, true },
			{ u"result"_s, QJsonArray{} },
		}));
		QCOMPARE(completed, 1);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(!Delta::CoreSendAllowed(Delta::CoreChatCanSend({
			{ u"id"_s, 10 },
			{ u"canSend"_s, true },
			{ u"result"_s, QJsonArray{} },
		}, 10)));
	}

	void chatSendPermissionsRefreshWhileReadingDoesNotPublishStaleAllow() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QCOMPARE(fixture.pending.size(), size_t(1));
		fixture.permissions->refresh(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 0);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QCOMPARE(completed, 1);
		QVERIFY(last == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsResetRejectsOldReadAfterReobserve() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		fixture.permissions->reset();
		auto completed = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 0);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QCOMPARE(completed, 1);
		QVERIFY(last == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsRefreshWatchedSkipsIdleChats() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		fixture.permissions->observe(scope, 2, 11);
		fixture.permissions->observe(scope, 2, 12);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QVERIFY(fixture.reply(FullChatJson(11, true)));
		QVERIFY(fixture.reply(FullChatJson(12, true)));
		fixture.methods.clear();
		fixture.pending.clear();
		fixture.permissions->refreshWatched(scope, 2, 10);
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(fixture.pending.size(), size_t(1));
		QCOMPARE(fixture.pending.front().params, (QJsonArray{ 2, 10 }));
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		auto waiting = 0;
		fixture.permissions->observe(scope, 2, 11, [&](std::optional<bool>) { ++waiting; });
		QCOMPARE(fixture.pending.size(), size_t(1));
		QCOMPARE(fixture.pending.front().params, (QJsonArray{ 2, 11 }));
		fixture.methods.clear();
		fixture.permissions->refreshWatched(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(2));
		QCOMPARE(fixture.pending[0].params, (QJsonArray{ 2, 11 }));
		QCOMPARE(fixture.pending[1].params, (QJsonArray{ 2, 10 }));
		QCOMPARE(waiting, 0);
		QVERIFY(fixture.permissions->allowed(2, 12));
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
	}

	void chatSendPermissionsEnsureDoesNotRetryFailedRead() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->ensure(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(u"offline"_s, true));
		QVERIFY(fixture.permissions->watching(2, 10));
		QVERIFY(!fixture.permissions->known(2, 10).has_value());
		fixture.permissions->ensure(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(0));
		fixture.permissions->observe(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		fixture.permissions->observe(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
	}

	void chatSendPermissionsWatchRefreshesFailedAndDenied() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->watch(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(u"offline"_s, true));
		QVERIFY(!fixture.permissions->known(2, 10).has_value());
		fixture.permissions->ensure(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(0));
		fixture.permissions->watch(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(FullChatJson(10, false)));
		QVERIFY(fixture.permissions->known(2, 10) == std::optional<bool>(false));
		fixture.permissions->ensure(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(0));
		fixture.permissions->watch(scope, 2, 10);
		QCOMPARE(fixture.pending.size(), size_t(1));
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QVERIFY(fixture.permissions->allowed(2, 10));
	}

	void chatSendPermissionsRefreshObservedSkipsIdleUnfocused() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		fixture.permissions->observe(scope, 2, 11);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QVERIFY(fixture.reply(FullChatJson(11, true)));
		fixture.methods.clear();
		fixture.pending.clear();
		fixture.permissions->refreshObserved(scope, 2, 11, 10);
		QCOMPARE(fixture.pending.size(), size_t(0));
		QVERIFY(fixture.permissions->allowed(2, 11));
		fixture.permissions->refreshObserved(scope, 2, 99, 10);
		QVERIFY(!fixture.permissions->watching(2, 99));
		QCOMPARE(fixture.pending.size(), size_t(0));
		fixture.permissions->refreshObserved(scope, 2, 10, 10);
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(fixture.pending.front().params, (QJsonArray{ 2, 10 }));
		auto waiting = 0;
		fixture.permissions->observe(scope, 2, 11, [&](std::optional<bool>) { ++waiting; });
		QCOMPARE(fixture.pending.size(), size_t(2));
		fixture.methods.clear();
		fixture.permissions->refreshObserved(scope, 2, 11, 10);
		QCOMPARE(fixture.pending.size(), size_t(2));
		QCOMPARE(waiting, 0);
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.permissions->allowed(2, 11));
	}

	void chatSendPermissionsReentrantResetForgetAndReobserve() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto hits = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++hits;
			QVERIFY(value == std::optional<bool>(true));
			fixture.permissions->forget(2, 10);
			QVERIFY(!fixture.permissions->watching(2, 10));
			fixture.permissions->reset();
			fixture.permissions->observe(scope, 2, 11, [&](std::optional<bool> next) {
				last = next;
				++hits;
			});
		});
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(hits, 1);
		QVERIFY(last == std::optional<bool>(true));
		QVERIFY(!fixture.permissions->watching(2, 10));
		QVERIFY(fixture.permissions->watching(2, 11));
		QVERIFY(fixture.permissions->reading(2, 11));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(!fixture.permissions->allowed(2, 11));
		QVERIFY(fixture.reply(FullChatJson(11, false)));
		QCOMPARE(hits, 2);
		QVERIFY(last == std::optional<bool>(false));
		QVERIFY(!fixture.permissions->allowed(2, 11));
	}

	void chatSendPermissionsChangedCallbackMayResetAndReobserve() {
		std::unique_ptr<Delta::ChatSendPermissions> permissions;
		std::vector<PermissionFixture::Pending> pending;
		auto next = Delta::AccountScope();
		auto changes = 0;
		auto hits = 0;
		permissions = std::make_unique<Delta::ChatSendPermissions>(
			[&](QString, QJsonArray params, DeltaRpc::Callback done) {
				pending.push_back({ u"get_full_chat_by_id"_s, std::move(params), std::move(done) });
			},
			[&] {
				++changes;
				if (changes != 1) return;
				permissions->reset();
				permissions->observe(next, 2, 12);
			});
		auto scope = Delta::AccountScope();
		permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			++hits;
			QVERIFY(value == std::optional<bool>(true));
			QVERIFY(!permissions->watching(2, 10));
			QVERIFY(permissions->watching(2, 12));
			QVERIFY(permissions->reading(2, 12));
		});
		QCOMPARE(pending.size(), size_t(1));
		auto first = std::move(pending.front().done);
		pending.erase(pending.begin());
		first({{ u"result"_s, FullChatJson(10, true) }});
		QCOMPARE(hits, 1);
		QCOMPARE(changes, 1);
		QCOMPARE(pending.size(), size_t(1));
		QCOMPARE(pending.front().params, (QJsonArray{ 2, 12 }));
		QVERIFY(!permissions->allowed(2, 10));
		auto second = std::move(pending.front().done);
		pending.erase(pending.begin());
		second({{ u"result"_s, FullChatJson(12, false) }});
		QVERIFY(permissions->known(2, 12) == std::optional<bool>(false));
		QVERIFY(!permissions->allowed(2, 12));
	}

	void chatSendPermissionsAllowedCallbackFiresOnce() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto sends = 0;
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> canSend) {
			if (fixture.permissions->allowed(2, 10) && Delta::CoreSendAllowed(canSend)) ++sends;
		});
		QCOMPARE(sends, 0);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(sends, 1);
		fixture.permissions->observe(scope, 2, 10);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(sends, 1);
	}

	void chatSendPermissionsMalformedAndErrorDoNotAuthorize() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto sends = 0;
		const auto sendIfAllowed = [&](std::optional<bool>) {
			if (fixture.permissions->allowed(2, 10)) ++sends;
		};
		fixture.permissions->observe(scope, 2, 10, sendIfAllowed);
		QVERIFY(fixture.reply(QJsonObject{{ u"id"_s, 10 }}));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		fixture.permissions->observe(scope, 2, 10, sendIfAllowed);
		QVERIFY(fixture.reply(FullChatJson(11, true)));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		fixture.permissions->observe(scope, 2, 10, sendIfAllowed);
		QVERIFY(fixture.reply(u"offline"_s, true));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		const auto untracked = QJsonObject{
			{ u"id"_s, 10 },
			{ u"chatType"_s, u"Single"_s },
			{ u"canSend"_s, true },
		};
		QVERIFY(Delta::CoreFullChatCanSend(untracked, 10) == std::optional<bool>(true));
		fixture.permissions->refreshObserved(scope, 2, 10, 10);
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.reply(QJsonObject{
			{ u"id"_s, 10 },
			{ u"chatType"_s, u"Single"_s },
		}));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QCOMPARE(sends, 0);
		Delta::ReadCoreChatCanSend([&](QString, QJsonArray, DeltaRpc::Callback done) {
			done({{ u"result"_s, FullChatJson(10, true) }});
		}, scope, 0, 10, [&](std::optional<bool> canSend) { QVERIFY(!canSend); });
		Delta::ReadCoreChatCanSend([&](QString, QJsonArray, DeltaRpc::Callback done) {
			done({{ u"result"_s, FullChatJson(10, true) }});
		}, scope, 2, 0, [&](std::optional<bool> canSend) { QVERIFY(!canSend); });
		QCOMPARE(sends, 0);
	}

	void chatSendPermissionsStaleAccountIgnoresResult() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		auto last = std::optional<bool>(true);
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QVERIFY(fixture.permissions->reading(2, 10));
		scope.invalidate();
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 1);
		QVERIFY(!last.has_value());
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(!fixture.permissions->known(2, 10).has_value());
		QCOMPARE(fixture.changes, 0);
		auto next = Delta::AccountScope();
		fixture.permissions->observe(next, 2, 10, [&](std::optional<bool> value) {
			last = value;
			++completed;
		});
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QCOMPARE(completed, 2);
		QVERIFY(last == std::optional<bool>(true));
		QVERIFY(fixture.permissions->allowed(2, 10));
		auto late = 0;
		fixture.permissions->observe(next, 2, 11, [&](std::optional<bool>) { ++late; });
		fixture.permissions->reset();
		QCOMPARE(late, 1);
		QVERIFY(!fixture.permissions->watching(2, 10));
		QVERIFY(!fixture.permissions->watching(2, 11));
		QVERIFY(!fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.reply(FullChatJson(11, true)));
		QVERIFY(!fixture.permissions->allowed(2, 11));
	}

	void chatSendPermissionsParsesContactRequestIndependently() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto completed = 0;
		fixture.permissions->observe(scope, 2, 10, [&](std::optional<bool> canSend) {
			QVERIFY(canSend == std::optional<bool>(true));
			++completed;
		});
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
		QVERIFY(fixture.reply(RequestFullChatJson(10, true, true)));
		QCOMPARE(completed, 1);
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(!fixture.permissions->sendable(2, 10));
		QVERIFY(fixture.permissions->contactRequest(2, 10) == std::optional<bool>(true));
		QVERIFY(fixture.permissions->knownType(2, 10) == std::optional<Delta::CoreChatType>(Delta::CoreChatType::Single));
		QCOMPARE(fixture.changes, 1);
		QCOMPARE(fixture.methods.size(), size_t(1));
	}

	void chatSendPermissionsMissingRequestFlagDoesNotOverlay() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		QVERIFY(fixture.reply(FullChatJson(10, true)));
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.permissions->sendable(2, 10));
		QVERIFY(!fixture.permissions->contactRequest(2, 10).has_value());
		QVERIFY(!fixture.permissions->knownType(2, 10).has_value());
		QCOMPARE(fixture.methods, QStringList{ u"get_full_chat_by_id"_s });
	}

	void chatSendPermissionsMalformedRequestLeavesSendUnchanged() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		auto json = FullChatJson(10, true);
		json.insert(u"isContactRequest"_s, 1);
		fixture.permissions->observe(scope, 2, 10);
		QVERIFY(fixture.reply(json));
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.permissions->sendable(2, 10));
		QVERIFY(!fixture.permissions->contactRequest(2, 10).has_value());
		QCOMPARE(fixture.methods.size(), size_t(1));
	}

	void chatSendPermissionsClearedRequestBecomesSendable() {
		auto fixture = PermissionFixture();
		auto scope = Delta::AccountScope();
		fixture.permissions->observe(scope, 2, 10);
		QVERIFY(fixture.reply(RequestFullChatJson(10, true, true)));
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(!fixture.permissions->sendable(2, 10));
		fixture.permissions->refresh(scope, 2, 10);
		QVERIFY(fixture.reply(RequestFullChatJson(10, true, false)));
		QVERIFY(fixture.permissions->allowed(2, 10));
		QVERIFY(fixture.permissions->sendable(2, 10));
		QVERIFY(fixture.permissions->contactRequest(2, 10) == std::optional<bool>(false));
		QCOMPARE(fixture.changes, 2);
		QCOMPARE(fixture.methods.size(), size_t(2));
	}

	void realCoreHistoryDeletion() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, temporary.filePath(u"accounts"_s));
		const auto account = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		QVERIFY(account > 0);
		const auto message = CoreCall(*rpc, u"add_device_message"_s,
			{account, u"history-test"_s, QJsonObject{{u"text"_s, u"Delete only this fixture"_s}}})[u"result"_s].toInt();
		QVERIFY(message > 0);
		const auto original = CoreCall(*rpc, u"get_message"_s, {account, message})
			[u"result"_s].toObject();
		const auto chat = original[u"chatId"_s].toInt();
		const auto timestamp = original[u"timestamp"_s].toInteger();
		QVERIFY(chat > 0);
		Delta::AccountScope scope;
		const auto selected = CoreCall(*rpc, u"add_device_message"_s,
			{account, u"selected-delete"_s, QJsonObject{{u"text"_s, u"Selected deletion fixture"_s}}})[u"result"_s].toInt();
		QVERIFY(selected > 0);
		auto lookedUp = 0;
		Delta::ReadVisibleMessage(rpc, scope, account, chat, selected,
			[&](QJsonObject result) {
				QCOMPARE(result[u"id"_s].toInt(), selected);
				QCOMPARE(result[u"text"_s].toString(), u"Selected deletion fixture"_s);
				++lookedUp;
			});
		QTRY_COMPARE(lookedUp, 1);
		auto deleted = false;
		Delta::DeleteCoreMessages(rpc, scope, account, QJsonArray{selected}, false,
			[&](QString error) {
				QVERIFY(error.isEmpty());
				deleted = true;
			});
		QTRY_VERIFY(deleted);
		Delta::ReadVisibleMessage(rpc, scope, account, chat, selected,
			[&](QJsonObject result) {
				QVERIFY(result.isEmpty());
				++lookedUp;
			});
		QTRY_COMPARE(lookedUp, 2);
		const auto remaining = CoreCall(*rpc, u"get_message_ids"_s,
			{account, chat, false, false})[u"result"_s].toArray();
		QVERIFY(!remaining.contains(selected));
		QVERIFY(remaining.contains(message));
		auto cleared = false;
		Delta::DeleteChatHistory(rpc, scope, account, chat, false, false,
			[&](Delta::HistoryDeletionResult result) {
				QVERIFY(result.error.isEmpty());
				QVERIFY(result.deletedIds.contains(message));
				QVERIFY(!result.chatDeleted);
				cleared = true;
			}, Delta::HistoryDateRange{timestamp - 1, timestamp + 1});
		QTRY_VERIFY(cleared);
		const auto messages = CoreCall(*rpc, u"get_message_ids"_s, {account, chat, false, false});
		QVERIFY(!messages.contains(u"error"_s));
		QVERIFY(!messages[u"result"_s].toArray().contains(message));
		cleared = false;
		Delta::DeleteChatHistory(rpc, scope, account, chat, false, false,
			[&](Delta::HistoryDeletionResult result) {
				QVERIFY(result.error.isEmpty());
				cleared = true;
			});
		QTRY_VERIFY(cleared);
		QVERIFY(CoreCall(*rpc, u"get_message_ids"_s, {account, chat, false, false})
			[u"result"_s].toArray().empty());
		const auto group = CoreCall(*rpc, u"create_group_chat"_s,
			{account, u"Disposable history test"_s, false})[u"result"_s].toInt();
		QVERIFY(group > 0);
		auto removed = false;
		Delta::DeleteChatHistory(rpc, scope, account, group, true, false,
			[&](Delta::HistoryDeletionResult result) {
				QVERIFY(result.error.isEmpty());
				QVERIFY(result.chatDeleted);
				removed = true;
			});
		QTRY_VERIFY(removed);
		QVERIFY(CoreCall(*rpc, u"get_full_chat_by_id"_s, {account, group}).contains(u"error"_s));
		rpc->stop();
	}

	void historyDateDeletionUsesTimestampsAndBatches() {
		for (const auto mode : {u"normal"_s, u"history-large"_s, u"history-bad-message"_s}) {
			auto rpc = std::make_shared<DeltaRpc>();
			rpc->start(u"" RPC_FIXTURE_PATH ""_s, mode);
			Delta::AccountScope scope;
			auto done = false;
			const auto large = mode == u"history-large"_s;
			Delta::DeleteChatHistory(rpc, scope, 1, 10, false, false,
				[&](Delta::HistoryDeletionResult result) {
					if (mode == u"history-bad-message"_s) {
						QVERIFY(!result.error.isEmpty());
						QVERIFY(result.deletedIds.empty());
					} else {
						QVERIFY(result.error.isEmpty());
						QCOMPARE(result.deletedIds, large ? QVector<int>({101, 102, 103, 104}) : QVector<int>({12}));
					}
					QVERIFY(!result.chatDeleted);
					done = true;
				}, large ? Delta::HistoryDateRange{100, 105} : Delta::HistoryDateRange{150, 250});
			QTRY_VERIFY(done);
			const auto state = CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject();
			if (mode == u"history-bad-message"_s) {
				QVERIFY(!state[u"calls"_s].toArray().contains(u"delete_messages"_s));
			} else if (large) {
				auto batches = 0;
				for (const auto &call : state[u"calls"_s].toArray()) {
					if (call == u"get_messages"_s) ++batches;
				}
				QCOMPARE(batches, 3);
			}
			rpc->stop();
		}
	}

	void clearsOnlyConfirmedSnapshot() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"history-new-message"_s);
		Delta::AccountScope scope;
		auto done = false;
		Delta::DeleteChatHistory(rpc, scope, 1, 10, false, false,
			[&](Delta::HistoryDeletionResult result) {
				QVERIFY(result.error.isEmpty());
				QVERIFY(!result.chatDeleted);
				QCOMPARE(result.deletedIds, QVector<int>({10, 80, 12}));
				done = true;
			});
		QTRY_VERIFY(done);
		const auto state = CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject();
		QCOMPARE(state[u"ids"_s].toArray(), QJsonArray{999});
		QCOMPARE(state[u"calls"_s].toArray(), (QJsonArray{u"get_message_ids"_s, u"delete_messages"_s}));
		rpc->stop();
	}

	void rejectedHistoryDeletionStopsBeforeRemovingChat() {
		for (const auto mode : {u"history-reject-list"_s, u"history-reject-delete"_s}) {
			auto rpc = std::make_shared<DeltaRpc>();
			rpc->start(u"" RPC_FIXTURE_PATH ""_s, mode);
			Delta::AccountScope scope;
			auto done = false;
			Delta::DeleteChatHistory(rpc, scope, 1, 10, true, true,
				[&](Delta::HistoryDeletionResult result) {
					QCOMPARE(result.error, u"deletion rejected"_s);
					QVERIFY(result.deletedIds.empty());
					QVERIFY(!result.chatDeleted);
					done = true;
				});
			QTRY_VERIFY(done);
			const auto state = CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject();
			QCOMPARE(state[u"ids"_s].toArray(), (QJsonArray{10, 80, 12}));
			QVERIFY(!state[u"calls"_s].toArray().contains(u"delete_chat"_s));
			rpc->stop();
		}
	}

	void reportsPartialHistoryDeletion() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"history-reject-chat"_s);
		Delta::AccountScope scope;
		auto done = false;
		Delta::DeleteChatHistory(rpc, scope, 1, 10, true, true,
			[&](Delta::HistoryDeletionResult result) {
				QCOMPARE(result.error, u"deletion rejected"_s);
				QVERIFY(!result.chatDeleted);
				QCOMPARE(result.deletedIds, QVector<int>({10, 80, 12}));
				done = true;
			});
		QTRY_VERIFY(done);
		rpc->stop();
	}

	void deletesChatWithoutRevokingMessages() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		Delta::AccountScope scope;
		auto done = false;
		Delta::DeleteChatHistory(rpc, scope, 1, 10, true, false,
			[&](Delta::HistoryDeletionResult result) {
				QVERIFY(result.error.isEmpty());
				QVERIFY(result.chatDeleted);
				done = true;
			});
		QTRY_VERIFY(done);
		const auto state = CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject();
		QCOMPARE(state[u"calls"_s].toArray(), (QJsonArray{u"delete_chat"_s}));
		rpc->stop();
	}

	void accountSwitchCancelsRemainingDeletionSteps() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"history-hold-delete"_s);
		Delta::AccountScope scope;
		auto done = false;
		Delta::DeleteChatHistory(rpc, scope, 1, 10, true, true,
			[&](Delta::HistoryDeletionResult) { done = true; });
		QTRY_VERIFY(CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject()
			[u"calls"_s].toArray().contains(u"delete_messages_for_all"_s));
		scope.invalidate();
		CoreCall(*rpc, u"release"_s, {});
		const auto state = CoreCall(*rpc, u"history_state"_s, {})[u"result"_s].toObject();
		QVERIFY(!state[u"calls"_s].toArray().contains(u"delete_chat"_s));
		QVERIFY(!done);
		rpc->stop();
	}

	void realCoreDraftRoundTrip() {
		const auto program = qEnvironmentVariable("DELTA_TEST_CORE_RPC");
		if (program.isEmpty()) QSKIP("Set DELTA_TEST_CORE_RPC to test the real Core server.");
		QTemporaryDir temporary;
		QVERIFY(temporary.isValid());
		auto attachment = QFile(temporary.filePath(u"attachment.txt"_s));
		QVERIFY(attachment.open(QIODevice::WriteOnly));
		attachment.write("draft attachment integrity\n");
		attachment.close();
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(program, temporary.filePath(u"accounts"_s));
		const auto first = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		const auto second = CoreCall(*rpc, u"add_account"_s, {})[u"result"_s].toInt();
		QVERIFY(first > 0 && second > 0 && first != second);
		auto inventory = std::optional<Delta::AccountInventory>();
		Delta::ReadAccountInventory([=](QString method, QJsonArray params, DeltaRpc::Callback done) {
			rpc->call(method, params, std::move(done));
		}, [&](Delta::AccountInventory result) { inventory = std::move(result); });
		QTRY_VERIFY(inventory.has_value());
		QVERIFY(inventory->error.isEmpty());
		QVERIFY(inventory->configured.empty());
		QCOMPARE(inventory->unconfigured, first);
		const auto chatA = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {first, 1})[u"result"_s].toInt();
		const auto chatB = CoreCall(*rpc, u"create_chat_by_contact_id"_s, {second, 1})[u"result"_s].toInt();
		QVERIFY(chatA > 0);
		QCOMPARE(chatA, chatB);
		QVERIFY(!CoreCall(*rpc, u"misc_set_draft"_s,
			{first, chatA, u"first original"_s, attachment.fileName(), u"attachment.txt"_s, QJsonValue(), u"File"_s}).contains(u"error"_s));
		QVERIFY(!CoreCall(*rpc, u"misc_set_draft"_s,
			{second, chatB, u"second original"_s, QJsonValue(), QJsonValue(), QJsonValue(), QJsonValue()}).contains(u"error"_s));
		const auto items = CoreCall(*rpc, u"get_chatlist_items_by_entries"_s,
			{first, QJsonArray{chatA}})[u"result"_s].toObject();
		const auto preview = items[QString::number(chatA)].toObject()[u"lastMessageId"_s].toInt();
		QVERIFY(preview > 0);
		QCOMPARE(CoreCall(*rpc, u"get_message"_s, {first, preview})
			[u"result"_s].toObject()[u"state"_s].toInt(), 19);
		const auto history = CoreCall(*rpc, u"get_message_ids"_s, {first, chatA, false, false})[u"result"_s].toArray();
		QVERIFY(!history.contains(preview));
		Delta::AccountScope visibilityScope;
		auto checked = false;
		Delta::CheckMessageVisibility(rpc, visibilityScope, first, chatA, preview,
			[&](bool visible) {
				QVERIFY(!visible);
				checked = true;
			});
		QTRY_VERIFY(checked);
		auto errors = 0;
		Delta::DraftStore drafts(rpc, [&](QString) { ++errors; });
		auto loaded = 0;
		drafts.load(first, chatA, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(draft.text, u"first original"_s);
			QVERIFY(QFileInfo::exists(draft.file));
			++loaded;
		});
		drafts.load(second, chatB, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty());
			QCOMPARE(draft.text, u"second original"_s);
			++loaded;
		});
		QTRY_COMPARE(loaded, 2);
		drafts.save(first, chatA, u"changed in first"_s, 0);
		auto saved = QJsonObject();
		QTRY_VERIFY(([&] {
			saved = CoreCall(*rpc, u"get_draft"_s, {first, chatA})[u"result"_s].toObject();
			return saved[u"text"_s].toString() == u"changed in first"_s;
		})());
		QCOMPARE(saved[u"fileName"_s].toString(), u"attachment.txt"_s);
		auto persisted = QFile(saved[u"file"_s].toString());
		QVERIFY(persisted.open(QIODevice::ReadOnly));
		QCOMPARE(persisted.readAll(), QByteArray("draft attachment integrity\n"));
		QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {second, chatB})[u"result"_s].toObject()[u"text"_s].toString(), u"second original"_s);
		drafts.recoverText(first, chatA, u"rejected after switching"_s, 0);
		QTRY_VERIFY(([&] {
			saved = CoreCall(*rpc, u"get_draft"_s, {first, chatA})[u"result"_s].toObject();
			return saved[u"text"_s].toString()
				== u"rejected after switching\n\nchanged in first"_s;
		})());
		QCOMPARE(saved[u"fileName"_s].toString(), u"attachment.txt"_s);
		QCOMPARE(CoreCall(*rpc, u"get_draft"_s, {second, chatB})[u"result"_s].toObject()[u"text"_s].toString(), u"second original"_s);
		drafts.save(second, chatB, {}, 0);
		QTRY_VERIFY(CoreCall(*rpc, u"get_draft"_s, {second, chatB})[u"result"_s].isNull());
		QCOMPARE(errors, 0);
		rpc->stop();
	}

	void deliversEventsAndStopsPendingRequests() {
		DeltaRpc rpc;
		auto events = 0;
		auto failed = 0;
		rpc.setEventHandler([&](QJsonObject event) {
			QCOMPARE(event[u"method"_s].toString(), u"fixture_event"_s);
			++events;
		});
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"get_next_event"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failed;
		});
		rpc.call(u"event"_s, {}, nullptr);
		QTRY_COMPARE(events, 1);
		rpc.stop();
		QCOMPARE(failed, 1);
		QCOMPARE(events, 1);
	}

	void clearsTextDraftInCore() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"plain"_s);
		Delta::DraftStore drafts(rpc, [](QString) { QFAIL("Unexpected draft failure"); });
		auto loaded = false;
		drafts.load(1, 10, [&](Delta::ComposeDraft, QString) { loaded = true; });
		QTRY_VERIFY(loaded);
		drafts.save(1, 10, {}, 0);
		auto checked = false;
		rpc->call(u"inspect_draft"_s, {1, 10}, [&](QJsonObject reply) {
			QVERIFY(reply[u"result"_s].toObject().isEmpty());
			checked = true;
		});
		QTRY_VERIFY(checked);
		rpc->stop();
	}

	void historyUsesCoreOrder() {
		const auto ids = std::vector<int>{10, 80, 12, 65, 20};
		QCOMPARE(*Delta::HistoryPage(ids, 0, 0, 2), (std::vector<int>{20, 65}));
		QCOMPARE(*Delta::HistoryPage(ids, 65, 0, 2), (std::vector<int>{12, 80}));
		QCOMPARE(*Delta::HistoryPage(ids, 80, -3, 2), (std::vector<int>{65, 12}));
		QCOMPARE(*Delta::HistoryPage(ids, 20, -3, 2), (std::vector<int>{}));
		QCOMPARE(*Delta::HistoryPage(ids, 10, 0, 2), (std::vector<int>{}));
		QVERIFY(!Delta::HistoryPage(ids, 999, 0, 2));
	}

	void startupFailureCompletesPendingRequests() {
		DeltaRpc rpc;
		auto failed = false;
		auto events = 0;
		rpc.setEventHandler([&](QJsonObject) { ++events; });
		rpc.start(u"/nonexistent/delta-rpc-fixture"_s, u"normal"_s);
		rpc.call(u"echo"_s, {}, [&](QJsonObject reply) { failed = reply.contains(u"error"_s); });
		QTRY_VERIFY(failed);
		QCOMPARE(events, 1);
		QVERIFY(!rpc.running());
	}

	void incompleteEnvelopeFailsClosed() {
		DeltaRpc rpc;
		auto failed = false;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"incomplete"_s, {}, [&](QJsonObject reply) { failed = reply.contains(u"error"_s); });
		QTRY_VERIFY(failed);
		QVERIFY(!rpc.running());
		rpc.stop();
	}

	void routesOutOfOrderReplies() {
		DeltaRpc rpc;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		auto completed = QStringList();
		rpc.call(u"hold"_s, {}, [&](QJsonObject) { completed.push_back(u"first"_s); });
		rpc.call(u"echo"_s, {}, [&](QJsonObject) { completed.push_back(u"second"_s); });
		QTRY_COMPARE(completed, QStringList{u"second"_s});
		rpc.call(u"release"_s, {}, nullptr);
		QTRY_COMPARE(completed.size(), 2);
		QCOMPARE(completed.back(), u"first"_s);
		rpc.stop();
	}

	void accountSwitchDropsStaleReplies() {
		DeltaRpc rpc;
		Delta::AccountScope account;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		auto stale = false;
		auto current = false;
		const auto preparedInOldProfile = account.guard();
		QVERIFY(preparedInOldProfile());
		rpc.call(u"hold"_s, {}, account.bind([&](QJsonObject) { stale = true; }));
		account.invalidate();
		QVERIFY(!preparedInOldProfile());
		QVERIFY(account.guard()());
		// A switch away and back creates another scope, never reviving old work.
		account.invalidate();
		QVERIFY(!preparedInOldProfile());
		rpc.call(u"release"_s, {}, account.bind([&](QJsonObject) { current = true; }));
		QTRY_VERIFY(current);
		QVERIFY(!stale);
		auto preparationGuard = std::function<bool()>();
		{
			Delta::AccountScope temporary;
			preparationGuard = temporary.guard();
			QVERIFY(preparationGuard());
		}
		QVERIFY(!preparationGuard());
		rpc.stop();
	}

	void crashFailsPendingOnce() {
		DeltaRpc rpc;
		auto failures = 0;
		auto events = 0;
		rpc.setEventHandler([&](QJsonObject reply) { if (reply.contains(u"error"_s)) ++events; });
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"get_next_event"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failures;
		});
		rpc.call(u"crash"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failures;
		});
		QTRY_COMPARE(failures, 2);
		QCOMPARE(events, 1);
		QVERIFY(!rpc.running());
		rpc.stop();
		QCOMPARE(failures, 2);
	}

	void malformedResponseFailsClosed() {
		DeltaRpc rpc;
		auto failures = 0;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"malformed"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failures;
		});
		QTRY_COMPARE(failures, 1);
		QVERIFY(!rpc.running());
		rpc.call(u"echo"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failures;
		});
		QCOMPARE(failures, 2);
		rpc.stop();
	}

	void timeoutDoesNotRetryMutation() {
		DeltaRpc rpc;
		auto failures = 0;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"hang"_s, {}, [&](QJsonObject reply) {
			if (reply.contains(u"error"_s)) ++failures;
		}, 100);
		QTRY_COMPARE(failures, 1);
		QVERIFY(!rpc.running());
		QTest::qWait(150);
		QCOMPARE(failures, 1);
		rpc.stop();
	}

	void restartDiscardsPartialFrame() {
		DeltaRpc rpc;
		auto failed = false;
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		rpc.call(u"partial"_s, {}, [&](QJsonObject reply) { failed = reply.contains(u"error"_s); });
		QTRY_VERIFY(failed);
		rpc.start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		auto ok = false;
		rpc.call(u"echo"_s, {42}, [&](QJsonObject reply) { ok = reply[u"result"_s].toArray() == QJsonArray{42}; });
		QTRY_VERIFY(ok);
		rpc.stop();
	}

	void draftsAreAccountScopedAndPreserveAttachments() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"normal"_s);
		auto errors = 0;
		Delta::DraftStore drafts(rpc, [&](QString) { ++errors; });
		auto loaded = 0;
		drafts.load(1, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty()); QCOMPARE(draft.text, u"account-1"_s); ++loaded;
		});
		drafts.load(2, 10, [&](Delta::ComposeDraft draft, QString error) {
			QVERIFY(error.isEmpty()); QCOMPARE(draft.text, u"account-2"_s); ++loaded;
		});
		QTRY_COMPARE(loaded, 2);
		drafts.save(1, 10, u"private draft"_s, 19);
		auto checked = false;
		rpc->call(u"inspect_draft"_s, {1, 10}, [&](QJsonObject reply) {
			const auto draft = reply[u"result"_s].toObject();
			QCOMPARE(draft[u"text"_s].toString(), u"private draft"_s);
			QCOMPARE(draft[u"file"_s].toString(), u"/fixture/attachment.bin"_s);
			QCOMPARE(draft[u"quote"_s].toObject()[u"messageId"_s].toInt(), 19);
			checked = true;
		});
		QTRY_VERIFY(checked);
		checked = false;
		rpc->call(u"inspect_draft"_s, {2, 10}, [&](QJsonObject reply) {
			QCOMPARE(reply[u"result"_s].toObject()[u"text"_s].toString(), u"account-2"_s);
			checked = true;
		});
		QTRY_VERIFY(checked);
		QCOMPARE(errors, 0);
		rpc->stop();
	}

	void serializesDraftWrites() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"hold-first-save"_s);
		Delta::DraftStore drafts(rpc, [](QString) {});
		auto loaded = false;
		drafts.load(1, 10, [&](Delta::ComposeDraft, QString) { loaded = true; });
		QTRY_VERIFY(loaded);
		drafts.save(1, 10, u"first"_s, 0);
		drafts.save(1, 10, u"second"_s, 0);
		drafts.save(1, 10, u"latest"_s, 0);
		auto writes = -1;
		rpc->call(u"writes"_s, {}, [&](QJsonObject reply) { writes = reply[u"result"_s].toInt(); });
		QTRY_COMPARE(writes, 1);
		rpc->call(u"release"_s, {}, nullptr);
		auto latest = false;
		QTRY_VERIFY_WITH_TIMEOUT(([&] {
			if (!latest) rpc->call(u"inspect_draft"_s, {1, 10}, [&](QJsonObject reply) {
				latest = reply[u"result"_s].toObject()[u"text"_s].toString() == u"latest"_s;
			});
			return latest;
		})(), 3000);
		rpc->stop();
	}

	void failedDraftWriteIsVisibleWithoutAutomaticRetry() {
		auto rpc = std::make_shared<DeltaRpc>();
		rpc->start(u"" RPC_FIXTURE_PATH ""_s, u"reject"_s);
		auto errors = 0;
		Delta::DraftStore drafts(rpc, [&](QString error) { QCOMPARE(error, u"write rejected"_s); ++errors; });
		drafts.save(1, 10, u"retain me"_s, 0);
		QTRY_COMPARE(errors, 1);
		QTest::qWait(100);
		QCOMPARE(errors, 1);
		rpc->stop();
	}
};

QTEST_GUILESS_MAIN(IntegrityTests)
#include "integrity_tests.moc"
