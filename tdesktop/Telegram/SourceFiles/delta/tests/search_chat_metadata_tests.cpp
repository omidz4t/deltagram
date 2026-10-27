#include "search_chat_metadata.h"

#include <QtTest/QtTest>

#include <algorithm>
#include <optional>
#include <vector>

using namespace Qt::StringLiterals;

namespace {

QJsonObject BasicChatJson(
		int id,
		const QString &name,
		const QString &type = u"Single"_s,
		QJsonValue image = QJsonValue(QJsonValue::Null),
		bool device = false) {
	auto json = QJsonObject{
		{ u"id"_s, id },
		{ u"name"_s, name },
		{ u"chatType"_s, type },
		{ u"isContactRequest"_s, false },
		{ u"isMuted"_s, false },
		{ u"isDeviceChat"_s, device },
		{ u"isSelfTalk"_s, false },
	};
	json.insert(u"profileImage"_s, std::move(image));
	return json;
}

struct MetadataFixture {
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

	bool replyFor(int chat, QJsonObject envelope) {
		for (auto i = pending.begin(); i != pending.end(); ++i) {
			if (i->params.size() >= 2 && i->params.at(1).toInt() == chat) {
				auto done = std::move(i->done);
				pending.erase(i);
				done(std::move(envelope));
				return true;
			}
		}
		return false;
	}

	bool replyResult(QJsonValue value) {
		return reply({ { u"result"_s, std::move(value) } });
	}

	bool replyError(QJsonValue error = u"interrupted"_s) {
		return reply({ { u"error"_s, std::move(error) } });
	}
};

void expectSearchOnly(const MetadataFixture &rpc) {
	for (const auto &call : rpc.log) {
		QCOMPARE(call.method, u"get_basic_chat_info"_s);
		QVERIFY(call.method != u"get_full_chat_by_id"_s);
		QVERIFY(call.method != u"get_fresh_msgs"_s);
		QVERIFY(call.method != u"can_send"_s);
		QVERIFY(call.method != u"why_cant_send"_s);
		QVERIFY(call.method != u"is_chat_muted"_s);
		QVERIFY(call.method != u"accept_chat"_s);
		QVERIFY(call.method != u"block_chat"_s);
		QVERIFY(call.method != u"block_contact"_s);
		QVERIFY(call.method != u"send_msg"_s);
		QVERIFY(call.method != u"create_chat_by_contact_id"_s);
		QVERIFY(call.method != u"get_chatlist_entries"_s);
		QVERIFY(call.method != u"search_messages"_s);
		QVERIFY(call.method != u"get_message"_s);
		QCOMPARE(call.params.size(), 2);
		QVERIFY(call.params.at(0).toInt() > 0);
		QVERIFY(call.params.at(1).toInt() > 9);
	}
}

[[nodiscard]] bool hasFailed(
		const Delta::SearchChatMetadataResult &result,
		int chat) {
	return std::find(
		result.failedIds.begin(),
		result.failedIds.end(),
		chat) != result.failedIds.end();
}

[[nodiscard]] bool hasName(
		const Delta::SearchChatMetadataResult &result,
		const QString &name) {
	for (auto i = result.chats.cbegin(); i != result.chats.cend(); ++i) {
		if (i.value().name == name) {
			return true;
		}
	}
	return false;
}

} // namespace

class SearchChatMetadataTests final : public QObject {
	Q_OBJECT

private slots:
	void realChatIdsAreAboveReserved() {
		QVERIFY(!Delta::CoreSearchChatId(0));
		QVERIFY(!Delta::CoreSearchChatId(1));
		QVERIFY(!Delta::CoreSearchChatId(6));
		QVERIFY(!Delta::CoreSearchChatId(9));
		QVERIFY(!Delta::CoreSearchChatId(-1));
		QVERIFY(Delta::CoreSearchChatId(10));
		QVERIFY(Delta::CoreSearchChatId(11));
	}

	void nonemptyNamesAreUsable_data() {
		QTest::addColumn<QString>("name");
		QTest::addColumn<bool>("usable");
		QTest::newRow("device") << u"Device messages"_s << true;
		QTest::newRow("empty") << u""_s << false;
		QTest::newRow("spaces") << u"   "_s << false;
		QTest::newRow("Unknown") << u"Unknown"_s << true;
		QTest::newRow("Unknown-padded") << u" Unknown "_s << true;
	}

	void nonemptyNamesAreUsable() {
		QFETCH(QString, name);
		QFETCH(bool, usable);
		QCOMPARE(Delta::SearchChatNameUsable(name), usable);
	}

	void multipleHitsOneChatDedup() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 11, 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QCOMPARE(rpc.methods(), QStringList{ u"get_basic_chat_info"_s });
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 3, 11 }));
		QVERIFY(rpc.replyResult(BasicChatJson(
			11,
			u"Device messages"_s,
			u"Single"_s,
			u"/tmp/device.png"_s,
			true)));
		QVERIFY(result);
		QCOMPARE(int(result->chats.size()), 1);
		QVERIFY(result->chats.contains(11));
		QCOMPARE(result->chats.value(11).chatId, 11);
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QCOMPARE(result->chats.value(11).avatar, u"/tmp/device.png"_s);
		QVERIFY(result->failedIds.empty());
		QVERIFY(result->error.isEmpty());
		{
			const auto type = Delta::SearchChatKnownType(result->chats.value(11));
			QVERIFY(type.has_value());
			QCOMPARE(*type, Delta::CoreChatType::Single);
		}
		QVERIFY(Delta::CoreJsonMarksDeviceTalk(result->chats.value(11).json));
		expectSearchOnly(rpc);
	}

	void twoChatsNamesMappedByRequestedId() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 18 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_basic_chat_info"_s,
			u"get_basic_chat_info"_s,
		}));
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 3, 11 }));
		QCOMPARE(rpc.log[1].params, (QJsonArray{ 3, 18 }));
		QVERIFY(rpc.replyFor(18, {
			{ u"result"_s, BasicChatJson(
				18,
				u"Fixture group"_s,
				u"Group"_s,
				u"/tmp/group.png"_s) },
		}));
		QVERIFY(!result);
		QVERIFY(rpc.replyFor(11, {
			{ u"result"_s, BasicChatJson(
				11,
				u"Device messages"_s,
				u"Single"_s,
				u"/tmp/device.png"_s,
				true) },
		}));
		QVERIFY(result);
		QCOMPARE(int(result->chats.size()), 2);
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QCOMPARE(result->chats.value(11).avatar, u"/tmp/device.png"_s);
		QCOMPARE(result->chats.value(18).name, u"Fixture group"_s);
		QCOMPARE(result->chats.value(18).avatar, u"/tmp/group.png"_s);
		{
			const auto deviceType = Delta::SearchChatKnownType(
				result->chats.value(11));
			QVERIFY(deviceType.has_value());
			QCOMPARE(*deviceType, Delta::CoreChatType::Single);
			const auto groupType = Delta::SearchChatKnownType(
				result->chats.value(18));
			QVERIFY(groupType.has_value());
			QCOMPARE(*groupType, Delta::CoreChatType::Group);
		}
		QVERIFY(result->failedIds.empty());
		QVERIFY(result->error.isEmpty());
		expectSearchOnly(rpc);
	}

	void failClosedEnvelope_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("id-mismatch")
			<< QJsonObject{ { u"result"_s, BasicChatJson(17, u"Other"_s) } };
		QTest::newRow("error")
			<< QJsonObject{ { u"error"_s, u"missing"_s } };
		QTest::newRow("error-and-result")
			<< QJsonObject{
				{ u"error"_s, u"missing"_s },
				{ u"result"_s, BasicChatJson(11, u"Device messages"_s) },
			};
		QTest::newRow("empty-name")
			<< QJsonObject{ { u"result"_s, BasicChatJson(11, u""_s) } };
		QTest::newRow("spaces-name")
			<< QJsonObject{ { u"result"_s, BasicChatJson(11, u"   "_s) } };
		QTest::newRow("result-array")
			<< QJsonObject{ { u"result"_s, QJsonArray{ 11 } } };
		QTest::newRow("result-string")
			<< QJsonObject{ { u"result"_s, u"Device messages"_s } };
		QTest::newRow("result-null")
			<< QJsonObject{ { u"result"_s, QJsonValue(QJsonValue::Null) } };
		QTest::newRow("missing-result")
			<< QJsonObject{};
		QTest::newRow("name-number")
			<< QJsonObject{
				{ u"result"_s, QJsonObject{
					{ u"id"_s, 11 },
					{ u"name"_s, 11 },
				} },
			};
		QTest::newRow("id-string")
			<< QJsonObject{
				{ u"result"_s, QJsonObject{
					{ u"id"_s, u"11"_s },
					{ u"name"_s, u"Device messages"_s },
				} },
			};
	}

	void failClosedEnvelope() {
		QFETCH(QJsonObject, reply);
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.reply(reply));
		QVERIFY(result);
		QVERIFY(result->chats.isEmpty());
		QVERIFY(hasFailed(*result, 11));
		QCOMPARE(result->error, Delta::kSearchChatMetadataUnavailable);
		QVERIFY(!hasName(*result, u"Other"_s));
		QVERIFY(!hasName(*result, u"Device messages"_s));
		expectSearchOnly(rpc);
	}

	void mismatchedIdDoesNotAssignTheOtherChat() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 18 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyFor(11, {
			{ u"result"_s, BasicChatJson(18, u"Hijack"_s, u"Group"_s) },
		}));
		QVERIFY(!result);
		QVERIFY(rpc.replyFor(18, {
			{ u"result"_s, BasicChatJson(18, u"Fixture group"_s, u"Group"_s) },
		}));
		QVERIFY(result);
		QVERIFY(!result->chats.contains(11));
		QVERIFY(hasFailed(*result, 11));
		QVERIFY(!hasFailed(*result, 18));
		QCOMPARE(result->chats.value(18).name, u"Fixture group"_s);
		QVERIFY(!hasName(*result, u"Hijack"_s));
		QCOMPARE(result->error, Delta::kSearchChatMetadataUnavailable);
		expectSearchOnly(rpc);
	}

	void legitUnknownNameAllowed() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyResult(BasicChatJson(11, u"Unknown"_s)));
		QVERIFY(result);
		QCOMPARE(result->chats.value(11).name, u"Unknown"_s);
		QVERIFY(result->failedIds.empty());
		QVERIFY(result->error.isEmpty());
		expectSearchOnly(rpc);
	}

	void legitUnknownPaddedNameAllowed() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyResult(BasicChatJson(11, u" Unknown "_s)));
		QVERIFY(result);
		QCOMPARE(result->chats.value(11).name, u"Unknown"_s);
		QVERIFY(result->failedIds.empty());
		expectSearchOnly(rpc);
	}

	void partialFailureKeepsValidChat() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 18 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyFor(11, {
			{ u"result"_s, BasicChatJson(
				11,
				u"Device messages"_s,
				u"Single"_s,
				QJsonValue(QJsonValue::Null),
				true) },
		}));
		QVERIFY(!result);
		QVERIFY(rpc.replyFor(18, { { u"error"_s, u"offline"_s } }));
		QVERIFY(result);
		QCOMPARE(int(result->chats.size()), 1);
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QVERIFY(!result->chats.contains(18));
		QVERIFY(hasFailed(*result, 18));
		QVERIFY(!hasFailed(*result, 11));
		QCOMPARE(result->error, Delta::kSearchChatMetadataUnavailable);
		expectSearchOnly(rpc);
	}

	void unknownChatTypeIsNotPositiveClassification() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyResult(BasicChatJson(
			11,
			u"Device messages"_s,
			u"FutureType"_s,
			QJsonValue(QJsonValue::Null),
			true)));
		QVERIFY(result);
		QVERIFY(result->chats.contains(11));
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QVERIFY(!Delta::SearchChatKnownType(result->chats.value(11)));
		const auto membership = Delta::CoreChatMembershipFromJson(
			result->chats.value(11).json,
			11);
		QVERIFY(membership);
		QVERIFY(!membership->direct.has_value());
		QVERIFY(!membership->group.has_value());
		QVERIFY(membership->deviceTalk);
		expectSearchOnly(rpc);
	}

	void staleAccountSwitchDropsCallback() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 18 },
			[&](Delta::SearchChatMetadataResult) {
				done = true;
				QFAIL("Search metadata callback escaped its profile");
			});
		scope.invalidate();
		QVERIFY(rpc.replyFor(11, {
			{ u"result"_s, BasicChatJson(11, u"Device messages"_s) },
		}));
		QVERIFY(rpc.replyFor(18, {
			{ u"result"_s, BasicChatJson(18, u"Fixture group"_s, u"Group"_s) },
		}));
		QVERIFY(!done);
		expectSearchOnly(rpc);
	}

	void firstDispatchInvalidationStopsLaterChats() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		const auto inner = rpc.bind();
		Delta::ReadSearchChatMetadata(
			[&](QString method, QJsonArray params, DeltaRpc::Callback callback) {
				inner(std::move(method), std::move(params), std::move(callback));
				if (rpc.log.size() == 1) {
					scope.invalidate();
				}
			},
			scope,
			3,
			{ 11, 18 },
			[&](Delta::SearchChatMetadataResult) {
				done = true;
				QFAIL("Search metadata callback escaped its start generation");
			});
		QCOMPARE(int(rpc.log.size()), 1);
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 3, 11 }));
		QCOMPARE(int(rpc.pending.size()), 1);
		QVERIFY(rpc.replyResult(BasicChatJson(11, u"Device messages"_s)));
		QVERIFY(!done);
		QCOMPARE(int(rpc.log.size()), 1);
		QVERIFY(rpc.pending.empty());
		expectSearchOnly(rpc);
	}

	void requestMethodAllowlistGetBasicChatInfoOnly() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11, 18, 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QCOMPARE(int(rpc.log.size()), 2);
		QVERIFY(rpc.replyFor(11, {
			{ u"result"_s, BasicChatJson(11, u"Device messages"_s) },
		}));
		QVERIFY(rpc.replyFor(18, {
			{ u"result"_s, BasicChatJson(18, u"Fixture group"_s, u"Group"_s) },
		}));
		QVERIFY(result);
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_basic_chat_info"_s,
			u"get_basic_chat_info"_s,
		}));
		expectSearchOnly(rpc);
	}

	void reservedInvalidInputNoRpc() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto completed = 0;
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			0,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				QVERIFY(value.chats.isEmpty());
				QVERIFY(hasFailed(value, 11));
				QCOMPARE(value.error, Delta::kSearchChatMetadataUnavailable);
				++completed;
			});
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 0, 1, 6, 9, -1 },
			[&](Delta::SearchChatMetadataResult value) {
				QVERIFY(value.chats.isEmpty());
				QCOMPARE(int(value.failedIds.size()), 5);
				QVERIFY(hasFailed(value, 0));
				QVERIFY(hasFailed(value, 1));
				QVERIFY(hasFailed(value, 6));
				QVERIFY(hasFailed(value, 9));
				QVERIFY(hasFailed(value, -1));
				QCOMPARE(value.error, Delta::kSearchChatMetadataUnavailable);
				++completed;
			});
		QCOMPARE(completed, 2);
		QVERIFY(rpc.log.empty());
	}

	void reservedMixedWithValidOnlyReadsRealChat() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 9, 11, 6, 11, 0 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QCOMPARE(rpc.methods(), QStringList{ u"get_basic_chat_info"_s });
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 3, 11 }));
		QVERIFY(rpc.replyResult(BasicChatJson(11, u"Device messages"_s)));
		QVERIFY(result);
		QCOMPARE(int(result->chats.size()), 1);
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QVERIFY(hasFailed(*result, 9));
		QVERIFY(hasFailed(*result, 6));
		QVERIFY(hasFailed(*result, 0));
		QVERIFY(!hasFailed(*result, 11));
		QCOMPARE(int(result->failedIds.size()), 3);
		expectSearchOnly(rpc);
	}

	void emptyInputNoRpc() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{},
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(result);
		QVERIFY(result->chats.isEmpty());
		QVERIFY(result->failedIds.empty());
		QVERIFY(result->error.isEmpty());
		QVERIFY(rpc.log.empty());
	}

	void pageLimitCapsUniqueRpcs() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		auto ids = std::vector<int>();
		for (auto chat = 10; chat <= 10 + Delta::kSearchChatMetadataPageLimit; ++chat) {
			ids.push_back(chat);
		}
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			ids,
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QCOMPARE(int(rpc.log.size()), Delta::kSearchChatMetadataPageLimit);
		for (auto i = 0; i != Delta::kSearchChatMetadataPageLimit; ++i) {
			const auto chat = 10 + i;
			QVERIFY(rpc.replyFor(chat, {
				{ u"result"_s, BasicChatJson(chat, u"Chat %1"_s.arg(chat)) },
			}));
		}
		QVERIFY(result);
		QCOMPARE(int(result->chats.size()), Delta::kSearchChatMetadataPageLimit);
		QCOMPARE(int(result->failedIds.size()), 1);
		QCOMPARE(
			result->failedIds.front(),
			10 + Delta::kSearchChatMetadataPageLimit);
		QCOMPARE(result->error, Delta::kSearchChatMetadataUnavailable);
		expectSearchOnly(rpc);
	}

	void numericProfileImageDoesNotManufactureNameFailure() {
		MetadataFixture rpc;
		auto scope = Delta::AccountScope();
		auto result = std::optional<Delta::SearchChatMetadataResult>();
		Delta::ReadSearchChatMetadata(
			rpc.bind(),
			scope,
			3,
			{ 11 },
			[&](Delta::SearchChatMetadataResult value) {
				result = std::move(value);
			});
		QVERIFY(rpc.replyResult(BasicChatJson(
			11,
			u"Device messages"_s,
			u"Single"_s,
			42,
			true)));
		QVERIFY(result);
		QCOMPARE(result->chats.value(11).name, u"Device messages"_s);
		QVERIFY(result->chats.value(11).avatar.isEmpty());
		QVERIFY(result->failedIds.empty());
		expectSearchOnly(rpc);
	}

	void parserRejectsReservedChatWithoutUsingJsonName() {
		const auto reply = QJsonObject{
			{ u"result"_s, BasicChatJson(6, u"Archive"_s) },
		};
		QVERIFY(!Delta::SearchChatMetadataFromReply(reply, 6));
		QVERIFY(!Delta::SearchChatMetadataFromReply(reply, 11));
	}
};

QTEST_GUILESS_MAIN(SearchChatMetadataTests)
#include "search_chat_metadata_tests.moc"
