#include "incoming_notification.h"

#include <QtTest/QtTest>

#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

QJsonObject MessageJson(
		int id,
		int chat,
		int state,
		bool info,
		const QString &text = u"hello"_s) {
	return {
		{ u"id"_s, id },
		{ u"chatId"_s, chat },
		{ u"state"_s, state },
		{ u"isInfo"_s, info },
		{ u"text"_s, text },
		{ u"fromId"_s, info ? 2 : 12 },
		{ u"systemMessageType"_s, info ? u"ChatE2ee"_s : u"Unknown"_s },
	};
}

QJsonObject BasicChatJson(int id, const QString &name) {
	return {
		{ u"id"_s, id },
		{ u"name"_s, name },
		{ u"chatType"_s, u"Group"_s },
		{ u"isContactRequest"_s, false },
		{ u"isMuted"_s, false },
		{ u"isDeviceChat"_s, false },
		{ u"isSelfTalk"_s, false },
	};
}

struct NameFixture {
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
			pending.push_back({
				std::move(method),
				std::move(params),
				std::move(done),
			});
		};
	}

	[[nodiscard]] QStringList methods() const {
		auto names = QStringList();
		for (const auto &call : log) {
			names.push_back(call.method);
		}
		return names;
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

void expectNameOnly(const NameFixture &rpc) {
	for (const auto &call : rpc.log) {
		QCOMPARE(call.method, u"get_basic_chat_info"_s);
		QVERIFY(call.method != u"get_full_chat_by_id"_s);
		QVERIFY(call.method != u"get_fresh_msgs"_s);
		QVERIFY(call.method != u"can_send"_s);
		QVERIFY(call.method != u"is_chat_muted"_s);
		QVERIFY(call.method != u"accept_chat"_s);
		QVERIFY(call.method != u"send_msg"_s);
	}
}

} // namespace

class IncomingNotificationTests final : public QObject {
	Q_OBJECT

private slots:
	void eventKindOnlyIncomingMsgAuthorizesUnread_data() {
		QTest::addColumn<QString>("kind");
		QTest::addColumn<bool>("incoming");
		QTest::newRow("IncomingMsg") << u"IncomingMsg"_s << true;
		QTest::newRow("MsgsChanged") << u"MsgsChanged"_s << false;
		QTest::newRow("ReactionsChanged") << u"ReactionsChanged"_s << false;
		QTest::newRow("IncomingReaction") << u"IncomingReaction"_s << false;
		QTest::newRow("MsgRead") << u"MsgRead"_s << false;
		QTest::newRow("MsgReadCountChanged") << u"MsgReadCountChanged"_s << false;
		QTest::newRow("MsgDelivered") << u"MsgDelivered"_s << false;
		QTest::newRow("MsgFailed") << u"MsgFailed"_s << false;
		QTest::newRow("empty") << u""_s << false;
		QTest::newRow("incoming-lowercase") << u"incomingmsg"_s << false;
	}

	void eventKindOnlyIncomingMsgAuthorizesUnread() {
		QFETCH(QString, kind);
		QFETCH(bool, incoming);
		QCOMPARE(
			Delta::IncomingEventKindFromName(kind),
			incoming
				? Delta::IncomingEventKind::Incoming
				: Delta::IncomingEventKind::Existing);
	}

	void classifierMatrix_data() {
		QTest::addColumn<QString>("kind");
		QTest::addColumn<QJsonObject>("message");
		QTest::addColumn<int>("chat");
		QTest::addColumn<int>("msg");
		QTest::addColumn<int>("expected");
		QTest::newRow("IncomingMsg-normal-fresh")
			<< u"IncomingMsg"_s
			<< MessageJson(313, 18, 10, false, u"user text"_s)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Unread);
		QTest::newRow("IncomingMsg-request-fresh")
			<< u"IncomingMsg"_s
			<< MessageJson(145, 13, 10, false)
			<< 13 << 145 << int(Delta::IncomingApplyKind::Unread);
		QTest::newRow("MsgsChanged-info-13")
			<< u"MsgsChanged"_s
			<< MessageJson(
				312,
				18,
				13,
				true,
				u"Messages are end-to-end encrypted."_s)
			<< 18 << 312 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("MsgsChanged-normal-fresh")
			<< u"MsgsChanged"_s
			<< MessageJson(313, 18, 10, false, u"user text"_s)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("IncomingMsg-info")
			<< u"IncomingMsg"_s
			<< MessageJson(
				312,
				18,
				13,
				true,
				u"Messages are end-to-end encrypted."_s)
			<< 18 << 312 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("IncomingMsg-info-fresh")
			<< u"IncomingMsg"_s
			<< MessageJson(144, 13, 10, true)
			<< 13 << 144 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("draft-19")
			<< u"IncomingMsg"_s
			<< MessageJson(9, 18, 19, false)
			<< 18 << 9 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("MsgsChanged-draft-19")
			<< u"MsgsChanged"_s
			<< MessageJson(9, 18, 19, false)
			<< 18 << 9 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("id-mismatch")
			<< u"IncomingMsg"_s
			<< MessageJson(312, 18, 10, false)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("chat-mismatch")
			<< u"IncomingMsg"_s
			<< MessageJson(313, 17, 10, false)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("missing-isInfo")
			<< u"IncomingMsg"_s
			<< QJsonObject{
				{ u"id"_s, 313 },
				{ u"chatId"_s, 18 },
				{ u"state"_s, 10 },
			}
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("isInfo-number")
			<< u"IncomingMsg"_s
			<< QJsonObject{
				{ u"id"_s, 313 },
				{ u"chatId"_s, 18 },
				{ u"state"_s, 10 },
				{ u"isInfo"_s, 0 },
			}
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("state-string")
			<< u"IncomingMsg"_s
			<< QJsonObject{
				{ u"id"_s, 313 },
				{ u"chatId"_s, 18 },
				{ u"state"_s, u"10"_s },
				{ u"isInfo"_s, false },
			}
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("empty-json")
			<< u"IncomingMsg"_s
			<< QJsonObject()
			<< 18 << 313 << int(Delta::IncomingApplyKind::Skip);
		QTest::newRow("outgoing-existing-pending")
			<< u"MsgsChanged"_s
			<< MessageJson(80, 18, 20, false)
			<< 18 << 80 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("IncomingReaction-fresh")
			<< u"IncomingReaction"_s
			<< MessageJson(313, 18, 10, false)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("MsgDelivered")
			<< u"MsgDelivered"_s
			<< MessageJson(80, 18, 26, false)
			<< 18 << 80 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("noticed-normal")
			<< u"IncomingMsg"_s
			<< MessageJson(313, 18, 13, false)
			<< 18 << 313 << int(Delta::IncomingApplyKind::Existing);
		QTest::newRow("fresh-list-absence-is-not-a-gate")
			<< u"IncomingMsg"_s
			<< MessageJson(147, 14, 10, false)
			<< 14 << 147 << int(Delta::IncomingApplyKind::Unread);
	}

	void classifierMatrix() {
		QFETCH(QString, kind);
		QFETCH(QJsonObject, message);
		QFETCH(int, chat);
		QFETCH(int, msg);
		QFETCH(int, expected);
		QCOMPARE(
			int(Delta::ClassifyIncomingMessage(
				Delta::IncomingEventKindFromName(kind),
				chat,
				msg,
				message)),
			expected);
	}

	void defaultOriginIsExisting() {
		QCOMPARE(
			Delta::ClassifyIncomingMessage(
				Delta::IncomingEventKind::Existing,
				18,
				313,
				MessageJson(313, 18, 10, false)),
			Delta::IncomingApplyKind::Existing);
	}

	void eventIdsRequireExactPositiveInts_data() {
		QTest::addColumn<QJsonObject>("event");
		QTest::addColumn<int>("chat");
		QTest::addColumn<int>("msg");
		QTest::newRow("camel")
			<< QJsonObject{
				{ u"kind"_s, u"IncomingMsg"_s },
				{ u"chatId"_s, 18 },
				{ u"msgId"_s, 313 },
			}
			<< 18 << 313;
		QTest::newRow("snake")
			<< QJsonObject{
				{ u"kind"_s, u"MsgsChanged"_s },
				{ u"chat_id"_s, 18 },
				{ u"msg_id"_s, 312 },
			}
			<< 18 << 312;
		QTest::newRow("zero")
			<< QJsonObject{ { u"chatId"_s, 0 }, { u"msgId"_s, 313 } }
			<< 0 << 313;
		QTest::newRow("fraction")
			<< QJsonObject{ { u"chatId"_s, 18.5 }, { u"msgId"_s, 313 } }
			<< 0 << 313;
		QTest::newRow("string")
			<< QJsonObject{
				{ u"chatId"_s, u"18"_s },
				{ u"msgId"_s, u"313"_s },
			}
			<< 0 << 0;
	}

	void eventIdsRequireExactPositiveInts() {
		QFETCH(QJsonObject, event);
		QFETCH(int, chat);
		QFETCH(int, msg);
		QCOMPARE(Delta::IncomingEventChatId(event), chat);
		QCOMPARE(Delta::IncomingEventMsgId(event), msg);
	}

	void nonemptyNamesAreUsable_data() {
		QTest::addColumn<QString>("name");
		QTest::addColumn<bool>("usable");
		QTest::newRow("group") << u"Fixture group"_s << true;
		QTest::newRow("empty") << u""_s << false;
		QTest::newRow("spaces") << u"   "_s << false;
		QTest::newRow("Unknown") << u"Unknown"_s << true;
		QTest::newRow("Unknown-padded") << u" Unknown "_s << true;
	}

	void nonemptyNamesAreUsable() {
		QFETCH(QString, name);
		QFETCH(bool, usable);
		QCOMPARE(Delta::IncomingNameUsable(name), usable);
	}

	void existingTextFastPath_data() {
		QTest::addColumn<int>("decision");
		QTest::addColumn<bool>("hasPhotoOrDocument");
		QTest::addColumn<bool>("fastPath");
		QTest::newRow("existing-text")
			<< int(Delta::IncomingApplyKind::Existing) << false << true;
		QTest::newRow("existing-photo")
			<< int(Delta::IncomingApplyKind::Existing) << true << false;
		QTest::newRow("unread-text")
			<< int(Delta::IncomingApplyKind::Unread) << false << false;
		QTest::newRow("unread-photo")
			<< int(Delta::IncomingApplyKind::Unread) << true << false;
		QTest::newRow("skip-text")
			<< int(Delta::IncomingApplyKind::Skip) << false << false;
	}

	void existingTextFastPath() {
		QFETCH(int, decision);
		QFETCH(bool, hasPhotoOrDocument);
		QFETCH(bool, fastPath);
		QCOMPARE(
			Delta::IncomingExistingTextFastPath(
				Delta::IncomingApplyKind(decision),
				hasPhotoOrDocument),
			fastPath);
	}

	void matchingBasicChatNameResolves() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto name = std::optional<QString>();
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 18, [&](QString value) {
			name = std::move(value);
		});
		QCOMPARE(rpc.methods(), QStringList{ u"get_basic_chat_info"_s });
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 3, 18 }));
		QVERIFY(rpc.replyResult(BasicChatJson(18, u"Fixture group"_s)));
		QVERIFY(name);
		QCOMPARE(*name, u"Fixture group"_s);
		expectNameOnly(rpc);
	}

	void matchingBasicChatNameUnknownResolves() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto name = std::optional<QString>();
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 18, [&](QString value) {
			name = std::move(value);
		});
		QCOMPARE(rpc.methods(), QStringList{ u"get_basic_chat_info"_s });
		QVERIFY(rpc.replyResult(BasicChatJson(18, u"Unknown"_s)));
		QVERIFY(name);
		QCOMPARE(*name, u"Unknown"_s);
		expectNameOnly(rpc);
	}

	void matchingBasicChatNameUnknownPaddedResolves() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto name = std::optional<QString>();
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 18, [&](QString value) {
			name = std::move(value);
		});
		QVERIFY(rpc.replyResult(BasicChatJson(18, u" Unknown "_s)));
		QVERIFY(name);
		QCOMPARE(*name, u"Unknown"_s);
		expectNameOnly(rpc);
	}

	void nameLookupRejectsMismatchErrorAndEmpty_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("id-mismatch")
			<< QJsonObject{ { u"result"_s, BasicChatJson(17, u"Other"_s) } };
		QTest::newRow("error")
			<< QJsonObject{ { u"error"_s, u"missing"_s } };
		QTest::newRow("empty-name")
			<< QJsonObject{ { u"result"_s, BasicChatJson(18, u""_s) } };
		QTest::newRow("spaces-name")
			<< QJsonObject{ { u"result"_s, BasicChatJson(18, u"   "_s) } };
		QTest::newRow("result-array")
			<< QJsonObject{ { u"result"_s, QJsonArray{ 18 } } };
		QTest::newRow("name-number")
			<< QJsonObject{
				{ u"result"_s, QJsonObject{
					{ u"id"_s, 18 },
					{ u"name"_s, 18 },
				} },
			};
	}

	void nameLookupRejectsMismatchErrorAndEmpty() {
		QFETCH(QJsonObject, reply);
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto name = std::optional<QString>();
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 18, [&](QString value) {
			name = std::move(value);
		});
		QVERIFY(rpc.reply(reply));
		QVERIFY(name);
		QVERIFY(name->isEmpty());
		expectNameOnly(rpc);
	}

	void invalidAccountOrChatDoesNotRpc() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto completed = 0;
		Delta::ReadIncomingChatName(rpc.bind(), scope, 0, 18, [&](QString name) {
			QVERIFY(name.isEmpty());
			++completed;
		});
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 0, [&](QString name) {
			QVERIFY(name.isEmpty());
			++completed;
		});
		QCOMPARE(completed, 2);
		QVERIFY(rpc.log.empty());
	}

	void nameLookupDropsAfterAccountSwitch() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadIncomingChatName(rpc.bind(), scope, 3, 18, [&](QString) {
			done = true;
			QFAIL("Name callback escaped its profile");
		});
		scope.invalidate();
		QVERIFY(rpc.replyResult(BasicChatJson(18, u"Fixture group"_s)));
		QVERIFY(!done);
		expectNameOnly(rpc);
	}

	void nameLookupNeverTouchesFreshListOrFullChat() {
		NameFixture rpc;
		auto scope = Delta::AccountScope();
		auto name = std::optional<QString>();
		Delta::ReadIncomingChatName(rpc.bind(), scope, 2, 14, [&](QString value) {
			name = std::move(value);
		});
		QVERIFY(rpc.replyResult(BasicChatJson(14, u"Request group"_s)));
		QVERIFY(name);
		QCOMPARE(*name, u"Request group"_s);
		QVERIFY(!rpc.methods().contains(u"get_fresh_msgs"_s));
		QVERIFY(!rpc.methods().contains(u"get_full_chat_by_id"_s));
		expectNameOnly(rpc);
	}
};

QTEST_GUILESS_MAIN(IncomingNotificationTests)
#include "incoming_notification_tests.moc"
