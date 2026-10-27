#include "group_info.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject FullChatJson(
		int id,
		const QString &type = u"Group"_s,
		QJsonArray contacts = QJsonArray{ 1, 10 },
		const QString &name = u"Integrity group"_s,
		QJsonValue muted = true,
		QJsonValue selfTalk = false) {
	auto json = QJsonObject{
		{ u"id"_s, id },
		{ u"chatType"_s, type },
		{ u"name"_s, name },
		{ u"contactIds"_s, std::move(contacts) },
	};
	if (!muted.isUndefined()) {
		json.insert(u"isMuted"_s, muted);
	}
	if (!selfTalk.isUndefined()) {
		json.insert(u"isSelfTalk"_s, selfTalk);
	}
	return json;
}

QJsonObject ContactJson(
		int id,
		const QString &name = u"Integrity sender profile20261026"_s,
		const QString &address = u"sender@example.test"_s) {
	return {
		{ u"id"_s, id },
		{ u"displayName"_s, name },
		{ u"address"_s, address },
		{ u"profileImage"_s, QString() },
		{ u"lastSeen"_s, 0 },
	};
}

struct GroupFixture {
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

class GroupInfoTests final : public QObject {
	Q_OBJECT

	void expectAllowed(const GroupFixture &rpc) {
		const auto allowed = QStringList{
			u"get_full_chat_by_id"_s,
			u"get_contact"_s,
		};
		for (const auto &call : rpc.log) {
			QVERIFY2(allowed.contains(call.method), qPrintable(call.method));
			QVERIFY(call.method != u"create_chat_by_contact_id"_s);
			QVERIFY(call.method != u"get_chat_id_by_contact_id"_s);
			QVERIFY(call.method != u"accept_chat"_s);
			QVERIFY(call.method != u"block_chat"_s);
			QVERIFY(call.method != u"block_contact"_s);
			QVERIFY(call.method != u"unblock_contact"_s);
			QVERIFY(call.method != u"delete_chat"_s);
			QVERIFY(call.method != u"delete_contact"_s);
			QVERIFY(call.method != u"send_msg"_s);
			QVERIFY(call.method != u"misc_send_draft"_s);
			QVERIFY(call.method != u"add_contact_to_chat"_s);
			QVERIFY(call.method != u"remove_contact_from_chat"_s);
		}
	}

	void expectParams(
			const GroupFixture &rpc,
			int index,
			const QString &method,
			QJsonArray params) {
		QVERIFY(index >= 0 && index < int(rpc.log.size()));
		QCOMPARE(rpc.log[index].method, method);
		QCOMPARE(rpc.log[index].params, params);
	}

private slots:
	void groupIdAndTypeValidation_data() {
		QTest::addColumn<QJsonObject>("json");
		QTest::addColumn<int>("expected");
		QTest::addColumn<int>("kind");
		QTest::newRow("group-13")
			<< FullChatJson(13)
			<< 13
			<< int(Delta::CoreGroupRead::Group);
		QTest::newRow("id-mismatch")
			<< FullChatJson(12)
			<< 13
			<< int(Delta::CoreGroupRead::Invalid);
		QTest::newRow("single")
			<< FullChatJson(13, u"Single"_s, { 10 })
			<< 13
			<< int(Delta::CoreGroupRead::NotGroup);
		QTest::newRow("out-broadcast")
			<< FullChatJson(13, u"OutBroadcast"_s, { 1 })
			<< 13
			<< int(Delta::CoreGroupRead::NotGroup);
		QTest::newRow("in-broadcast")
			<< FullChatJson(13, u"InBroadcast"_s, { 1 })
			<< 13
			<< int(Delta::CoreGroupRead::NotGroup);
		QTest::newRow("mailinglist")
			<< FullChatJson(13, u"Mailinglist"_s, { 1 })
			<< 13
			<< int(Delta::CoreGroupRead::NotGroup);
		QTest::newRow("unknown-type")
			<< FullChatJson(13, u"Foo"_s)
			<< 13
			<< int(Delta::CoreGroupRead::Invalid);
		QTest::newRow("self-talk")
			<< FullChatJson(13, u"Group"_s, { 1, 10 }, u"G"_s, true, true)
			<< 13
			<< int(Delta::CoreGroupRead::Invalid);
		QTest::newRow("reserved-chat")
			<< FullChatJson(5)
			<< 5
			<< int(Delta::CoreGroupRead::Invalid);
	}

	void groupIdAndTypeValidation() {
		QFETCH(QJsonObject, json);
		QFETCH(int, expected);
		QFETCH(int, kind);
		const auto parsed = Delta::CoreGroupChatFromJson(
			json,
			std::uint64_t(expected));
		QCOMPARE(int(parsed.kind), kind);
		if (parsed.kind == Delta::CoreGroupRead::Group) {
			QCOMPARE(parsed.group.id, expected);
			QCOMPARE(int(parsed.group.contactIds.size()), 2);
			QCOMPARE(parsed.group.contactIds[0], 1);
			QCOMPARE(parsed.group.contactIds[1], 10);
		} else {
			QCOMPARE(parsed.group.id, 0);
		}
	}

	void malformedMembersCannotBecomeChatIds_data() {
		QTest::addColumn<QJsonArray>("contacts");
		QTest::newRow("fraction") << QJsonArray{ 1, 10.5 };
		QTest::newRow("string") << QJsonArray{ 1, u"10"_s };
		QTest::newRow("zero") << QJsonArray{ 1, 0 };
		QTest::newRow("negative") << QJsonArray{ 1, -10 };
		QTest::newRow("duplicate") << QJsonArray{ 1, 10, 10 };
		QTest::newRow("bool") << QJsonArray{ 1, true };
		QTest::newRow("null") << QJsonArray{ 1, QJsonValue() };
	}

	void malformedMembersCannotBecomeChatIds() {
		QFETCH(QJsonArray, contacts);
		const auto parsed = Delta::CoreGroupChatFromJson(
			FullChatJson(13, u"Group"_s, contacts),
			13);
		QCOMPARE(parsed.kind, Delta::CoreGroupRead::Invalid);
		QVERIFY(parsed.group.contactIds.empty());
		QVERIFY(!Delta::CoreGroupMemberProfileUserId(0));
		QVERIFY(!Delta::CoreGroupMemberProfileUserId(-10));
		QVERIFY(!Delta::CoreChatUserId(
			Delta::kContactUserIdBase + 10));
	}

	void memberProfileIsSenderIdentityNotChatId() {
		QCOMPARE(
			Delta::CoreGroupClassifyMember(1),
			Delta::CoreGroupMemberKind::Self);
		QCOMPARE(
			Delta::CoreGroupClassifyMember(2),
			Delta::CoreGroupMemberKind::Reserved);
		QCOMPARE(
			Delta::CoreGroupClassifyMember(5),
			Delta::CoreGroupMemberKind::Reserved);
		QCOMPARE(
			Delta::CoreGroupClassifyMember(9),
			Delta::CoreGroupMemberKind::Reserved);
		QCOMPARE(
			Delta::CoreGroupClassifyMember(10),
			Delta::CoreGroupMemberKind::Peer);
		QVERIFY(!Delta::CoreGroupMemberProfileUserId(1));
		const auto profile = Delta::CoreGroupMemberProfileUserId(10);
		QVERIFY(profile);
		QCOMPARE(*profile, Delta::kContactUserIdBase + 10);
		QVERIFY(Delta::IsSenderUserId(*profile));
		QVERIFY(!Delta::CoreChatUserId(*profile));
		QVERIFY(!Delta::CoreIdFitsInt(*profile));
		QVERIFY(Delta::CoreGroupChatId(13));
		QVERIFY(!Delta::CoreJsonIdMatches(
			QJsonObject{ { u"id"_s, 10 } },
			*profile));
		QVERIFY(!Delta::CoreJsonIdMatches(
			QJsonObject{ { u"id"_s, 13 } },
			*profile));
		QVERIFY(Delta::CoreGroupChatId(10));
		const auto sameNumber = Delta::CoreGroupMemberProfileUserId(13);
		QVERIFY(sameNumber);
		QCOMPARE(*sameNumber, Delta::kContactUserIdBase + 13);
		QVERIFY(*sameNumber != std::uint64_t(13));
		QVERIFY(!Delta::CoreChatUserId(*sameNumber));
	}

	void explicitMessageResolvesContactForPeerIdentity() {
		const auto minted = Delta::CoreGroupMemberProfileUserId(10);
		QVERIFY(minted);
		QCOMPARE(*minted, Delta::kContactUserIdBase + 10);
		const auto contact = Delta::CoreGroupMemberFromContact(
			{ { u"result"_s, ContactJson(10) } },
			10);
		QVERIFY(contact);
		QCOMPARE(contact->contactId, 10);
		QCOMPARE(contact->profileUserId, *minted);
		QCOMPARE(contact->kind, Delta::CoreGroupMemberKind::Peer);
		QVERIFY(contact->profileUserId != std::uint64_t(10));
		QVERIFY(contact->profileUserId != std::uint64_t(13));
		QVERIFY(Delta::IsSenderUserId(contact->profileUserId));
		QVERIFY(!Delta::CoreChatUserId(contact->profileUserId));
	}

	void readMatchingGroup() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupReadResult, QString>>();
		Delta::ReadCoreGroupChat(
			rpc.bind(),
			scope,
			3,
			13,
			[&](Delta::CoreGroupReadResult read, QString error) {
				result = std::pair<Delta::CoreGroupReadResult, QString>(
					std::move(read),
					std::move(error));
			});
		expectParams(rpc, 0, u"get_full_chat_by_id"_s, { 3, 13 });
		QVERIFY(rpc.replyResult(FullChatJson(13)));
		QVERIFY(result);
		QCOMPARE(result->first.kind, Delta::CoreGroupRead::Group);
		QCOMPARE(result->first.group.id, 13);
		QCOMPARE(result->first.group.name, u"Integrity group"_s);
		QCOMPARE(int(result->first.group.contactIds.size()), 2);
		QCOMPARE(result->first.group.contactIds[0], 1);
		QCOMPARE(result->first.group.contactIds[1], 10);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(rpc.methods(), QStringList{ u"get_full_chat_by_id"_s });
		expectAllowed(rpc);
	}

	void readWrongTypeIsNotGroup() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupReadResult, QString>>();
		Delta::ReadCoreGroupChat(rpc.bind(), scope, 3, 13, [&](
				Delta::CoreGroupReadResult read, QString error) {
			result = { std::move(read), std::move(error) };
		});
		QVERIFY(rpc.replyResult(FullChatJson(13, u"OutBroadcast"_s, { 1 })));
		QVERIFY(result);
		QCOMPARE(result->first.kind, Delta::CoreGroupRead::NotGroup);
		QCOMPARE(result->second, Delta::kGroupInfoNotGroup);
		expectAllowed(rpc);
	}

	void inspectPeerReadsFullThenContact() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(),
			scope,
			3,
			13,
			10,
			[&](Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		expectParams(rpc, 0, u"get_full_chat_by_id"_s, { 3, 13 });
		QVERIFY(rpc.replyResult(FullChatJson(13)));
		expectParams(rpc, 1, u"get_contact"_s, { 3, 10 });
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(result);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(result->first.contactId, 10);
		QCOMPARE(result->first.kind, Delta::CoreGroupMemberKind::Peer);
		QCOMPARE(result->first.profileUserId, Delta::kContactUserIdBase + 10);
		QCOMPARE(result->first.name, u"Integrity sender profile20261026"_s);
		QVERIFY(!Delta::CoreChatUserId(result->first.profileUserId));
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_full_chat_by_id"_s,
			u"get_contact"_s,
		}));
		expectAllowed(rpc);
	}

	void inspectSelfDoesNotMintChatId() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 1, [&](
				Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		QVERIFY(rpc.replyResult(FullChatJson(13)));
		expectParams(rpc, 1, u"get_contact"_s, { 3, 1 });
		QVERIFY(rpc.replyResult(ContactJson(1, u"Me"_s, u"me@example.test"_s)));
		QVERIFY(result);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(result->first.contactId, 1);
		QCOMPARE(result->first.kind, Delta::CoreGroupMemberKind::Self);
		QCOMPARE(result->first.profileUserId, std::uint64_t(0));
		expectAllowed(rpc);
	}

	void inspectReservedDoesNotBecomeChatId() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 2, [&](
				Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		QVERIFY(rpc.replyResult(FullChatJson(13, u"Group"_s, { 1, 2, 10 })));
		expectParams(rpc, 1, u"get_contact"_s, { 3, 2 });
		QVERIFY(rpc.replyResult(ContactJson(2, u"Info"_s, QString())));
		QVERIFY(result);
		QVERIFY(result->second.isEmpty());
		QCOMPARE(result->first.contactId, 2);
		QCOMPARE(result->first.kind, Delta::CoreGroupMemberKind::Reserved);
		QCOMPARE(result->first.profileUserId, Delta::kContactUserIdBase + 2);
		QVERIFY(!Delta::CoreChatUserId(result->first.profileUserId));
		QVERIFY(result->first.profileUserId != std::uint64_t(2));
		QVERIFY(result->first.profileUserId != std::uint64_t(13));
		expectAllowed(rpc);
	}

	void inspectMissingMemberDoesNotGetContact() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 11, [&](
				Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		QVERIFY(rpc.replyResult(FullChatJson(13)));
		QVERIFY(result);
		QCOMPARE(result->first.contactId, 0);
		QCOMPARE(result->second, Delta::kGroupMemberUnknown);
		QVERIFY(!rpc.has(u"get_contact"_s));
		expectAllowed(rpc);
	}

	void invalidMemberDoesNotRpc() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 0, [&](
				Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		QVERIFY(result);
		QCOMPARE(result->second, Delta::kGroupMemberUnreadable);
		QVERIFY(rpc.log.empty());
	}

	void reservedChatDoesNotRpc() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupReadResult, QString>>();
		Delta::ReadCoreGroupChat(rpc.bind(), scope, 3, 5, [&](
				Delta::CoreGroupReadResult read, QString error) {
			result = { std::move(read), std::move(error) };
		});
		QVERIFY(result);
		QCOMPARE(result->second, Delta::kGroupInfoUnavailable);
		QVERIFY(rpc.log.empty());
	}

	void staleAccountDropsCallback_data() {
		QTest::addColumn<int>("repliesBeforeInvalidate");
		QTest::newRow("before-full") << 0;
		QTest::newRow("before-contact") << 1;
	}

	void staleAccountDropsCallback() {
		QFETCH(int, repliesBeforeInvalidate);
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 10, [&](
				Delta::CoreGroupMemberProfile, QString) {
				done = true;
			});
		if (repliesBeforeInvalidate >= 1) {
			QVERIFY(rpc.replyResult(FullChatJson(13)));
		}
		scope.invalidate();
		if (repliesBeforeInvalidate == 0) {
			QVERIFY(rpc.replyResult(FullChatJson(13)));
		} else {
			QVERIFY(rpc.replyResult(ContactJson(10)));
		}
		QVERIFY(!done);
		expectAllowed(rpc);
		QVERIFY(!rpc.has(u"create_chat_by_contact_id"_s));
		QVERIFY(!rpc.has(u"accept_chat"_s));
		QVERIFY(!rpc.has(u"block_contact"_s));
		QVERIFY(!rpc.has(u"send_msg"_s));
	}

	void inspectDoesNotWrite() {
		GroupFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<std::pair<Delta::CoreGroupMemberProfile, QString>>();
		Delta::InspectCoreGroupMember(
			rpc.bind(), scope, 3, 13, 10, [&](
				Delta::CoreGroupMemberProfile profile, QString error) {
				result = { std::move(profile), std::move(error) };
			});
		QVERIFY(rpc.replyResult(FullChatJson(13)));
		QVERIFY(rpc.replyResult(ContactJson(10)));
		QVERIFY(result);
		QVERIFY(result->second.isEmpty());
		for (const auto &call : rpc.log) {
			QVERIFY(call.method.startsWith(u"get_"_s));
		}
		expectAllowed(rpc);
	}
};

QTEST_GUILESS_MAIN(GroupInfoTests)
#include "group_info_tests.moc"
