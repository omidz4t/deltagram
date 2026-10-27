#include "profile_setup_actions.h"

#include <memory>
#include <optional>
#include <vector>

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

struct SetupFixture {
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

QJsonObject ProgressEnvelope(
		int contextId,
		const QString &kind,
		int progress,
		const QString &comment = {}) {
	auto event = QJsonObject{
		{ u"kind"_s, kind },
		{ u"progress"_s, progress },
	};
	if (!comment.isEmpty()) {
		event.insert(u"comment"_s, comment);
	}
	return {
		{ u"contextId"_s, contextId },
		{ u"event"_s, std::move(event) },
	};
}

} // namespace

class ProfileSetupActionsTests final : public QObject {
	Q_OBJECT

	void expectNoOpenMutations(const SetupFixture &rpc) {
		QVERIFY(!rpc.has(u"start_io"_s));
		QVERIFY(!rpc.has(u"init_transports"_s));
		QVERIFY(!rpc.has(u"add_account"_s));
		QVERIFY(!rpc.has(u"add_or_update_transport"_s));
		QVERIFY(!rpc.has(u"set_config"_s));
		QVERIFY(!rpc.has(u"select_account"_s));
	}

	void expectParams(
			const SetupFixture &rpc,
			int index,
			const QString &method,
			QJsonArray params) {
		QVERIFY(index >= 0 && index < int(rpc.log.size()));
		QCOMPARE(rpc.log[index].method, method);
		QCOMPARE(rpc.log[index].params, params);
	}

private slots:
	void importPassphrasePreservesExactText() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(table, 5,
			Delta::ProfileSetupKind::Import);
		const auto secret = u"  fixture-بازی passphrase\t "_s;
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(rpc.bind(), lease, u"/tmp/encrypted.tar"_s,
			[&](auto value) { result = std::move(value); }, secret);
		expectParams(rpc, 0, u"import_backup"_s,
			QJsonArray{ 5, u"/tmp/encrypted.tar"_s, secret });
		QVERIFY(Delta::ProfileSetupBlocksMutation(table, 5));
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		expectNoOpenMutations(rpc);
	}

	void wrongPassphraseReadbackAllowsExplicitSameIdRetry() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(rpc.bind(),
			Delta::TryAcquireProfileSetup(table, 5, Delta::ProfileSetupKind::Import),
			u"/tmp/encrypted.tar"_s,
			[&](auto value) { result = std::move(value); }, u"wrong-fixture"_s);
		QVERIFY(rpc.replyError(u"backup passphrase is not correct"_s));
		expectParams(rpc, 1, u"is_configured"_s, QJsonArray{ 5 });
		QVERIFY(!result);
		QVERIFY(rpc.replyResult(false));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Retryable);
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
		QCOMPARE(rpc.log.size(), std::size_t(2));
		Delta::ImportProfileBackup(rpc.bind(),
			Delta::TryAcquireProfileSetup(table, 5, Delta::ProfileSetupKind::Import),
			u"/tmp/encrypted.tar"_s,
			[&](auto value) { result = std::move(value); }, u"correct-fixture"_s);
		expectParams(rpc, 2, u"import_backup"_s,
			QJsonArray{ 5, u"/tmp/encrypted.tar"_s, u"correct-fixture"_s });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		expectNoOpenMutations(rpc);
	}

	void emptyPassphraseStillUsesNull() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		Delta::ImportProfileBackup(rpc.bind(),
			Delta::TryAcquireProfileSetup(table, 5, Delta::ProfileSetupKind::Import),
			u"/tmp/plain.tar"_s, [](auto) {}, QString());
		QVERIFY(rpc.log[0].params[2].isNull());
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
	}

	void importSuccessExactSequenceNullPassphrase() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(lease);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/plain.tar"_s,
			[&](auto value) { result = std::move(value); });
		expectParams(
			rpc,
			0,
			u"import_backup"_s,
			QJsonArray{ 5, u"/tmp/plain.tar"_s, QJsonValue() });
		QVERIFY(rpc.log[0].params.at(2).isNull());
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		QCOMPARE(result->accountId, 5);
		QVERIFY(result->acknowledged);
		QVERIFY(result->wrote);
		QVERIFY(result->configured);
		QVERIFY(Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupCanRetryMutation(*result, 5));
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
		QVERIFY(Delta::ProfileSetupBlocksMutation(table, 5));
		QCOMPARE(rpc.methods(), QStringList{ u"import_backup"_s });
		expectNoOpenMutations(rpc);
	}

	void duplicateAcquisitionRefusesSecondSetup() {
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto first = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(first);
		auto second = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Copy);
		QVERIFY(!second);
		auto create = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		QVERIFY(!create);
		QVERIFY(Delta::ProfileSetupHeld(table, 5));
		auto other = Delta::TryAcquireProfileSetup(
			table,
			6,
			Delta::ProfileSetupKind::Create);
		QVERIFY(other);
		QCOMPARE(other->accountId, 6);
	}

	void createDeniedDuringHeldImport() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto importLease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(importLease);
		auto createLease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		QVERIFY(!createLease);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			importLease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		auto after = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		QVERIFY(!after);
		QCOMPARE(rpc.methods(), QStringList{ u"import_backup"_s });
		expectNoOpenMutations(rpc);
	}

	void errorConfiguredRecoverOpenOnly() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyError(u"lost ack"_s));
		expectParams(rpc, 1, u"is_configured"_s, { 5 });
		QVERIFY(rpc.replyResult(true));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::OpenOnly);
		QCOMPARE(result->accountId, 5);
		QVERIFY(result->wrote);
		QVERIFY(!result->acknowledged);
		QVERIFY(result->configured);
		QVERIFY(Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupCanRetryMutation(*result, 5));
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import));
		QCOMPARE(result->error, Delta::kProfileSetupUnconfirmed);
		QVERIFY(!result->error.contains(u"lost ack"_s));
		QCOMPARE(rpc.methods(), (QStringList{
			u"import_backup"_s,
			u"is_configured"_s,
		}));
		expectNoOpenMutations(rpc);
	}

	void errorUnconfiguredUnlocksSameId() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/bad.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyError(u"invalid archive"_s));
		QVERIFY(rpc.replyResult(false));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Retryable);
		QCOMPARE(result->accountId, 5);
		QCOMPARE(result->error, u"invalid archive"_s);
		QVERIFY(Delta::ProfileSetupCanRetryMutation(*result, 5));
		QVERIFY(!Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
		auto retry = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(retry);
		QCOMPARE(retry->accountId, 5);
		expectNoOpenMutations(rpc);
	}

	void malformedAndUnknownReadbackFailClosed_data() {
		QTest::addColumn<QJsonObject>("configured");
		QTest::newRow("missing") << QJsonObject{};
		QTest::newRow("error") << QJsonObject{ { u"error"_s, u"offline"_s } };
		QTest::newRow("string") << QJsonObject{ { u"result"_s, u"true"_s } };
		QTest::newRow("number") << QJsonObject{ { u"result"_s, 1 } };
		QTest::newRow("null") << QJsonObject{
			{ u"result"_s, QJsonValue(QJsonValue::Null) },
		};
		QTest::newRow("error-and-bool") << QJsonObject{
			{ u"error"_s, u"offline"_s },
			{ u"result"_s, true },
		};
	}

	void malformedAndUnknownReadbackFailClosed() {
		QFETCH(QJsonObject, configured);
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.reply({}));
		QVERIFY(rpc.reply(configured));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Closed);
		QCOMPARE(result->accountId, 5);
		QVERIFY(!Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupCanRetryMutation(*result, 5));
		QVERIFY(Delta::ProfileSetupClosed(table, 5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import));
		QCOMPARE(result->error, Delta::kProfileSetupUnreadable);
		expectNoOpenMutations(rpc);
	}

	void capturedIdMismatchDoesNotWriteOrOpen() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::CommitProfileSetup(
			rpc.bind(),
			lease,
			u"import_backup"_s,
			QJsonArray{ 7, u"/tmp/a.tar"_s, QJsonValue() },
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Denied);
		QCOMPARE(result->accountId, 5);
		QVERIFY(!result->wrote);
		QVERIFY(!Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupShouldOpen(*result, 7));
		QVERIFY(rpc.log.empty());
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
	}

	void staleUiDoesNotOpenWrongId() {
		auto result = Delta::ProfileSetupResult();
		result.outcome = Delta::ProfileSetupOutcome::Succeeded;
		result.accountId = 5;
		result.acknowledged = true;
		QVERIFY(Delta::ProfileSetupShouldOpen(result, 5));
		QVERIFY(!Delta::ProfileSetupShouldOpen(result, 8));
		QVERIFY(!Delta::ProfileSetupShouldOpen(result, 0));
		result.outcome = Delta::ProfileSetupOutcome::OpenOnly;
		QVERIFY(Delta::ProfileSetupShouldOpen(result, 5));
		QVERIFY(!Delta::ProfileSetupShouldOpen(result, 9));
		result.outcome = Delta::ProfileSetupOutcome::Retryable;
		QVERIFY(!Delta::ProfileSetupShouldOpen(result, 5));
	}

	void completedImportBlocksMutationsAndLateProgress() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
		QVERIFY(Delta::ProfileSetupBlocksMutation(table, 5));
		QVERIFY(Delta::ProfileSetupShouldOpen(*result, 5));
		QVERIFY(!Delta::ProfileSetupCanRetryMutation(*result, 5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create));
		const auto late = ProgressEnvelope(5, u"ImexProgress"_s, 1000);
		const auto parsed = Delta::ParseProfileSetupProgress(late);
		QVERIFY(parsed.has_value());
		QCOMPARE(parsed->contextId, 5);
		QCOMPARE(parsed->progress, 1000);
		QVERIFY(!Delta::ProfileSetupProgressStatus(late, table, 5));
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import));
		QCOMPARE(rpc.methods().count(u"import_backup"_s), 1);
		expectNoOpenMutations(rpc);
	}

	void noteOpenedClearsCompletedLease() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
		Delta::ProfileSetupNoteOpened(table, 5);
		QVERIFY(!Delta::ProfileSetupOpenReady(table, 5));
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
		QVERIFY(!Delta::ProfileSetupClosed(table, 5));
		expectNoOpenMutations(rpc);
	}

	void progressStatusAppliesOnlyToHeldExactContext() {
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(lease);
		const auto imex = ProgressEnvelope(5, u"ImexProgress"_s, 1000);
		const auto parsed = Delta::ParseProfileSetupProgress(imex);
		QVERIFY(parsed.has_value());
		QCOMPARE(parsed->contextId, 5);
		QCOMPARE(parsed->progress, 1000);
		QVERIFY(parsed->comment.isEmpty());
		const auto percent = Delta::ProfileSetupProgressStatus(imex, table, 5);
		QVERIFY(percent.has_value());
		QCOMPARE(*percent, u"100%"_s);
		const auto configure = ProgressEnvelope(
			5,
			u"ConfigureProgress"_s,
			0,
			u"Contacting the mail server…"_s);
		const auto comment = Delta::ProfileSetupProgressStatus(
			configure,
			table,
			5);
		QVERIFY(comment.has_value());
		QCOMPARE(*comment, u"Contacting the mail server…"_s);
		QVERIFY(!Delta::ProfileSetupProgressStatus(
			ProgressEnvelope(7, u"ImexProgress"_s, 1000),
			table,
			5));
		QVERIFY(!Delta::ProfileSetupProgressStatus(imex, table, 7));
		QVERIFY(!Delta::ProfileSetupProgressStatus(
			QJsonObject{
				{ u"event"_s, QJsonObject{
					{ u"kind"_s, u"ImexProgress"_s },
					{ u"progress"_s, 1000 },
				} },
			},
			table,
			5));
		QVERIFY(!Delta::ProfileSetupProgressStatus(
			ProgressEnvelope(5, u"IncomingMsg"_s, 1000),
			table,
			5));
		QVERIFY(!Delta::ParseProfileSetupProgress(QJsonObject{
			{ u"contextId"_s, 5.5 },
			{ u"event"_s, QJsonObject{
				{ u"kind"_s, u"ImexProgress"_s },
				{ u"progress"_s, 500 },
			} },
		}));
		QVERIFY(!Delta::ProfileSetupProgressStatus(
			ProgressEnvelope(5, u"ImexProgress"_s, 0),
			table,
			5));
	}

	void progressStatusIgnoredAfterClosed() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.reply({}));
		QVERIFY(rpc.reply({}));
		QVERIFY(Delta::ProfileSetupClosed(table, 5));
		QVERIFY(!Delta::ProfileSetupProgressStatus(
			ProgressEnvelope(5, u"ImexProgress"_s, 1000),
			table,
			5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import));
		expectNoOpenMutations(rpc);
	}

	void leaseCleanupAfterTerminalAndWidgetDrop() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(lease);
		std::weak_ptr<void> weak = lease->lifetime;
		QVERIFY(!weak.expired());
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		lease.reset();
		auto widget = table;
		table.reset();
		QVERIFY(!weak.expired());
		QVERIFY(rpc.replyError());
		QVERIFY(!weak.expired());
		widget.reset();
		QVERIFY(!weak.expired());
		QVERIFY(rpc.replyResult(false));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Retryable);
		QVERIFY(weak.expired());
		expectNoOpenMutations(rpc);
	}

	void sharedTableRejectsCrossWindowCreateDuringImport() {
		char rpcIdentity = 0;
		auto windowA = Delta::ProfileSetupTableFor(&rpcIdentity);
		auto windowB = Delta::ProfileSetupTableFor(&rpcIdentity);
		QCOMPARE(windowA.get(), windowB.get());
		auto importLease = Delta::TryAcquireProfileSetup(
			windowA,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(importLease);
		QVERIFY(!Delta::TryAcquireProfileSetup(
			windowB,
			5,
			Delta::ProfileSetupKind::Create));
		QVERIFY(Delta::ProfileSetupHeld(windowB, 5));
	}

	void copySuccessExactSequence() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Copy);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::CopyProfileBackup(
			rpc.bind(),
			lease,
			u"OPENPGP4FPR:ABCD"_s,
			[&](auto value) { result = std::move(value); });
		expectParams(
			rpc,
			0,
			u"get_backup"_s,
			QJsonArray{ 5, u"OPENPGP4FPR:ABCD"_s });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		QCOMPARE(rpc.methods(), QStringList{ u"get_backup"_s });
		expectNoOpenMutations(rpc);
	}

	void commitStartIoIsDeniedWithoutWrite() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::CommitProfileSetup(
			rpc.bind(),
			lease,
			u"start_io"_s,
			{ 5 },
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Denied);
		QVERIFY(!result->wrote);
		QVERIFY(rpc.log.empty());
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
		auto again = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		QVERIFY(again);
		result.reset();
		Delta::CommitProfileSetup(
			rpc.bind(),
			again,
			u"add_account"_s,
			{ 5 },
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Denied);
		QVERIFY(!result->wrote);
		QVERIFY(rpc.log.empty());
	}

	void createCommitInitTransportsDoesNotStartIo() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::CommitProfileSetup(
			rpc.bind(),
			lease,
			u"init_transports"_s,
			QJsonArray{ 5, QJsonValue() },
			[&](auto value) { result = std::move(value); });
		expectParams(
			rpc,
			0,
			u"init_transports"_s,
			QJsonArray{ 5, QJsonValue() });
		QVERIFY(rpc.replyResult(QJsonValue(QJsonValue::Null)));
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Succeeded);
		QCOMPARE(rpc.methods(), QStringList{ u"init_transports"_s });
		QVERIFY(!rpc.has(u"start_io"_s));
		QVERIFY(!rpc.has(u"import_backup"_s));
		QVERIFY(!rpc.has(u"add_account"_s));
		QVERIFY(Delta::ProfileSetupOpenReady(table, 5));
	}

	void emptyImportPathDenied() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			std::move(lease),
			{},
			[&](auto value) { result = std::move(value); });
		QVERIFY(result);
		QCOMPARE(result->outcome, Delta::ProfileSetupOutcome::Denied);
		QVERIFY(rpc.log.empty());
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
	}

	void pickerCancelReleaseAllowsLaterImport() {
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(Delta::ProfileSetupHeld(table, 5));
		Delta::ReleaseProfileSetup(lease);
		QVERIFY(!Delta::ProfileSetupBlocksMutation(table, 5));
		auto again = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		QVERIFY(again);
	}

	void noteOpenedDoesNotClearClosed() {
		SetupFixture rpc;
		auto table = std::make_shared<Delta::ProfileSetupTable>();
		auto lease = Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Import);
		auto result = std::optional<Delta::ProfileSetupResult>();
		Delta::ImportProfileBackup(
			rpc.bind(),
			lease,
			u"/tmp/a.tar"_s,
			[&](auto value) { result = std::move(value); });
		QVERIFY(rpc.replyError());
		QVERIFY(rpc.reply({}));
		QVERIFY(Delta::ProfileSetupClosed(table, 5));
		Delta::ProfileSetupNoteOpened(table, 5);
		QVERIFY(Delta::ProfileSetupClosed(table, 5));
		QVERIFY(!Delta::TryAcquireProfileSetup(
			table,
			5,
			Delta::ProfileSetupKind::Create));
		expectNoOpenMutations(rpc);
	}
};

QTEST_GUILESS_MAIN(ProfileSetupActionsTests)
#include "profile_setup_actions_tests.moc"
