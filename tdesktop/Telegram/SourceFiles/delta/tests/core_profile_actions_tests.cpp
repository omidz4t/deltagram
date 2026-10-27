#include "core_profile_actions.h"

#include <initializer_list>
#include <memory>

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject ContactJson(
		int id,
		const QString &name = u"Integrity peer"_s,
		bool key = true,
		bool e2ee = true) {
	return {
		{ u"id"_s, id },
		{ u"name"_s, name },
		{ u"displayName"_s, name },
		{ u"isKeyContact"_s, key },
		{ u"e2eeAvail"_s, e2ee },
	};
}

QJsonObject ContactMap(int id, const QJsonObject &contact = {}) {
	return { { QString::number(id), contact.isEmpty() ? ContactJson(id) : contact } };
}

QJsonArray ContactListJson(std::initializer_list<int> ids) {
	auto list = QJsonArray();
	for (const auto id : ids) {
		list.append(ContactJson(id));
	}
	return list;
}

QJsonObject FullChatJson(
		int id,
		const QString &type = u"Single"_s,
		QJsonArray contacts = QJsonArray{ 1, 10 },
		bool device = false,
		bool selfTalk = false) {
	return {
		{ u"id"_s, id },
		{ u"chatType"_s, type },
		{ u"isDeviceChat"_s, device },
		{ u"isSelfTalk"_s, selfTalk },
		{ u"contactIds"_s, std::move(contacts) },
	};
}

struct ProfileFixture {
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
			if (call.method == u"change_contact_name"_s
				|| call.method == u"delete_contact"_s
				|| call.method == u"delete_chat"_s) {
				++count;
			}
		}
		return count;
	}

	[[nodiscard]] bool has(const QString &method) const {
		return methods().contains(method);
	}

	Delta::AccountInventoryRequest marked(std::shared_ptr<int> owner) {
		return [owner, inner = bind()](
				QString method,
				QJsonArray params,
				DeltaRpc::Callback done) {
			inner(std::move(method), std::move(params), std::move(done));
		};
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

const auto kSecretError = QJsonObject{
	{ u"code"_s, -32603 },
	{ u"message"_s, u"sqlite /var/secret/dc.db for user@chatmail.test"_s },
	{ u"data"_s, u"token=abc"_s },
};

} // namespace

class CoreProfileActionsTests final : public QObject {
	Q_OBJECT

	void expectAllowed(const ProfileFixture &rpc) {
		const auto allowed = QStringList{
			u"get_contact"_s,
			u"get_contacts"_s,
			u"get_contacts_by_ids"_s,
			u"get_full_chat_by_id"_s,
			u"get_contact_encryption_info"_s,
			u"get_chat_encryption_info"_s,
			u"get_chat_id_by_contact_id"_s,
			u"change_contact_name"_s,
			u"delete_contact"_s,
			u"delete_chat"_s,
		};
		for (const auto &call : rpc.log) {
			QVERIFY2(allowed.contains(call.method), qPrintable(call.method));
			QVERIFY(call.method != u"create_contact"_s);
			QVERIFY(call.method != u"create_chat_by_contact_id"_s);
			QVERIFY(call.method != u"accept_chat"_s);
			QVERIFY(call.method != u"block_chat"_s);
			QVERIFY(call.method != u"block_contact"_s);
			QVERIFY(call.method != u"unblock_contact"_s);
			QVERIFY(call.method != u"send_msg"_s);
			QVERIFY(call.method != u"misc_send_draft"_s);
			QVERIFY(call.method != u"add_contact_to_chat"_s);
			QVERIFY(call.method != u"remove_contact_from_chat"_s);
			QVERIFY(call.method != u"lookup_contact_id_by_addr"_s);
		}
	}

	void expectContactDeleteOnly(const ProfileFixture &rpc) {
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"get_chat_id_by_contact_id"_s));
		QVERIFY(!rpc.has(u"create_contact"_s));
		QVERIFY(!rpc.has(u"accept_chat"_s));
		QVERIFY(!rpc.has(u"unblock_contact"_s));
		QVERIFY(!rpc.has(u"send_msg"_s));
	}

	bool replyHiddenLists(ProfileFixture &rpc) {
		return rpc.replyResult(ContactListJson({ 11 }))
			&& rpc.replyResult(ContactListJson({ 1, 11 }))
			&& rpc.replyResult(ContactListJson({}));
	}

	void expectParams(
			const ProfileFixture &rpc,
			int index,
			const QString &method,
			QJsonArray params) {
		QVERIFY(index >= 0 && index < int(rpc.log.size()));
		QCOMPARE(rpc.log[index].method, method);
		QCOMPARE(rpc.log[index].params, params);
	}

	void expectLocalError(const QString &error) {
		QVERIFY(!error.contains(u"sqlite"_s));
		QVERIFY(!error.contains(u"user@"_s));
		QVERIFY(!error.contains(u"token="_s));
		QVERIFY(!error.contains(u"@chatmail"_s));
		QVERIFY(!error.contains(u"/var/secret"_s));
	}

private slots:
	void contactParserRequiresExactIdAndFlags_data() {
		QTest::addColumn<QJsonObject>("json");
		QTest::addColumn<int>("expected");
		QTest::addColumn<bool>("ok");
		QTest::newRow("key-e2ee") << ContactJson(10) << 10 << true;
		QTest::newRow("address-contact") << ContactJson(10, u"Integrity peer"_s, false, false)
			<< 10 << true;
		QTest::newRow("id-mismatch") << ContactJson(11) << 10 << false;
		QTest::newRow("reserved") << ContactJson(1) << 1 << false;
		QTest::newRow("device") << ContactJson(5) << 5 << false;
		QTest::newRow("string-id") << QJsonObject{
			{ u"id"_s, u"10"_s }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, true },
		} << 10 << false;
		QTest::newRow("fraction-id") << QJsonObject{
			{ u"id"_s, 10.5 }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, true },
		} << 10 << false;
		QTest::newRow("missing-name") << QJsonObject{
			{ u"id"_s, 10 }, { u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, true },
		} << 10 << false;
		QTest::newRow("non-bool-key") << QJsonObject{
			{ u"id"_s, 10 }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, 1 }, { u"e2eeAvail"_s, true },
		} << 10 << false;
		QTest::newRow("non-bool-e2ee") << QJsonObject{
			{ u"id"_s, 10 }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, u"yes"_s },
		} << 10 << false;
		QTest::newRow("error-object") << QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"id"_s, 10 }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, true },
		} << 10 << false;
	}

	void contactParserRequiresExactIdAndFlags() {
		QFETCH(QJsonObject, json);
		QFETCH(int, expected);
		QFETCH(bool, ok);
		QCOMPARE(Delta::CoreProfileContactFromJson(json, expected).has_value(), ok);
		QCOMPARE(
			Delta::CoreProfileContactFromReply({ { u"result"_s, json } }, expected).has_value(),
			ok && !json.contains(u"error"_s));
		QVERIFY(!Delta::CoreProfileContactFromReply(
			{ { u"error"_s, u"offline"_s }, { u"result"_s, json } }, expected));
	}

	void mapParserUsesStringKeys() {
		const auto mapped = Delta::CoreProfileContactFromMap(ContactMap(10), 10);
		QVERIFY(mapped);
		QCOMPARE(mapped->id, 10);
		QVERIFY(mapped->isKeyContact);
		QVERIFY(mapped->e2eeAvail);
		QVERIFY(!Delta::CoreProfileContactFromMap(ContactMap(11), 10));
		QVERIFY(!Delta::CoreProfileContactFromMapReply(
			{ { u"error"_s, u"offline"_s }, { u"result"_s, ContactMap(10) } }, 10));
	}

	void singleChatParserExactPeerBoundary_data() {
		QTest::addColumn<QJsonObject>("json");
		QTest::addColumn<int>("expected");
		QTest::addColumn<bool>("ok");
		QTest::addColumn<int>("peer");
		QTest::newRow("self-and-peer") << FullChatJson(20) << 20 << true << 10;
		QTest::newRow("peer-only") << FullChatJson(20, u"Single"_s, { 10 }) << 20 << true << 10;
		QTest::newRow("device-and-peer")
			<< FullChatJson(20, u"Single"_s, { 5, 10 }) << 20 << true << 10;
		QTest::newRow("special-and-peer")
			<< FullChatJson(20, u"Single"_s, { 9, 10 }) << 20 << true << 10;
		QTest::newRow("two-peers")
			<< FullChatJson(20, u"Single"_s, { 1, 10, 11 }) << 20 << true << 0;
		QTest::newRow("self-only") << FullChatJson(20, u"Single"_s, { 1 }) << 20 << true << 0;
		QTest::newRow("device-only") << FullChatJson(20, u"Single"_s, { 5 }) << 20 << true << 0;
		QTest::newRow("empty") << FullChatJson(20, u"Single"_s, {}) << 20 << true << 0;
		QTest::newRow("group") << FullChatJson(13, u"Group"_s, { 1, 10 }) << 13 << false << 0;
		QTest::newRow("mailinglist")
			<< FullChatJson(22, u"Mailinglist"_s, { 1, 10 }) << 22 << false << 0;
		QTest::newRow("broadcast")
			<< FullChatJson(23, u"InBroadcast"_s, { 1, 10 }) << 23 << false << 0;
		QTest::newRow("device-chat")
			<< FullChatJson(20, u"Single"_s, { 1, 10 }, true) << 20 << false << 0;
		QTest::newRow("self-talk")
			<< FullChatJson(20, u"Single"_s, { 1 }, false, true) << 20 << false << 0;
		QTest::newRow("id-mismatch") << FullChatJson(21) << 20 << false << 0;
		QTest::newRow("reserved-chat") << FullChatJson(9) << 9 << false << 0;
		QTest::newRow("duplicate")
			<< FullChatJson(20, u"Single"_s, { 10, 10 }) << 20 << false << 0;
		QTest::newRow("fraction-contact")
			<< FullChatJson(20, u"Single"_s, { 1, 10.5 }) << 20 << false << 0;
		QTest::newRow("string-contact")
			<< FullChatJson(20, u"Single"_s, QJsonArray{ 1, u"10"_s }) << 20 << false << 0;
		QTest::newRow("missing-device") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isSelfTalk"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 10 } },
		} << 20 << false << 0;
		QTest::newRow("missing-self-talk") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isDeviceChat"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 10 } },
		} << 20 << false << 0;
		QTest::newRow("unknown-type") << FullChatJson(20, u"Foo"_s) << 20 << false << 0;
	}

	void singleChatParserExactPeerBoundary() {
		QFETCH(QJsonObject, json);
		QFETCH(int, expected);
		QFETCH(bool, ok);
		QFETCH(int, peer);
		const auto parsed = Delta::CoreProfileSingleChat(json, expected);
		QCOMPARE(parsed.has_value(), ok);
		if (!ok) {
			return;
		}
		const auto exact = Delta::CoreExactPeerContactId(*parsed);
		if (peer == 0) {
			QVERIFY(!exact);
		} else {
			QVERIFY(exact);
			QCOMPARE(*exact, peer);
		}
	}

	void senderResolveReadsContactOnly() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			13,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10, u"Integrity sender"_s, true, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->contactId, 10);
		QCOMPARE(result->chatId, 0);
		QCOMPARE(result->contact.name, u"Integrity sender"_s);
		QVERIFY(result->contact.isKeyContact);
		QVERIFY(!result->contact.e2eeAvail);
		QVERIFY(!rpc.has(u"get_full_chat_by_id"_s));
		QVERIFY(!rpc.has(u"get_contacts_by_ids"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void chatResolveUsesExactPeerFromFullChat() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		expectParams(rpc, 1, u"get_contacts_by_ids"_s, { 2, QJsonArray{ 10 } });
		QVERIFY(rpc.replyResult(ContactMap(10)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->contactId, 10);
		QCOMPARE(result->chatId, 20);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"create_contact"_s));
		expectAllowed(rpc);
	}

	void fullChatErrorDoesNotLookupContacts() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		expectLocalError(result->error);
		QVERIFY(!rpc.has(u"get_contacts_by_ids"_s));
		QCOMPARE(rpc.methods(), QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void groupChatDoesNotResolveContact() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			13,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(FullChatJson(13, u"Group"_s, { 1, 10 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		QVERIFY(!rpc.has(u"get_contacts_by_ids"_s));
		QVERIFY(!rpc.has(u"get_contact"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void selfOnlyChatDoesNotResolve() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, { 1 })));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		QVERIFY(!rpc.has(u"get_contacts_by_ids"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void missingMapContactIsUnreadable() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(ContactMap(11)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreProfileUnreadable);
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_full_chat_by_id"_s,
			u"get_contacts_by_ids"_s,
		}));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void twoRealContactsDoNotResolve() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, { 1, 10, 11 })));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		QVERIFY(!rpc.has(u"get_contacts_by_ids"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void reservedOrInvalidIdsDoNotRpc_data() {
		QTest::addColumn<int>("account");
		QTest::addColumn<int>("sender");
		QTest::addColumn<int>("chat");
		QTest::newRow("account-zero") << 0 << 10 << 0;
		QTest::newRow("sender-self") << 2 << 1 << 0;
		QTest::newRow("sender-device") << 2 << 5 << 0;
		QTest::newRow("sender-special") << 2 << 9 << 0;
		QTest::newRow("chat-special") << 2 << 0 << 9;
		QTest::newRow("missing-both") << 2 << 0 << 0;
	}

	void reservedOrInvalidIdsDoNotRpc() {
		QFETCH(int, account);
		QFETCH(int, sender);
		QFETCH(int, chat);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			account,
			sender > 0 ? std::optional<int>(sender) : std::nullopt,
			chat > 0 ? std::optional<int>(chat) : std::nullopt,
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void malformedContactReadIsUnreadable_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("error") << QJsonObject{ { u"error"_s, kSecretError } };
		QTest::newRow("wrong-id") << QJsonObject{ { u"result"_s, ContactJson(11) } };
		QTest::newRow("non-bool") << QJsonObject{ { u"result"_s, QJsonObject{
			{ u"id"_s, 10 }, { u"name"_s, u"Integrity peer"_s },
			{ u"displayName"_s, u"Integrity peer"_s },
			{ u"isKeyContact"_s, true }, { u"e2eeAvail"_s, 1 },
		} } };
		QTest::newRow("array") << QJsonObject{ { u"result"_s, QJsonArray{} } };
		QTest::newRow("null") << QJsonObject{ { u"result"_s, QJsonValue() } };
	}

	void malformedContactReadIsUnreadable() {
		QFETCH(QJsonObject, reply);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			std::nullopt,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreProfileUnreadable);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionContactUsesTypedId() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			10,
			20,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_contact_encryption_info"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(u"fingerprint line"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->text, u"fingerprint line"_s);
		QVERIFY(!rpc.has(u"get_chat_encryption_info"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionChatKeepsGroupReadable() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			13,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_chat_encryption_info"_s, { 2, 13 });
		QVERIFY(rpc.replyResult(u"group fingerprints"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->text, u"group fingerprints"_s);
		QVERIFY(!rpc.has(u"get_contact_encryption_info"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionFingerprintTextIsKept() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			10,
			std::nullopt,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(u"E2EE fingerprint for user@chatmail.test"_s));
		QVERIFY(result);
		QCOMPARE(result->text, u"E2EE fingerprint for user@chatmail.test"_s);
		QVERIFY(result->error.isEmpty());
		expectAllowed(rpc);
	}

	void encryptionTextFromReplyBoundaries_data() {
		QTest::addColumn<QString>("text");
		QTest::addColumn<bool>("ok");
		QTest::newRow("empty") << u""_s << false;
		QTest::newRow("spaces") << u"   "_s << false;
		QTest::newRow("newlines") << u"\n\n"_s << false;
		QTest::newRow("tabs") << u"\t \n"_s << false;
		QTest::newRow("plain") << u"fingerprint line"_s << true;
		QTest::newRow("multiline")
			<< u"Messages are end-to-end encrypted.\n\nFirst diagnostic line.\nSecond diagnostic line."_s
			<< true;
	}

	void encryptionTextFromReplyBoundaries() {
		QFETCH(QString, text);
		QFETCH(bool, ok);
		const auto parsed = Delta::CoreEncryptionTextFromReply({
			{ u"result"_s, text },
		});
		QCOMPARE(parsed.has_value(), ok);
		if (ok) {
			QCOMPARE(*parsed, text);
		}
	}

	void encryptionEmptyReplyIsUnreadable_data() {
		QTest::addColumn<QString>("text");
		QTest::newRow("empty") << u""_s;
		QTest::newRow("whitespace") << u" \n\t "_s;
	}

	void encryptionEmptyReplyIsUnreadable() {
		QFETCH(QString, text);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			13,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(text));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreEncryptionUnreadable);
		QVERIFY(result->text.isEmpty());
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionMultilineBodyIsKept() {
		const auto body = u"Messages are end-to-end encrypted.\n\nFirst diagnostic line.\nSecond diagnostic line."_s;
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			13,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(body));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->text, body);
		QVERIFY(result->error.isEmpty());
		QCOMPARE(
			*Delta::CoreEncryptionTextFromReply({ { u"result"_s, body } }),
			body);
		QVERIFY(!rpc.has(u"get_contact_encryption_info"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionNonStringIsUnreadable_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("error") << QJsonObject{ { u"error"_s, kSecretError } };
		QTest::newRow("object") << QJsonObject{ { u"result"_s, QJsonObject{} } };
		QTest::newRow("number") << QJsonObject{ { u"result"_s, 10 } };
		QTest::newRow("null") << QJsonObject{ { u"result"_s, QJsonValue() } };
		QTest::newRow("missing") << QJsonObject{};
	}

	void encryptionNonStringIsUnreadable() {
		QFETCH(QJsonObject, reply);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreEncryptionUnreadable);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void encryptionReservedDoesNotRpc_data() {
		QTest::addColumn<int>("contact");
		QTest::addColumn<int>("chat");
		QTest::newRow("self") << 1 << 0;
		QTest::newRow("device") << 5 << 0;
		QTest::newRow("special") << 0 << 9;
		QTest::newRow("missing") << 0 << 0;
	}

	void encryptionReservedDoesNotRpc() {
		QFETCH(int, contact);
		QFETCH(int, chat);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			contact > 0 ? std::optional<int>(contact) : std::nullopt,
			chat > 0 ? std::optional<int>(chat) : std::nullopt,
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kCoreEncryptionUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void nullAckRequiresResultField() {
		QVERIFY(Delta::CoreProfileRpcNull({
			{ u"result"_s, QJsonValue(QJsonValue::Null) },
		}));
		QVERIFY(!Delta::CoreProfileRpcNull({}));
		QVERIFY(!Delta::CoreProfileRpcNull({ { u"error"_s, u"offline"_s } }));
		QVERIFY(!Delta::CoreProfileRpcNull(QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"result"_s, QJsonValue(QJsonValue::Null) },
		}));
		QVERIFY(!Delta::CoreProfileRpcNull({ { u"result"_s, QJsonObject{} } }));
	}

	void chatLookupParserBoundaries() {
		const auto found = Delta::CoreProfileChatIdLookup({
			{ u"result"_s, 20 },
		});
		QCOMPARE(found.kind, Delta::CoreProfileLookup::Found);
		QCOMPARE(found.chatId, 20);
		QCOMPARE(
			Delta::CoreProfileChatIdLookup({
				{ u"result"_s, QJsonValue(QJsonValue::Null) },
			}).kind,
			Delta::CoreProfileLookup::Missing);
		QCOMPARE(Delta::CoreProfileChatIdLookup({}).kind, Delta::CoreProfileLookup::Invalid);
		QCOMPARE(
			Delta::CoreProfileChatIdLookup({ { u"error"_s, u"offline"_s } }).kind,
			Delta::CoreProfileLookup::Invalid);
		QCOMPARE(
			Delta::CoreProfileChatIdLookup({ { u"result"_s, 9 } }).kind,
			Delta::CoreProfileLookup::Invalid);
		QCOMPARE(
			Delta::CoreProfileChatIdLookup({ { u"result"_s, 10.5 } }).kind,
			Delta::CoreProfileLookup::Invalid);
	}

	void contactListParserUsesExactIds() {
		const auto omitted = Delta::CoreProfileContactIdsFromListReply({
			{ u"result"_s, ContactListJson({ 11, 12 }) },
		});
		QVERIFY(omitted);
		QVERIFY(!omitted->contains(10));
		QVERIFY(omitted->contains(11));
		const auto listed = Delta::CoreProfileContactIdsFromListReply({
			{ u"result"_s, ContactListJson({ 10 }) },
		});
		QVERIFY(listed);
		QVERIFY(listed->contains(10));
		QVERIFY(!Delta::CoreProfileContactIdsFromListReply({}));
		QVERIFY(!Delta::CoreProfileContactIdsFromListReply({
			{ u"error"_s, u"offline"_s },
			{ u"result"_s, ContactListJson({}) },
		}));
		QVERIFY(!Delta::CoreProfileContactIdsFromListReply({
			{ u"result"_s, QJsonObject{} },
		}));
		QVERIFY(!Delta::CoreProfileContactIdsFromListReply({
			{ u"result"_s, QJsonArray{ 10 } },
		}));
		QVERIFY(!Delta::CoreProfileContactIdsFromListReply({
			{ u"result"_s, QJsonArray{ ContactJson(10), ContactJson(10) } },
		}));
	}

	void changeNameVerifiesReadbackOnce() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"  Renamed peer  "_s,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		expectParams(rpc, 1, u"change_contact_name"_s, { 2, 10, u"Renamed peer"_s });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		expectParams(rpc, 2, u"get_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10, u"Renamed peer"_s)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(result->acknowledged);
		QCOMPARE(result->contact.name, u"Renamed peer"_s);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"change_contact_name"_s), 1);
		QVERIFY(!rpc.has(u"create_contact"_s));
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"accept_chat"_s));
		expectAllowed(rpc);
	}

	void changeNameLostAckMatchingNameIsVerified() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(rpc.replyResult(ContactJson(10, u"Renamed peer"_s)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(result->error.isEmpty());
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"change_contact_name"_s), 1);
		QCOMPARE(rpc.methods().count(u"get_contact"_s), 2);
		expectAllowed(rpc);
	}

	void changeNameLostAckPreservedNameIsDenied() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QCOMPARE(result->error, Delta::kCoreProfileUnconfirmed);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"get_contact"_s), 2);
		expectAllowed(rpc);
	}

	void changeNameMalformedAckUnreadableIsUnknown() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.reply({}));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void changeNameAckedUnreadableIsUnknown() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(u"offline"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void changeNameMismatchedReadbackAfterAckIsUnknown() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(10, u"Old name"_s)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->acknowledged);
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		QCOMPARE(result->contact.name, u"Old name"_s);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"change_contact_name"_s), 1);
		expectAllowed(rpc);
	}

	void changeNameEmptyClearsPrivateAlias() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"   "_s,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10, u"Private alias"_s)));
		expectParams(rpc, 1, u"change_contact_name"_s, { 2, 10, u""_s });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactJson(10, u""_s)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(result->wrote);
		QCOMPARE(result->contact.name, u""_s);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void deleteMalformedAckListFailureIsNotSuccess() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		expectParams(rpc, 0, u"get_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		expectParams(rpc, 1, u"delete_contact"_s, { 2, 10 });
		QVERIFY(rpc.reply({}));
		expectParams(
			rpc,
			2,
			u"get_contacts"_s,
			Delta::CoreProfileContactListArgs(2, Delta::kCoreContactListDefault));
		QVERIFY(rpc.replyError(u"not found"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		QCOMPARE(rpc.methods().count(u"get_contacts"_s), 1);
		expectContactDeleteOnly(rpc);
		expectAllowed(rpc);
	}

	void deleteNullAckHiddenReadableContactIsVerified() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		expectParams(
			rpc,
			2,
			u"get_contacts"_s,
			Delta::CoreProfileContactListArgs(2, Delta::kCoreContactListDefault));
		QVERIFY(rpc.replyResult(ContactListJson({ 11 })));
		expectParams(
			rpc,
			3,
			u"get_contacts"_s,
			Delta::CoreProfileContactListArgs(2, Delta::kCoreContactListAddSelf));
		QVERIFY(rpc.replyResult(ContactListJson({ 1, 11 })));
		expectParams(
			rpc,
			4,
			u"get_contacts"_s,
			Delta::CoreProfileContactListArgs(2, Delta::kCoreContactListAddress));
		QVERIFY(rpc.replyResult(ContactListJson({})));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(result->error.isEmpty());
		QVERIFY(Delta::CoreProfileSucceeded(*result));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"get_contact"_s), 1);
		expectContactDeleteOnly(rpc);
		expectAllowed(rpc);
	}

	void deleteAckListReadFailureIsAcknowledged() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Acknowledged);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(result->error.isEmpty());
		QVERIFY(Delta::CoreProfileSucceeded(*result));
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"get_contacts"_s), 1);
		expectContactDeleteOnly(rpc);
		expectAllowed(rpc);
	}

	void deleteAddressListStillPresentIsNotHidden() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactListJson({ 11 })));
		QVERIFY(rpc.replyResult(ContactListJson({ 1 })));
		QVERIFY(rpc.replyResult(ContactListJson({ 10 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(rpc.methods().count(u"get_contacts"_s), 3);
		QCOMPARE(rpc.writes(), 1);
		expectContactDeleteOnly(rpc);
		expectAllowed(rpc);
	}

	void deleteStillListedAfterAckIsUnknown() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactListJson({ 10, 11 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"get_contacts"_s), 1);
		expectAllowed(rpc);
	}

	void deleteLostAckHiddenListsIsVerified() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(replyHiddenLists(rpc));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(result->error.isEmpty());
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		expectContactDeleteOnly(rpc);
		expectAllowed(rpc);
	}

	void deleteLostAckPreservedListIsDenied() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(rpc.replyResult(ContactListJson({ 10 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QCOMPARE(result->error, Delta::kCoreProfileUnconfirmed);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"get_contacts"_s), 1);
		expectAllowed(rpc);
	}

	void deleteDoesNotMutateForeignListId() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(replyHiddenLists(rpc));
		QVERIFY(result);
		QCOMPARE(result->contactId, 10);
		expectParams(rpc, 1, u"delete_contact"_s, { 2, 10 });
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		expectAllowed(rpc);
	}

	void deleteChatNullLookupIsUnknownWithoutWrites() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		expectParams(rpc, 1, u"get_chat_id_by_contact_id"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(!result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!result->chatAttempted);
		QVERIFY(!result->chatWrote);
		QVERIFY(!result->chatAcknowledged);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileChatAmbiguous);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"delete_contact"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void deleteChatThenContactUsesExactIds() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		expectParams(rpc, 1, u"get_chat_id_by_contact_id"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(20));
		expectParams(rpc, 2, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		expectParams(rpc, 3, u"delete_chat"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		expectParams(rpc, 4, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyError(u"not found"_s));
		expectParams(rpc, 5, u"delete_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(replyHiddenLists(rpc));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->chatId, 20);
		QVERIFY(result->attempted);
		QVERIFY(result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(result->chatAttempted);
		QVERIFY(result->chatWrote);
		QVERIFY(result->chatAcknowledged);
		QCOMPARE(rpc.writes(), 2);
		QCOMPARE(rpc.methods().count(u"delete_chat"_s), 1);
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		QCOMPARE(rpc.methods().count(u"get_full_chat_by_id"_s), 2);
		expectAllowed(rpc);
	}

	void deleteChatLookupErrorDoesNotWrite() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(!result->attempted);
		QVERIFY(!result->wrote);
		QCOMPARE(result->error, Delta::kCoreProfileUnreadable);
		expectLocalError(result->error);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"delete_contact"_s));
		expectAllowed(rpc);
	}

	void deleteChatRemainingStopsBeforeContact() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(20));
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->chatAttempted);
		QVERIFY(!result->chatWrote);
		QVERIFY(result->chatAcknowledged);
		QCOMPARE(result->chatId, 20);
		QVERIFY(!rpc.has(u"delete_contact"_s));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"delete_chat"_s), 1);
		expectAllowed(rpc);
	}

	void staleAccountDropsResolveCallback_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::newRow("before-rpc") << 0;
		QTest::newRow("after-fullchat") << 1;
	}

	void staleAccountDropsResolveCallback() {
		QFETCH(int, repliesBeforeInvalidate);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ResolveCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			std::nullopt,
			20,
			[&](auto) {
				done = true;
				QFAIL("Resolve callback escaped its profile");
			});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		} else {
			QVERIFY(rpc.replyResult(ContactMap(10)));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void staleAccountDropsNameReadbackAfterOneWrite() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto) {
				done = true;
				QFAIL("Name callback escaped its profile");
			});
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		scope.invalidate();
		QVERIFY(rpc.replyResult(ContactJson(10, u"Renamed peer"_s)));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"change_contact_name"_s), 1);
		expectAllowed(rpc);
	}

	void staleAccountDropsEncryptionCallback() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadCoreEncryptionInfo(
			rpc.bind(),
			scope,
			2,
			10,
			std::nullopt,
			[&](auto) {
				done = true;
				QFAIL("Encryption callback escaped its profile");
			});
		scope.invalidate();
		QVERIFY(rpc.replyResult(u"fingerprint line"_s));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void staleAccountDropsDeletePreRead() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto) {
				done = true;
				QFAIL("Delete pre-read callback escaped its profile");
			});
		scope.invalidate();
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void staleAccountDropsNameWriteAfterPreRead() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ChangeCoreContactName(
			rpc.bind(),
			scope,
			2,
			10,
			u"Renamed peer"_s,
			[&](auto) {
				done = true;
				QFAIL("Name write callback escaped its profile");
			});
		QVERIFY(rpc.replyResult(ContactJson(10)));
		scope.invalidate();
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void staleAccountDropsDeleteListReadAfterOneWrite() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto) {
				done = true;
				QFAIL("Delete callback escaped its profile");
			});
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		scope.invalidate();
		QVERIFY(rpc.replyResult(ContactListJson({})));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void staleAccountDropsDeleteChatLookup() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto) {
				done = true;
				QFAIL("Delete-chat callback escaped its profile");
			},
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		scope.invalidate();
		QVERIFY(rpc.replyResult(20));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void staleAccountDropsDeleteChatReview() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto) {
				done = true;
				QFAIL("Delete-chat review callback escaped its profile");
			},
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(20));
		scope.invalidate();
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void originalProbeFlagsStayFalseTrue() {
		ProfileFixture malformed;
		auto malformedScope = Delta::AccountScope();
		auto malformedResult = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			malformed.bind(),
			malformedScope,
			2,
			10,
			[&](auto value) { malformedResult = std::move(value); });
		QVERIFY(malformed.replyResult(ContactJson(10)));
		QVERIFY(malformed.reply({}));
		QVERIFY(malformed.replyError(u"not found"_s));
		QVERIFY(malformedResult);
		QVERIFY(!Delta::CoreProfileSucceeded(*malformedResult));

		ProfileFixture hidden;
		auto hiddenScope = Delta::AccountScope();
		auto hiddenResult = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			hidden.bind(),
			hiddenScope,
			2,
			10,
			[&](auto value) { hiddenResult = std::move(value); });
		QVERIFY(hidden.replyResult(ContactJson(10)));
		QVERIFY(hidden.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(replyHiddenLists(hidden));
		QVERIFY(hiddenResult);
		QVERIFY(Delta::CoreProfileSucceeded(*hiddenResult));
		QVERIFY(!Delta::CoreProfileSucceeded(*malformedResult));
	}

	void deleteReleasesRequestMarkerOnFinish() {
		auto owner = std::make_shared<int>(1);
		const auto weak = std::weak_ptr<int>(owner);
		auto result = std::optional<Delta::CoreProfileResult>();
		{
			ProfileFixture rpc;
			auto scope = Delta::AccountScope();
			Delta::DeleteCoreProfileContact(
				rpc.marked(owner),
				scope,
				2,
				10,
				[&](auto value) { result = std::move(value); });
			QVERIFY(rpc.replyResult(ContactJson(10)));
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
			QVERIFY(replyHiddenLists(rpc));
			QVERIFY(result);
			QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
			QVERIFY(rpc.pending.empty());
		}
		owner.reset();
		QVERIFY(weak.expired());
	}

	void deleteReleasesRequestMarkerWhenPendingDroppedAfterInvalidate() {
		auto owner = std::make_shared<int>(1);
		const auto weak = std::weak_ptr<int>(owner);
		{
			ProfileFixture rpc;
			auto scope = Delta::AccountScope();
			auto done = false;
			Delta::DeleteCoreProfileContact(
				rpc.marked(owner),
				scope,
				2,
				10,
				[&](auto) {
					done = true;
					QFAIL("Delete callback escaped its profile");
				});
			QVERIFY(rpc.replyResult(ContactJson(10)));
			QVERIFY(!rpc.pending.empty());
			scope.invalidate();
			rpc.pending.clear();
			QVERIFY(!done);
		}
		owner.reset();
		QVERIFY(weak.expired());
	}

	void deleteInlineCallbackDoesNotDestroyActiveFunctor() {
		auto request = std::make_shared<Delta::AccountInventoryRequest>();
		const auto weakRequest = std::weak_ptr<Delta::AccountInventoryRequest>(request);
		auto result = std::optional<Delta::CoreProfileResult>();
		auto scope = Delta::AccountScope();
		*request = [weakRequest](QString method, QJsonArray, DeltaRpc::Callback done) {
			QVERIFY(!weakRequest.expired());
			if (method == u"get_contact"_s) {
				done({ { u"result"_s, ContactJson(10) } });
			} else if (method == u"delete_contact"_s) {
				done({ { u"result"_s, QJsonValue(QJsonValue::Null) } });
			} else if (method == u"get_contacts"_s) {
				done({ { u"result"_s, ContactListJson({}) } });
			} else {
				QFAIL(qPrintable(method));
			}
			QVERIFY(!weakRequest.expired());
		};
		Delta::DeleteCoreProfileContact(
			[request](QString method, QJsonArray params, DeltaRpc::Callback done) {
				(*request)(std::move(method), std::move(params), std::move(done));
			},
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QVERIFY(!weakRequest.expired());
		request.reset();
		QVERIFY(weakRequest.expired());
	}

	void deleteChatInvalidReviewDoesNotWrite_data() {
		QTest::addColumn<QJsonObject>("chat");
		QTest::addColumn<int>("outcome");
		QTest::newRow("wrong-id")
			<< FullChatJson(21)
			<< int(Delta::CoreProfileOutcome::Unknown);
		QTest::newRow("group")
			<< FullChatJson(20, u"Group"_s, { 1, 10 })
			<< int(Delta::CoreProfileOutcome::Denied);
		QTest::newRow("device")
			<< FullChatJson(20, u"Single"_s, { 1, 10 }, true)
			<< int(Delta::CoreProfileOutcome::Denied);
		QTest::newRow("self")
			<< FullChatJson(20, u"Single"_s, { 1, 10 }, false, true)
			<< int(Delta::CoreProfileOutcome::Denied);
		QTest::newRow("wrong-peer")
			<< FullChatJson(20, u"Single"_s, { 1, 11 })
			<< int(Delta::CoreProfileOutcome::Denied);
	}

	void deleteChatInvalidReviewDoesNotWrite() {
		QFETCH(QJsonObject, chat);
		QFETCH(int, outcome);
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(20));
		expectParams(rpc, 2, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(chat));
		QVERIFY(result);
		QCOMPARE(int(result->outcome), outcome);
		QVERIFY(!result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->chatAttempted);
		QVERIFY(!result->chatWrote);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"delete_contact"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void deleteChatReviewedBlockedSingleDeletesOnce() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true,
			20);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		expectParams(rpc, 1, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		expectParams(rpc, 2, u"delete_chat"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		expectParams(rpc, 3, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyError(u"not found"_s));
		expectParams(rpc, 4, u"delete_contact"_s, { 2, 10 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(replyHiddenLists(rpc));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Verified);
		QCOMPARE(result->chatId, 20);
		QVERIFY(result->chatAttempted);
		QVERIFY(result->chatWrote);
		QVERIFY(result->chatAcknowledged);
		QVERIFY(result->wrote);
		QVERIFY(!rpc.has(u"get_chat_id_by_contact_id"_s));
		QCOMPARE(rpc.writes(), 2);
		QCOMPARE(rpc.methods().count(u"delete_chat"_s), 1);
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		expectAllowed(rpc);
	}

	void deleteChatLostAckAmbiguousNullDoesNotWriteContact() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true,
			20);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyError(kSecretError));
		QVERIFY(rpc.replyError(u"not found"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(result->chatAttempted);
		QVERIFY(!result->chatWrote);
		QVERIFY(!result->chatAcknowledged);
		QCOMPARE(result->chatId, 20);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		expectLocalError(result->error);
		QVERIFY(!rpc.has(u"delete_contact"_s));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"delete_chat"_s), 1);
		expectAllowed(rpc);
	}

	void deleteChatPartialContactUnconfirmedKeepsChatFields() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true,
			20);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(u"not found"_s));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(ContactListJson({ 10 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Unknown);
		QVERIFY(result->attempted);
		QVERIFY(!result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(result->chatAttempted);
		QVERIFY(result->chatWrote);
		QVERIFY(result->chatAcknowledged);
		QCOMPARE(result->chatId, 20);
		QVERIFY(!Delta::CoreProfileSucceeded(*result));
		QCOMPARE(result->error, Delta::kCoreProfileUnverified);
		QCOMPARE(rpc.writes(), 2);
		QCOMPARE(rpc.methods().count(u"delete_chat"_s), 1);
		QCOMPARE(rpc.methods().count(u"delete_contact"_s), 1);
		expectAllowed(rpc);
	}

	void deleteChatReviewedInvalidIdDoesNotWrite() {
		ProfileFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::CoreProfileResult>();
		Delta::DeleteCoreProfileContact(
			rpc.bind(),
			scope,
			2,
			10,
			[&](auto value) { result = std::move(value); },
			true,
			9);
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::CoreProfileOutcome::Denied);
		QCOMPARE(result->error, Delta::kCoreProfileUnavailable);
		QVERIFY(!rpc.has(u"get_chat_id_by_contact_id"_s));
		QVERIFY(!rpc.has(u"get_full_chat_by_id"_s));
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"delete_contact"_s));
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void unselectHelperRequiresExactChatEvidence() {
		Delta::CoreProfileResult result;
		result.outcome = Delta::CoreProfileOutcome::Verified;
		result.chatId = 20;
		result.chatAcknowledged = true;
		QVERIFY(Delta::CoreProfileShouldUnselectChat(result, 2, 2, 20));
		result.chatWrote = true;
		result.chatAcknowledged = false;
		QVERIFY(Delta::CoreProfileShouldUnselectChat(result, 2, 2, 20));
		QVERIFY(!Delta::CoreProfileShouldUnselectChat(result, 2, 3, 20));
		QVERIFY(!Delta::CoreProfileShouldUnselectChat(result, 2, 2, 21));
		QVERIFY(!Delta::CoreProfileShouldUnselectChat(result, 2, 2, std::nullopt));
		result.chatWrote = false;
		result.chatAcknowledged = false;
		QVERIFY(!Delta::CoreProfileShouldUnselectChat(result, 2, 2, 20));
		result.outcome = Delta::CoreProfileOutcome::Unknown;
		result.chatAcknowledged = true;
		QVERIFY(!Delta::CoreProfileShouldUnselectChat(result, 2, 2, 20));
	}

	void reviewedSingleChatRequiresExactPeer() {
		QVERIFY(Delta::CoreProfileReviewedSingleChat(
			{ { u"result"_s, FullChatJson(20) } }, 20, 10));
		QVERIFY(!Delta::CoreProfileReviewedSingleChat(
			{ { u"result"_s, FullChatJson(20, u"Group"_s) } }, 20, 10));
		QVERIFY(!Delta::CoreProfileReviewedSingleChat(
			{ { u"result"_s, FullChatJson(20, u"Single"_s, { 1, 11 }) } }, 20, 10));
		QVERIFY(!Delta::CoreProfileReviewedSingleChat(
			{ { u"result"_s, FullChatJson(21) } }, 20, 10));
		QVERIFY(Delta::CoreProfileFullChatIdPresent(
			{ { u"result"_s, FullChatJson(20, u"Group"_s) } }, 20));
		QVERIFY(!Delta::CoreProfileFullChatIdPresent(
			{ { u"result"_s, FullChatJson(21) } }, 20));
		QVERIFY(!Delta::CoreProfileFullChatIdPresent(
			{ { u"error"_s, u"offline"_s }, { u"result"_s, FullChatJson(20) } }, 20));
	}
};

QTEST_GUILESS_MAIN(CoreProfileActionsTests)
#include "core_profile_actions_tests.moc"
