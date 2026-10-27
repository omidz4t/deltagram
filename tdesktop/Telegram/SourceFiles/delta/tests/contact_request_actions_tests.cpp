#include "contact_request_actions.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject FullChatJson(
		int id,
		const QString &type = u"Single"_s,
		bool request = true,
		QJsonArray contacts = QJsonArray{ 12 },
		bool device = false,
		bool selfTalk = false,
		QJsonValue canSend = false) {
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

struct ActionFixture {
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
			if (call.method == u"accept_chat"_s || call.method == u"block_chat"_s) {
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

class ContactRequestActionsTests final : public QObject {
	Q_OBJECT

	void expectAllowed(const ActionFixture &rpc) {
		const auto allowed = QStringList{
			u"get_full_chat_by_id"_s,
			u"accept_chat"_s,
			u"block_chat"_s,
			u"get_contact"_s,
		};
		for (const auto &call : rpc.log) {
			QVERIFY2(allowed.contains(call.method), qPrintable(call.method));
			QVERIFY(call.method != u"delete_chat"_s);
			QVERIFY(call.method != u"block_contact"_s);
			QVERIFY(call.method != u"unblock_contact"_s);
			QVERIFY(call.method != u"create_chat_by_contact_id"_s);
			QVERIFY(call.method != u"get_chat_id_by_contact_id"_s);
			QVERIFY(call.method != u"get_chatlist_entries"_s);
			QVERIFY(call.method != u"get_draft"_s);
			QVERIFY(call.method != u"misc_set_draft"_s);
			QVERIFY(call.method != u"remove_draft"_s);
			QVERIFY(call.method != u"send_msg"_s);
			QVERIFY(call.method != u"misc_send_draft"_s);
		}
	}

	void expectParams(const ActionFixture &rpc, int index, const QString &method, QJsonArray params) {
		QVERIFY(index >= 0 && index < int(rpc.log.size()));
		QCOMPARE(rpc.log[index].method, method);
		QCOMPARE(rpc.log[index].params, params);
	}

private slots:
	void parserRequiresExactIdTypeAndRequestFlag_data() {
		QTest::addColumn<QJsonObject>("json");
		QTest::addColumn<int>("expectedId");
		QTest::addColumn<bool>("ok");
		QTest::addColumn<bool>("request");
		QTest::newRow("single-request")
			<< FullChatJson(20) << 20 << true << true;
		QTest::newRow("group-request")
			<< FullChatJson(21, u"Group"_s, true, { 1, 12, 15 }) << 21 << true << true;
		QTest::newRow("mailinglist")
			<< FullChatJson(22, u"Mailinglist"_s) << 22 << true << true;
		QTest::newRow("inbroadcast")
			<< FullChatJson(23, u"InBroadcast"_s) << 23 << true << true;
		QTest::newRow("outbroadcast")
			<< FullChatJson(24, u"OutBroadcast"_s) << 24 << true << true;
		QTest::newRow("accepted-can-send-false")
			<< FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false)
			<< 20 << true << false;
		QTest::newRow("request-can-send-true")
			<< FullChatJson(20, u"Single"_s, true, { 1, 12 }, false, false, true)
			<< 20 << true << true;
		QTest::newRow("can-send-false-is-not-a-request")
			<< FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false)
			<< 20 << true << false;
		QTest::newRow("id-mismatch") << FullChatJson(21) << 20 << false << false;
		QTest::newRow("string-id") << QJsonObject{
			{ u"id"_s, u"20"_s }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isDeviceChat"_s, false },
			{ u"isSelfTalk"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("fraction-id") << QJsonObject{
			{ u"id"_s, 20.5 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isDeviceChat"_s, false },
			{ u"isSelfTalk"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("reserved-id") << FullChatJson(9) << 9 << false << false;
		QTest::newRow("unknown-type") << FullChatJson(20, u"Foo"_s) << 20 << false << false;
		QTest::newRow("missing-type") << QJsonObject{
			{ u"id"_s, 20 }, { u"isContactRequest"_s, true },
			{ u"isDeviceChat"_s, false }, { u"isSelfTalk"_s, false },
			{ u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("missing-request-flag") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isDeviceChat"_s, false }, { u"isSelfTalk"_s, false },
			{ u"contactIds"_s, QJsonArray{ 1, 12 } }, { u"canSend"_s, false },
		} << 20 << false << false;
		QTest::newRow("request-flag-number") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, 1 }, { u"isDeviceChat"_s, false },
			{ u"isSelfTalk"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("device")
			<< FullChatJson(20, u"Single"_s, true, { 5 }, true) << 20 << false << false;
		QTest::newRow("self-talk")
			<< FullChatJson(20, u"Single"_s, true, { 1 }, false, true) << 20 << false << false;
		QTest::newRow("missing-device-flag") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isSelfTalk"_s, false },
			{ u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("missing-self-talk") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isDeviceChat"_s, false },
			{ u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
		QTest::newRow("missing-contacts") << QJsonObject{
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isDeviceChat"_s, false },
			{ u"isSelfTalk"_s, false },
		} << 20 << false << false;
		QTest::newRow("fraction-contact") << FullChatJson(
			20, u"Single"_s, true, QJsonArray{ 1, 12.5 }) << 20 << false << false;
		QTest::newRow("duplicate-contact") << FullChatJson(
			20, u"Single"_s, true, QJsonArray{ 12, 12 }) << 20 << false << false;
		QTest::newRow("error-object") << QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"id"_s, 20 }, { u"chatType"_s, u"Single"_s },
			{ u"isContactRequest"_s, true }, { u"isDeviceChat"_s, false },
			{ u"isSelfTalk"_s, false }, { u"contactIds"_s, QJsonArray{ 1, 12 } },
		} << 20 << false << false;
	}

	void parserRequiresExactIdTypeAndRequestFlag() {
		QFETCH(QJsonObject, json);
		QFETCH(int, expectedId);
		QFETCH(bool, ok);
		QFETCH(bool, request);
		const auto parsed = Delta::CoreContactRequestChat(json, expectedId);
		QCOMPARE(parsed.has_value(), ok);
		if (ok) {
			QCOMPARE(parsed->id, expectedId);
			QCOMPARE(parsed->isContactRequest, request);
		}
		const auto fromReply = Delta::CoreContactRequestFromReply(
			{ { u"result"_s, json } }, expectedId);
		QCOMPARE(fromReply.has_value(), ok && !json.contains(u"error"_s));
		QVERIFY(!Delta::CoreContactRequestFromReply(
			{ { u"error"_s, u"offline"_s }, { u"result"_s, json } }, expectedId));
		QVERIFY(!Delta::CoreContactRequestFromReply(json, expectedId));
	}

	void parserIgnoresCanSendWhenClassifyingRequests() {
		const auto locked = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false), 20);
		QVERIFY(locked);
		QVERIFY(!locked->isContactRequest);
		const auto request = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, true, { 1, 12 }, false, false, true), 20);
		QVERIFY(request);
		QVERIFY(request->isContactRequest);
		QCOMPARE(int(request->peerContactIds.size()), 1);
		QCOMPARE(request->peerContactIds.front(), 12);
		QVERIFY(!Delta::CoreSingleRequestPeer(*request));
	}

	void singleRequestPeerRequiresExactNonReservedPeer() {
		const auto exact = Delta::CoreContactRequestChat(FullChatJson(20), 20);
		QVERIFY(exact);
		const auto peer = Delta::CoreSingleRequestPeer(*exact);
		QVERIFY(peer);
		QCOMPARE(*peer, 12);
		const auto mixed = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, true, { 1, 12 }), 20);
		QVERIFY(mixed);
		QVERIFY(!Delta::CoreSingleRequestPeer(*mixed));
		const auto reserved = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, true, { 5 }), 20);
		QVERIFY(reserved);
		QVERIFY(!Delta::CoreSingleRequestPeer(*reserved));
		const auto two = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, true, { 12, 15 }), 20);
		QVERIFY(two);
		QVERIFY(!Delta::CoreSingleRequestPeer(*two));
		const auto selfOnly = Delta::CoreContactRequestChat(
			FullChatJson(20, u"Single"_s, true, { 1 }), 20);
		QVERIFY(selfOnly);
		QVERIFY(!Delta::CoreSingleRequestPeer(*selfOnly));
	}

	void inspectReadDoesNotMutate() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto seen = 0;
		Delta::ReadCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto chat) {
			QVERIFY(chat);
			QVERIFY(chat->isContactRequest);
			++seen;
		});
		QCOMPARE(rpc.methods(), QStringList{ u"get_full_chat_by_id"_s });
		expectParams(rpc, 0, u"get_full_chat_by_id"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QCOMPARE(seen, 1);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void inspectRejectsReservedAndInvalidIdsWithoutRpc() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto seen = 0;
		for (const auto chat : { 0, 1, 9, -4 }) {
			Delta::ReadCoreContactRequest(rpc.bind(), scope, 2, chat, [&](auto value) {
				QVERIFY(!value);
				++seen;
			});
		}
		Delta::ReadCoreContactRequest(rpc.bind(), scope, 0, 20, [&](auto value) {
			QVERIFY(!value);
			++seen;
		});
		QCOMPARE(seen, 5);
		QVERIFY(rpc.log.empty());
	}

	void acceptVerifiedWhileCanSendRemainsFalse() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, { 1, 12 }, false, false, false)));
		expectParams(rpc, 1, u"accept_chat"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(result->wrote);
		QVERIFY(result->acknowledged);
		QVERIFY(result->error.isEmpty());
		QVERIFY(Delta::ContactRequestSucceeded(*result));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods(), (QStringList{ u"get_full_chat_by_id"_s, u"accept_chat"_s, u"get_full_chat_by_id"_s }));
		expectAllowed(rpc);
	}

	void acceptLostAckReconciledByReadbackWithoutRetry() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"message"_s, u"secret@example.test /home/user/accounts"_s },
		}));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!result->error.contains(u"secret@"_s));
		QVERIFY(!result->error.contains(u"/home/"_s));
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"accept_chat"_s), 1);
		expectAllowed(rpc);
	}

	void acceptErrorStillRequestIsDenied() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyError());
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QCOMPARE(result->error, Delta::kContactRequestUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void acceptAckUnreadableReadbackIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(u"chat missing"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(result->acknowledged);
		QVERIFY(result->wrote);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(result->error, Delta::kContactRequestUnverified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void staleOrMalformedPreflightDoesNotWrite_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("not-request") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false) },
		};
		QTest::newRow("can-send-false-without-request") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, false, { 1, 12 }, false, false, false) },
		};
		QTest::newRow("device") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, { 5 }, true) },
		};
		QTest::newRow("self") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Single"_s, true, { 1 }, false, true) },
		};
		QTest::newRow("id-mismatch") << QJsonObject{ { u"result"_s, FullChatJson(21) } };
		QTest::newRow("error") << QJsonObject{ { u"error"_s, u"offline"_s } };
		QTest::newRow("malformed") << QJsonObject{ { u"result"_s, QJsonArray{} } };
		QTest::newRow("unknown-type") << QJsonObject{
			{ u"result"_s, FullChatJson(20, u"Foo"_s) },
		};
	}

	void staleOrMalformedPreflightDoesNotWrite() {
		QFETCH(QJsonObject, reply);
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QVERIFY(!result->wrote);
		QCOMPARE(rpc.writes(), 0);
		QCOMPARE(rpc.methods(), QStringList{ u"get_full_chat_by_id"_s });
		expectAllowed(rpc);
	}

	void reservedChatDoesNotRpc() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 9, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kContactRequestUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void acceptOutBroadcastUsesAcceptChat() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 4, 40, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(40, u"OutBroadcast"_s)));
		expectParams(rpc, 1, u"accept_chat"_s, { 4, 40 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(40, u"OutBroadcast"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineSingleVerifiesBlockedPeerViaGetContact() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		expectParams(rpc, 1, u"block_chat"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		expectParams(rpc, 3, u"get_contact"_s, { 2, 12 });
		QVERIFY(rpc.replyResult(ContactJson(12, true)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(result->acknowledged);
		QCOMPARE(rpc.writes(), 1);
		QVERIFY(!rpc.has(u"block_contact"_s));
		QVERIFY(!rpc.has(u"delete_chat"_s));
		expectAllowed(rpc);
	}

	void declineSingleLostAckReconciledByBlockedContact() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyError(u"lost after commit"_s));
		QVERIFY(rpc.replyError(u"unreadable chat"_s));
		QVERIFY(rpc.replyResult(ContactJson(12, true)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QCOMPARE(rpc.writes(), 1);
		QCOMPARE(rpc.methods().count(u"block_chat"_s), 1);
		expectAllowed(rpc);
	}

	void declineSingleStillRequestIsDenied() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QVERIFY(!rpc.has(u"get_contact"_s));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineSingleWithoutPeerDoesNotWrite() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, { 1 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"get_contact"_s));
		expectAllowed(rpc);
	}

	void declineSingleMixedOrReservedPeerDoesNotWrite_data() {
		QTest::addColumn<QJsonArray>("contacts");
		QTest::newRow("self-and-peer") << QJsonArray{ 1, 12 };
		QTest::newRow("reserved") << QJsonArray{ 5 };
		QTest::newRow("two-peers") << QJsonArray{ 12, 15 };
		QTest::newRow("empty") << QJsonArray{};
	}

	void declineSingleMixedOrReservedPeerDoesNotWrite() {
		QFETCH(QJsonArray, contacts);
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, contacts)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QCOMPARE(result->error, Delta::kContactRequestStale);
		QVERIFY(!result->wrote);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"get_contact"_s));
		QVERIFY(!rpc.has(u"block_contact"_s));
		expectAllowed(rpc);
	}

	void mixedContactsRemainInspectableWithoutWrite() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto seen = 0;
		Delta::ReadCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto chat) {
			QVERIFY(chat);
			QVERIFY(chat->isContactRequest);
			QCOMPARE(int(chat->contactIds.size()), 2);
			QVERIFY(!Delta::CoreSingleRequestPeer(*chat));
			++seen;
		});
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, true, { 1, 12 })));
		QCOMPARE(seen, 1);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void declineSingleUnblockedContactIsDenied() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(rpc.replyResult(ContactJson(12, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QCOMPARE(result->error, Delta::kContactRequestUnconfirmed);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineSingleUnreadableContactIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"message"_s, u"contact alice@example.test"_s },
		}));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(!result->error.contains(u"alice@"_s));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineGroupRequiresReviewedDeletionConfirmation() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kContactRequestNeedsDeletionConfirm);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void declineGroupRejectsStaleConfirmation_data() {
		QTest::addColumn<int>("reviewChat");
		QTest::addColumn<int>("reviewType");
		QTest::addColumn<bool>("confirmed");
		QTest::newRow("wrong-id") << 29 << int(Delta::CoreChatType::Group) << true;
		QTest::newRow("wrong-type") << 30 << int(Delta::CoreChatType::Single) << true;
		QTest::newRow("unconfirmed") << 30 << int(Delta::CoreChatType::Group) << false;
	}

	void declineGroupRejectsStaleConfirmation() {
		QFETCH(int, reviewChat);
		QFETCH(int, reviewType);
		QFETCH(bool, confirmed);
		const auto review = Delta::ContactRequestDeletionReview{
			reviewChat, Delta::CoreChatType(reviewType), confirmed,
		};
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QCOMPARE(rpc.writes(), 0);
		expectAllowed(rpc);
	}

	void declineGroupAckedGetFullErrorIsAcknowledgedSuccess() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			30, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		expectParams(rpc, 1, u"block_chat"_s, { 2, 30 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(u"No chat with id 30"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Acknowledged);
		QVERIFY(result->acknowledged);
		QVERIFY(result->wrote);
		QVERIFY(result->error.isEmpty());
		QVERIFY(Delta::ContactRequestSucceeded(*result));
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QVERIFY(!rpc.has(u"get_chatlist_entries"_s));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineGroupLostAckGetFullErrorIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			30, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(rpc.replyError(u"lost after commit"_s));
		QVERIFY(rpc.replyError(u"No chat with id 30"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(result->error, Delta::kContactRequestUnverified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineGroupStillPresentRequestIsDenied() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			30, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Denied);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineGroupAckedReadableNonRequestIsAcknowledgedSuccess() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			30, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, false, { 1, 12, 15 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Acknowledged);
		QVERIFY(result->acknowledged);
		QVERIFY(result->error.isEmpty());
		QVERIFY(Delta::ContactRequestSucceeded(*result));
		expectAllowed(rpc);
	}

	void declineGroupLostAckReadableNonRequestIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			30, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 30, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, true, { 1, 12, 15 })));
		QVERIFY(rpc.replyError(u"lost after commit"_s));
		QVERIFY(rpc.replyResult(FullChatJson(30, u"Group"_s, false, { 1, 12, 15 })));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(result->error, Delta::kContactRequestUnverified);
		expectAllowed(rpc);
	}

	void declineMailinglistVerifiesClearedRequestWithoutDeletion() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 22, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(22, u"Mailinglist"_s)));
		expectParams(rpc, 1, u"block_chat"_s, { 2, 22 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(22, u"Mailinglist"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineInBroadcastVerifiesClearedRequest() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 23, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(23, u"InBroadcast"_s)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(23, u"InBroadcast"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineMailinglistLostAckFalseRequestIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 22, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(22, u"Mailinglist"_s)));
		QVERIFY(rpc.replyError(u"lost after commit"_s));
		QVERIFY(rpc.replyResult(FullChatJson(22, u"Mailinglist"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(result->error, Delta::kContactRequestUnverified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineInBroadcastLostAckFalseRequestIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 23, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(23, u"InBroadcast"_s)));
		QVERIFY(rpc.replyError(u"lost after commit"_s));
		QVERIFY(rpc.replyResult(FullChatJson(23, u"InBroadcast"_s, false)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(!result->acknowledged);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineMailinglistAckUnreadableIsUnknown() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 22, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(22, u"Mailinglist"_s)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyError(u"unreadable chat"_s));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Unknown);
		QVERIFY(result->acknowledged);
		QVERIFY(!Delta::ContactRequestSucceeded(*result));
		QCOMPARE(result->error, Delta::kContactRequestUnverified);
		QCOMPARE(rpc.writes(), 1);
		expectAllowed(rpc);
	}

	void declineOutBroadcastDoesNotWrite() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 24, {}, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(24, u"OutBroadcast"_s)));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kContactRequestCannotBlockChannel);
		QCOMPARE(rpc.writes(), 0);
		QVERIFY(!rpc.has(u"block_chat"_s));
		expectAllowed(rpc);
	}

	void groupReviewOnSingleStillUsesBlockChat() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		const auto review = Delta::ContactRequestDeletionReview{
			20, Delta::CoreChatType::Group, true,
		};
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, review, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		expectParams(rpc, 1, u"block_chat"_s, { 2, 20 });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(rpc.replyResult(ContactJson(12, true)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ContactRequestOutcome::Verified);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		expectAllowed(rpc);
	}

	void duplicateCallerIntentsEachIssueOneWrite() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto first = std::optional<Delta::ContactRequestResult>();
		auto second = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			first = std::move(value);
		});
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			second = std::move(value);
		});
		QCOMPARE(int(rpc.pending.size()), 2);
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QCOMPARE(rpc.writes(), 2);
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		QVERIFY(first);
		QVERIFY(second);
		QCOMPARE(first->outcome, Delta::ContactRequestOutcome::Verified);
		QCOMPARE(second->outcome, Delta::ContactRequestOutcome::Verified);
		QCOMPARE(rpc.writes(), 2);
		expectAllowed(rpc);
	}

	void scopeDropAtEachAcceptBoundary_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::addColumn<int>("expectedWrites");
		QTest::newRow("preflight") << 0 << 0;
		QTest::newRow("write") << 1 << 1;
		QTest::newRow("readback") << 2 << 1;
	}

	void scopeDropAtEachAcceptBoundary() {
		QFETCH(int, repliesBeforeInvalidate);
		QFETCH(int, expectedWrites);
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto) {
			done = true;
			QFAIL("Accept callback escaped its profile");
		});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		}
		if (repliesBeforeInvalidate >= 2) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		} else if (repliesBeforeInvalidate == 1) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		} else {
			QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), expectedWrites);
		QVERIFY(!rpc.has(u"delete_chat"_s));
		expectAllowed(rpc);
	}

	void scopeDropAtEachSingleDeclineBoundary_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::addColumn<int>("expectedWrites");
		QTest::newRow("preflight") << 0 << 0;
		QTest::newRow("write") << 1 << 1;
		QTest::newRow("readback") << 2 << 1;
		QTest::newRow("contact") << 3 << 1;
	}

	void scopeDropAtEachSingleDeclineBoundary() {
		QFETCH(int, repliesBeforeInvalidate);
		QFETCH(int, expectedWrites);
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::DeclineCoreContactRequest(rpc.bind(), scope, 2, 20, {}, [&](auto) {
			done = true;
			QFAIL("Decline callback escaped its profile");
		});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		}
		if (repliesBeforeInvalidate >= 2) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		}
		if (repliesBeforeInvalidate >= 3) {
			QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(FullChatJson(20)));
		} else if (repliesBeforeInvalidate == 1) {
			QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		} else if (repliesBeforeInvalidate == 2) {
			QVERIFY(rpc.replyResult(FullChatJson(20, u"Single"_s, false)));
		} else {
			QVERIFY(rpc.replyResult(ContactJson(12, true)));
		}
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), expectedWrites);
		expectAllowed(rpc);
	}

	void inspectScopeDropDoesNotDeliver() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto) {
			done = true;
			QFAIL("Inspect callback escaped its profile");
		});
		scope.invalidate();
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QVERIFY(!done);
		QCOMPARE(rpc.writes(), 0);
	}

	void inspectNewScopeGenerationReadsWithoutWriting() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		const auto previous = scope.guard();
		scope.invalidate();
		QVERIFY(!previous());
		auto seen = 0;
		Delta::ReadCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto chat) {
			++seen;
			QVERIFY(chat.has_value());
			QCOMPARE(chat->id, 20);
			QVERIFY(chat->isContactRequest);
		});
		QCOMPARE(rpc.methods(), QStringList{ u"get_full_chat_by_id"_s });
		QCOMPARE(seen, 0);
		QVERIFY(rpc.replyResult(FullChatJson(20)));
		QCOMPARE(seen, 1);
		QCOMPARE(rpc.writes(), 0);
	}

	void localErrorsDoNotEchoCoreSecrets() {
		ActionFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::ContactRequestResult>();
		Delta::AcceptCoreContactRequest(rpc.bind(), scope, 2, 20, [&](auto value) {
			result = std::move(value);
		});
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"code"_s, -32603 },
			{ u"message"_s, u"sqlite /var/secret/dc.db for user@chatmail.test"_s },
			{ u"data"_s, u"token=abc"_s },
		}));
		QVERIFY(result);
		QCOMPARE(result->error, Delta::kContactRequestStale);
		QVERIFY(!result->error.contains(u"sqlite"_s));
		QVERIFY(!result->error.contains(u"user@"_s));
		QVERIFY(!result->error.contains(u"token="_s));
		QCOMPARE(rpc.writes(), 0);
	}
};

QTEST_GUILESS_MAIN(ContactRequestActionsTests)
#include "contact_request_actions_tests.moc"
