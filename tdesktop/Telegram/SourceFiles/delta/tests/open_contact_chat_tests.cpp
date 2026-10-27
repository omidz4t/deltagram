#include "open_contact_chat.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject FullChatJson(
		int id,
		const QString &type = u"Single"_s,
		bool request = false,
		QJsonArray contacts = QJsonArray{ 12 },
		bool device = false,
		bool selfTalk = false,
		QJsonValue canSend = QJsonValue(QJsonValue::Undefined)) {
	auto json = QJsonObject{
		{ u"id"_s, id },
		{ u"chatType"_s, type },
		{ u"isContactRequest"_s, request },
		{ u"isDeviceChat"_s, device },
		{ u"isSelfTalk"_s, selfTalk },
		{ u"contactIds"_s, std::move(contacts) },
	};
	if (!canSend.isUndefined()) {
		json.insert(u"canSend"_s, canSend);
	}
	return json;
}

QJsonObject ContactJson(int id, bool blocked) {
	return { { u"id"_s, id }, { u"isBlocked"_s, blocked } };
}

struct OpenFixture {
	struct Pending {
		QString method;
		QJsonArray params;
		DeltaRpc::Callback done;
	};
	std::vector<Pending> pending;
	std::vector<Pending> log;

	Delta::AccountInventoryRequest bind() {
		return [this](QString method, QJsonArray params, DeltaRpc::Callback done) {
			log.push_back({ method, params, {} });
			pending.push_back({ std::move(method), std::move(params), std::move(done) });
		};
	}

	[[nodiscard]] QStringList methods() const {
		auto names = QStringList();
		for (const auto &call : log) {
			names.push_back(call.method);
		}
		return names;
	}

	[[nodiscard]] int writes() const {
		auto count = 0;
		for (const auto &call : log) {
			if (call.method == u"create_chat_by_contact_id"_s) {
				++count;
			}
		}
		return count;
	}

	[[nodiscard]] bool has(const QString &method) const {
		return methods().contains(method);
	}

	bool reply(QJsonObject envelope) {
		if (pending.empty()) {
			return false;
		}
		auto done = std::move(pending.front().done);
		pending.erase(pending.begin());
		done(std::move(envelope));
		return true;
	}

	bool replyResult(QJsonValue value) {
		return reply({ { u"result"_s, std::move(value) } });
	}

	bool replyError(QJsonValue error = u"interrupted"_s) {
		return reply({ { u"error"_s, std::move(error) } });
	}
};

} // namespace

class OpenContactChatTests final : public QObject {
	Q_OBJECT

	void expectAllowed(const OpenFixture &rpc) {
		const auto allowed = QStringList{
			u"get_chat_id_by_contact_id"_s,
			u"get_full_chat_by_id"_s,
			u"get_contact"_s,
			u"create_chat_by_contact_id"_s,
		};
		for (const auto &call : rpc.log) {
			QVERIFY2(allowed.contains(call.method), qPrintable(call.method));
			QVERIFY(call.method != u"accept_chat"_s);
			QVERIFY(call.method != u"block_chat"_s);
			QVERIFY(call.method != u"block_contact"_s);
			QVERIFY(call.method != u"unblock_contact"_s);
			QVERIFY(call.method != u"delete_chat"_s);
			QVERIFY(call.method != u"get_chatlist_entries"_s);
			QVERIFY(call.method != u"send_msg"_s);
			QVERIFY(call.method != u"misc_send_draft"_s);
		}
	}

	void expectParams(const OpenFixture &rpc, int index, const QString &method, QJsonArray params) {
		QVERIFY(index >= 0 && index < int(rpc.log.size()));
		QCOMPARE(rpc.log[index].method, method);
		QCOMPARE(rpc.log[index].params, params);
	}

	void open(
			OpenFixture &rpc,
			Delta::AccountScope &scope,
			std::optional<std::pair<int, QString>> &result,
			int account = 2,
			int contact = 12) {
		Delta::OpenCoreContactChat(rpc.bind(), scope, account, contact, [&](int chat, QString error) {
			result = std::pair<int, QString>(chat, std::move(error));
		});
	}

private slots:
	void existingRequestOpensWithoutCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		expectParams(rpc, 0, u"get_chat_id_by_contact_id"_s, { 2, 12 });
		QVERIFY(rpc.replyResult(20));
		expectParams(rpc, 1, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, { 12 }, false, false, false)));
		QVERIFY(result);
		QCOMPARE(result->first, 20);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"get_contact"_s));
		QVERIFY(!rpc.has(u"accept_chat"_s));
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_chat_id_by_contact_id"_s,
			u"get_full_chat_by_id"_s,
		}));
		expectAllowed(rpc);
	}

	void existingAcceptedOpensWithoutCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(20));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false, { 12 })));
		QVERIFY(result);
		QCOMPARE(result->first, 20);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"accept_chat"_s));
		expectAllowed(rpc);
	}

	void missingUnblockedCreatesOnce() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		expectParams(rpc, 1, u"get_contact"_s, { 2, 12 });
		QVERIFY(rpc.replyResult(ContactJson(12, false)));
		expectParams(rpc, 2, u"create_chat_by_contact_id"_s, { 2, 12 });
		QVERIFY(rpc.replyResult(30));
		expectParams(rpc, 3, u"get_full_chat_by_id"_s, { 2, 30 });
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Single"_s, false, { 12 })));
		QVERIFY(result);
		QCOMPARE(result->first, 30);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"create_chat_by_contact_id"_s), 1);
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_chat_id_by_contact_id"_s,
			u"get_contact"_s,
			u"create_chat_by_contact_id"_s,
			u"get_full_chat_by_id"_s,
		}));
		expectAllowed(rpc);
	}

	void missingBlockedDoesNotCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(12, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactBlocked);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"get_full_chat_by_id"_s));
		expectAllowed(rpc);
	}

	void lookupErrorDoesNotCreateAndRedactsCoreText() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"code"_s, -32603 },
			{ u"message"_s, u"sqlite /var/secret/dc.db for user@chatmail.test"_s },
			{ u"data"_s, u"token=abc"_s },
		}));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QVERIFY(!result->second.contains(u"sqlite"_s));
		QVERIFY(!result->second.contains(u"user@"_s));
		QVERIFY(!result->second.contains(u"token="_s));
		QCOMPARE(rpc.writes(), 0);
		QCOMPARE(rpc.methods(), QStringList{ u"get_chat_id_by_contact_id"_s });
		expectAllowed(rpc);
	}

	void lookupMalformedDoesNotCreate_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("string") << QJsonObject{ { u"result"_s, u"20"_s } };
		QTest::newRow("fraction") << QJsonObject{ { u"result"_s, 20.5 } };
		QTest::newRow("zero") << QJsonObject{ { u"result"_s, 0 } };
		QTest::newRow("reserved") << QJsonObject{ { u"result"_s, 9 } };
		QTest::newRow("object") << QJsonObject{ { u"result"_s, QJsonObject{} } };
		QTest::newRow("missing-result") << QJsonObject{};
		QTest::newRow("error-and-id") << QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"result"_s, 20 },
		};
	}

	void lookupMalformedDoesNotCreate() {
		QFETCH(QJsonObject, reply);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"get_contact"_s));
		expectAllowed(rpc);
	}

	void visibleLookupFullChatFaultDoesNotCreate_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("error") << QJsonObject{ { u"error"_s, u"offline"_s } };
		QTest::newRow("id-mismatch") << QJsonObject{
			{ u"result"_s, FullChatJson(21, u"Single"_s, true, { 12 }) },
		};
		QTest::newRow("group") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Group"_s, true, { 12 }) },
		};
		QTest::newRow("wrong-peer") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, { 15 }) },
		};
		QTest::newRow("mixed") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, { 1, 12 }) },
		};
		QTest::newRow("empty-contacts") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, QJsonArray{}) },
		};
		QTest::newRow("device") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, { 12 }, true) },
		};
		QTest::newRow("self-talk") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, false, { 12 }, false, true) },
		};
		QTest::newRow("missing-request") << QJsonObject{ { u"result"_s, QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isDeviceChat"_s, false }, { u"isSelfTalk"_s, false },
			{ u"contactIds"_s, QJsonArray{ 12 } },
		} } };
		QTest::newRow("malformed") << QJsonObject{ { u"result"_s, QJsonArray{} } };
	}

	void visibleLookupFullChatFaultDoesNotCreate() {
		QFETCH(QJsonObject, reply);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(20));
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_chat_id_by_contact_id"_s,
			u"get_full_chat_by_id"_s,
		}));
		expectAllowed(rpc);
	}

	void unreadableContactDoesNotCreate_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("error") << QJsonObject{ { u"error"_s, u"offline"_s } };
		QTest::newRow("wrong-id") << QJsonObject{ { u"result"_s, ContactJson(15, false) } };
		QTest::newRow("missing-flag") << QJsonObject{
			{ u"result"_s, QJsonObject{ { u"id"_s, 12 } } },
		};
		QTest::newRow("reserved-id") << QJsonObject{ { u"result"_s, ContactJson(1, false) } };
		QTest::newRow("malformed") << QJsonObject{ { u"result"_s, QJsonArray{} } };
	}

	void unreadableContactDoesNotCreate() {
		QFETCH(QJsonObject, reply);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		expectAllowed(rpc);
	}

	void createErrorNoRetry() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(12, false)));
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"message"_s, u"secret@example.test /home/user/accounts"_s },
		}));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnconfirmed);
		QVERIFY(!result->second.contains(u"secret@"_s));
		QVERIFY(!result->second.contains(u"/home/"_s));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"create_chat_by_contact_id"_s), 1);
		QVERIFY(!rpc.has(u"get_full_chat_by_id"_s));
		expectAllowed(rpc);
	}

	void createMalformedNoRetry_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("string") << QJsonObject{ { u"result"_s, u"30"_s } };
		QTest::newRow("fraction") << QJsonObject{ { u"result"_s, 30.5 } };
		QTest::newRow("zero") << QJsonObject{ { u"result"_s, 0 } };
		QTest::newRow("reserved") << QJsonObject{ { u"result"_s, 9 } };
		QTest::newRow("null") << QJsonObject{ { u"result"_s, QJsonValue(QJsonValue::Null) } };
		QTest::newRow("missing-result") << QJsonObject{};
		QTest::newRow("error-and-id") << QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"result"_s, 30 },
		};
	}

	void createMalformedNoRetry() {
		QFETCH(QJsonObject, reply);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(12, false)));
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		QVERIFY(!rpc.has(u"get_full_chat_by_id"_s));
		expectAllowed(rpc);
	}

	void createReadbackFaultNoRetry_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("error") << QJsonObject{ { u"error"_s, u"chat missing"_s } };
		QTest::newRow("request") << QJsonObject{
			{ u"result"_s, FullChatJson(30, u"Single"_s, true, { 12 }) },
		};
		QTest::newRow("wrong-peer") << QJsonObject{
			{ u"result"_s, FullChatJson(30, u"Single"_s, false, { 15 }) },
		};
		QTest::newRow("id-mismatch") << QJsonObject{
			{ u"result"_s, FullChatJson(31, u"Single"_s, false, { 12 }) },
		};
		QTest::newRow("device") << QJsonObject{
			{ u"result"_s, FullChatJson(30, u"Single"_s, false, { 12 }, true) },
		};
	}

	void createReadbackFaultNoRetry() {
		QFETCH(QJsonObject, reply);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(12, false)));
		QVERIFY(rpc.replyResult(30));
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"create_chat_by_contact_id"_s), 1);
		expectAllowed(rpc);
	}

	void reservedContactsDoNotRpc_data() {
		QTest::addColumn<int>("contact");
		QTest::newRow("zero") << 0;
		QTest::newRow("info") << 2;
		QTest::newRow("device") << 5;
		QTest::newRow("last-special") << 9;
		QTest::newRow("negative") << -4;
	}

	void reservedContactsDoNotRpc() {
		QFETCH(int, contact);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, contact);
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void invalidAccountDoesNotRpc() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 0, 12);
		QVERIFY(result);
		QCOMPARE(result->second, Delta::kOpenContactUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void selfExistingOpensWithoutCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		expectParams(rpc, 0, u"get_chat_id_by_contact_id"_s, { 2, 1 });
		QVERIFY(rpc.replyResult(10));
		expectParams(rpc, 1, u"get_full_chat_by_id"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 1 }, false, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 10);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"get_contact"_s));
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		expectAllowed(rpc);
	}

	void selfMissingCreatesOnce() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(!rpc.has(u"get_contact"_s));
		expectParams(rpc, 1, u"create_chat_by_contact_id"_s, { 2, 1 });
		QVERIFY(rpc.replyResult(10));
		expectParams(rpc, 2, u"get_full_chat_by_id"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 1 }, false, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 10);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_chat_id_by_contact_id"_s,
			u"create_chat_by_contact_id"_s,
			u"get_full_chat_by_id"_s,
		}));
		expectAllowed(rpc);
	}

	void selfWrongPeerDoesNotCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(10));
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 12 }, false, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		expectAllowed(rpc);
	}

	void selfDeviceDoesNotCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(10));
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 1 }, true, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void selfRequestDoesNotCreate() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(10));
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, true, { 1 }, false, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnreadable);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		expectAllowed(rpc);
	}

	void selfCreateReadbackRequestNoRetry() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(10));
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, true, { 1 }, false, true)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"create_chat_by_contact_id"_s), 1);
		expectAllowed(rpc);
	}

	void selfCreateReadbackNotSelfTalkNoRetry() {
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<int, QString>>();
		open(rpc, scope, result, 2, 1);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(10));
		QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 1 }, false, false)));
		QVERIFY(result);
		QCOMPARE(result->first, 0);
		QCOMPARE(result->second, Delta::kOpenContactUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void scopeDropAtExistingBoundaries_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::newRow("lookup") << 0;
		QTest::newRow("fullchat") << 1;
	}

	void scopeDropAtExistingBoundaries() {
		QFETCH(int, repliesBeforeInvalidate);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::OpenCoreContactChat(rpc.bind(), scope, 2, 12, [&](int, QString) {
			done = true;
			QFAIL("Open callback escaped its profile");
		});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(20));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(20));
		} else {
			QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, { 12 })));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void scopeDropAtCreateBoundaries_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::addColumn<int>("expectedWrites");
		QTest::newRow("lookup") << 0 << 0;
		QTest::newRow("contact") << 1 << 0;
		QTest::newRow("create") << 2 << 1;
		QTest::newRow("readback") << 3 << 1;
	}

	void scopeDropAtCreateBoundaries() {
		QFETCH(int, repliesBeforeInvalidate);
		QFETCH(int, expectedWrites);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::OpenCoreContactChat(rpc.bind(), scope, 2, 12, [&](int, QString) {
			done = true;
			QFAIL("Open callback escaped its profile");
		});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		}
		if (repliesBeforeInvalidate >= 2) {
			QVERIFY(rpc.replyResult(ContactJson(12, false)));
		}
		if (repliesBeforeInvalidate >= 3) {
			QVERIFY(rpc.replyResult(30));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		} else if (repliesBeforeInvalidate == 1) {
			QVERIFY(rpc.replyResult(ContactJson(12, false)));
		} else if (repliesBeforeInvalidate == 2) {
			QVERIFY(rpc.replyResult(30));
		} else {
			QVERIFY(rpc.replyResult(FullChatJson(30, u"Single"_s, false, { 12 })));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), expectedWrites);
		expectAllowed(rpc);
	}

	void scopeDropAtSelfCreateBoundaries_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::addColumn<int>("expectedWrites");
		QTest::newRow("lookup") << 0 << 0;
		QTest::newRow("create") << 1 << 1;
		QTest::newRow("readback") << 2 << 1;
	}

	void scopeDropAtSelfCreateBoundaries() {
		QFETCH(int, repliesBeforeInvalidate);
		QFETCH(int, expectedWrites);
		OpenFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::OpenCoreContactChat(rpc.bind(), scope, 2, 1, [&](int, QString) {
			done = true;
			QFAIL("Self open callback escaped its profile");
		});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		}
		if (repliesBeforeInvalidate >= 2) {
			QVERIFY(rpc.replyResult(10));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		} else if (repliesBeforeInvalidate == 1) {
			QVERIFY(rpc.replyResult(10));
		} else {
			QVERIFY(rpc.replyResult(FullChatJson(10, u"Single"_s, false, { 1 }, false, true)));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), expectedWrites);
		QVERIFY(!rpc.has(u"get_contact"_s));
		expectAllowed(rpc);
	}
};

QTEST_GUILESS_MAIN(OpenContactChatTests)
#include "open_contact_chat_tests.moc"
