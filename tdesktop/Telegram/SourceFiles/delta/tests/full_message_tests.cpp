#include "full_message.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

QJsonObject MessageJson(
		int id,
		int chatId,
		bool hasHtml,
		const QString &text = u"Integrity HTML fallback native-a19 20261026"_s,
		const QString &subject = QString()) {
	auto json = QJsonObject{
		{ u"id"_s, id },
		{ u"chatId"_s, chatId },
		{ u"hasHtml"_s, hasHtml },
		{ u"text"_s, text },
	};
	if (!subject.isNull()) {
		json.insert(u"subject"_s, subject);
	}
	return json;
}

struct MessageFixture {
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

	[[nodiscard]] int htmlCalls() const {
		auto count = 0;
		for (const auto &call : log) {
			if (call.method == u"get_message_html"_s) {
				++count;
			}
		}
		return count;
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

	bool replyError(QJsonValue error) {
		return reply({ { u"error"_s, std::move(error) } });
	}
};

const auto kMarker = u"HTML-only detail marker 20261026"_s;
const auto kFallback = u"Integrity HTML fallback native-a19 20261026"_s;
const auto kHtml = u"<p>Integrity HTML fallback native-a19 20261026</p>"
	"<p>HTML-only detail marker 20261026</p>"
	"<table><tr><th>Item</th><th>Detail</th></tr>"
	"<tr><td>Integrity</td><td>HTML-only detail marker 20261026</td></tr>"
	"</table>"_s;

} // namespace

class FullMessageTests final : public QObject {
	Q_OBJECT

private slots:
	void flagRequiresBoolHasHtml() {
		QVERIFY(Delta::FullMessageFlag(MessageJson(101, 10, true), 101));
		QVERIFY(!Delta::FullMessageFlag(MessageJson(101, 10, false), 101));
		auto missing = MessageJson(101, 10, true);
		missing.remove(u"hasHtml"_s);
		QVERIFY(!Delta::FullMessageFlag(missing, 101));
		auto asString = MessageJson(101, 10, true);
		asString.insert(u"hasHtml"_s, u"true"_s);
		QVERIFY(!Delta::FullMessageFlag(asString, 101));
		auto asNumber = MessageJson(101, 10, true);
		asNumber.insert(u"hasHtml"_s, 1);
		QVERIFY(!Delta::FullMessageFlag(asNumber, 101));
		QVERIFY(!Delta::FullMessageFlag(QJsonObject{ { u"state"_s, 20 } }, 101));
	}

	void flagRequiresExactIdAndRealChat() {
		QVERIFY(!Delta::FullMessageFlag(MessageJson(102, 10, true), 101));
		QVERIFY(!Delta::FullMessageFlag(MessageJson(101, 9, true), 101));
		QVERIFY(!Delta::FullMessageFlag(MessageJson(9, 10, true), 9));
		auto stringId = MessageJson(101, 10, true);
		stringId.insert(u"id"_s, u"101"_s);
		QVERIFY(!Delta::FullMessageFlag(stringId, 101));
	}

	void sessionGateRejectsValidMetadata() {
		const auto json = MessageJson(101, 10, true);
		QVERIFY(Delta::FullMessageFlag(json, 101));
		QVERIFY(!Delta::FullMessageSessionAllows(false, json, 101));
		QVERIFY(Delta::FullMessageSessionAllows(true, json, 101));
		QVERIFY(!Delta::FullMessageSessionAllows(
			true,
			MessageJson(101, 10, false),
			101));
	}

	void subjectBecomesTitleOnly() {
		QCOMPARE(
			Delta::FullMessageTitleFromMetadata(MessageJson(101, 10, true)),
			QString());
		auto json = MessageJson(
			101,
			10,
			true,
			kFallback,
			u"Re: classic mail title 20261026"_s);
		QCOMPARE(
			Delta::FullMessageTitleFromMetadata(json),
			u"Re: classic mail title 20261026"_s);
		json.insert(u"subject"_s, u"  line\nbreak\tmail  "_s);
		QCOMPARE(
			Delta::FullMessageTitleFromMetadata(json),
			u"line break mail"_s);
		json.insert(u"subject"_s, 12);
		QVERIFY(Delta::FullMessageTitleFromMetadata(json).isEmpty());
		json.insert(
			u"subject"_s,
			QString(QChar(0x202E)) + u"mail title"_s + QString(QChar(0x202C)));
		QCOMPARE(
			Delta::FullMessageTitleFromMetadata(json),
			u"mail title"_s);
		json.insert(u"subject"_s, QString(QChar(0x202E)));
		QVERIFY(Delta::FullMessageTitleFromMetadata(json).isEmpty());
		json.insert(
			u"subject"_s,
			QString(Delta::kFullMessageTitleLimit + 12, u'A'));
		QCOMPARE(
			Delta::FullMessageTitleFromMetadata(json).size(),
			Delta::kFullMessageTitleLimit);
	}

	void invalidIdsSkipRpc() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		auto last = Delta::FullMessageResult();
		const auto capture = [&](Delta::FullMessageResult result) {
			++done;
			last = std::move(result);
		};
		Delta::ReadFullMessage(rpc.bind(), scope, 0, 101, 10, capture);
		Delta::ReadFullMessage(rpc.bind(), scope, 1, 9, 10, capture);
		Delta::ReadFullMessage(rpc.bind(), scope, 1, 101, 9, capture);
		Delta::ReadFullMessage(rpc.bind(), scope, -1, 101, 10, capture);
		QCOMPARE(done, 4);
		QCOMPARE(int(rpc.log.size()), 0);
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unavailable);
		QCOMPARE(last.error, Delta::kFullMessageUnavailable);
	}

	void happyPathExactAccountAndMessage() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) {
				++done;
				last = std::move(result);
			});
		QCOMPARE(rpc.methods(), (QStringList{ u"get_message"_s }));
		QCOMPARE(rpc.log[0].params, (QJsonArray{ 1, 101 }));
		QVERIFY(rpc.replyResult(MessageJson(
			101,
			10,
			true,
			kFallback,
			u"Re: classic mail title 20261026"_s)));
		QCOMPARE(rpc.methods(), (QStringList{
			u"get_message"_s,
			u"get_message_html"_s,
		}));
		QCOMPARE(rpc.log[1].params, (QJsonArray{ 1, 101 }));
		QVERIFY(rpc.replyResult(kHtml));
		QCOMPARE(done, 1);
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Ready);
		QVERIFY(last.html.contains(kMarker));
		QCOMPARE(last.fallback, kFallback);
		QCOMPARE(last.title, u"Re: classic mail title 20261026"_s);
		QVERIFY(!last.html.contains(u"Re: classic mail title 20261026"_s));
		QVERIFY(!last.fallback.contains(u"Re: classic mail title 20261026"_s));
		QVERIFY(last.error.isEmpty());
		QCOMPARE(rpc.htmlCalls(), 1);
	}

	void idMismatchSkipsHtml() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) {
				++done;
				last = std::move(result);
			});
		QVERIFY(rpc.replyResult(MessageJson(102, 10, true)));
		QCOMPARE(done, 1);
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unavailable);
		QCOMPARE(last.error, Delta::kFullMessageUnavailable);
		QCOMPARE(rpc.htmlCalls(), 0);
	}

	void chatMismatchSkipsHtml() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) {
				++done;
				last = std::move(result);
			});
		QVERIFY(rpc.replyResult(MessageJson(101, 11, true)));
		QCOMPARE(done, 1);
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unavailable);
		QCOMPARE(rpc.htmlCalls(), 0);
	}

	void hasHtmlTypesSkipHtml() {
		auto run = [](QJsonValue hasHtml) {
			MessageFixture rpc;
			auto scope = Delta::AccountScope();
			auto done = 0;
			auto last = Delta::FullMessageResult();
			Delta::ReadFullMessage(
				rpc.bind(),
				scope,
				1,
				101,
				10,
				[&](Delta::FullMessageResult result) {
					++done;
					last = std::move(result);
				});
			auto json = MessageJson(101, 10, true);
			if (hasHtml.isUndefined()) {
				json.remove(u"hasHtml"_s);
			} else {
				json.insert(u"hasHtml"_s, hasHtml);
			}
			QVERIFY(rpc.replyResult(json));
			QCOMPARE(done, 1);
			QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unavailable);
			QCOMPARE(rpc.htmlCalls(), 0);
		};
		run(false);
		run(u"true"_s);
		run(1);
		run(QJsonValue(QJsonValue::Undefined));
	}

	void ordinaryFalseMetadataSkipsHtml() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult) { ++done; });
		QVERIFY(rpc.replyResult(MessageJson(101, 10, false)));
		QCOMPARE(done, 1);
		QCOMPARE(rpc.methods(), (QStringList{ u"get_message"_s }));
		QCOMPARE(rpc.htmlCalls(), 0);
	}

	void metadataErrorRedactsCore() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) {
				++done;
				last = std::move(result);
			});
		QVERIFY(rpc.replyError(QJsonObject{
			{ u"code"_s, -1 },
			{ u"message"_s, u"SQL explosion at /secret/core.db"_s },
		}));
		QCOMPARE(done, 1);
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unreadable);
		QCOMPARE(last.error, Delta::kFullMessageUnreadable);
		QVERIFY(!last.error.contains(u"SQL"_s));
		QVERIFY(!last.error.contains(u"secret"_s));
		QCOMPARE(rpc.htmlCalls(), 0);
	}

	void htmlNullIsEmpty() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) { last = std::move(result); });
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Empty);
		QCOMPARE(last.error, Delta::kFullMessageEmpty);
		QCOMPARE(last.fallback, kFallback);
	}

	void htmlEmptyStringIsEmpty() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) { last = std::move(result); });
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(rpc.replyResult(u""_s));
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Empty);
		QCOMPARE(last.error, Delta::kFullMessageEmpty);
	}

	void htmlNonStringIsMalformed() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) { last = std::move(result); });
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(rpc.replyResult(QJsonObject{ { u"html"_s, kHtml } }));
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Malformed);
		QCOMPARE(last.error, Delta::kFullMessageMalformed);
		QVERIFY(!last.error.contains(u"html"_s));
	}

	void htmlErrorRedactsCore() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto last = Delta::FullMessageResult();
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult result) { last = std::move(result); });
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(rpc.replyError(u"blob missing at /var/core/secret"_s));
		QCOMPARE(last.outcome, Delta::FullMessageOutcome::Unreadable);
		QCOMPARE(last.error, Delta::kFullMessageUnreadable);
		QVERIFY(!last.error.contains(u"secret"_s));
		QVERIFY(!last.error.contains(u"/var"_s));
		QCOMPARE(rpc.htmlCalls(), 1);
	}

	void staleScopeBeforeMetadataDropsHtml() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult) {
				done = true;
				QFAIL("Full message callback escaped its scope");
			});
		QCOMPARE(rpc.methods(), (QStringList{ u"get_message"_s }));
		scope.invalidate();
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(!done);
		QCOMPARE(rpc.htmlCalls(), 0);
		QCOMPARE(int(rpc.pending.size()), 0);
	}

	void firstDispatchInvalidationStopsHtml() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		const auto inner = rpc.bind();
		Delta::ReadFullMessage(
			[&](QString method, QJsonArray params, DeltaRpc::Callback callback) {
				inner(std::move(method), std::move(params), std::move(callback));
				if (rpc.log.size() == 1) {
					scope.invalidate();
				}
			},
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult) {
				done = true;
				QFAIL("Full message callback escaped its start generation");
			});
		QCOMPARE(int(rpc.log.size()), 1);
		QCOMPARE(rpc.log[0].method, u"get_message"_s);
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QVERIFY(!done);
		QCOMPARE(rpc.htmlCalls(), 0);
	}

	void staleScopeAfterHtmlDispatchDropsResult() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = false;
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult) {
				done = true;
				QFAIL("Full message HTML callback escaped its scope");
			});
		QVERIFY(rpc.replyResult(MessageJson(101, 10, true)));
		QCOMPARE(rpc.htmlCalls(), 1);
		scope.invalidate();
		QVERIFY(rpc.replyResult(kHtml));
		QVERIFY(!done);
	}

	void completeOnceDoesNotRetry() {
		MessageFixture rpc;
		auto scope = Delta::AccountScope();
		auto done = 0;
		Delta::ReadFullMessage(
			rpc.bind(),
			scope,
			1,
			101,
			10,
			[&](Delta::FullMessageResult) { ++done; });
		QVERIFY(rpc.replyError(u"nope"_s));
		QCOMPARE(done, 1);
		QCOMPARE(rpc.htmlCalls(), 0);
		QVERIFY(!rpc.replyResult(MessageJson(101, 10, true)));
		QCOMPARE(done, 1);
	}
};

QTEST_GUILESS_MAIN(FullMessageTests)
#include "full_message_tests.moc"
