#include "backup_export.h"

#include <QtTest/QtTest>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>

#include <optional>

struct ExportFixture {
	QStringList methods;
	QList<QJsonArray> params;
	DeltaRpc::Callback pending;
	Delta::AccountInventoryRequest request() {
		return [this](QString method, QJsonArray args, DeltaRpc::Callback done) {
			methods.push_back(method);
			params.push_back(std::move(args));
			pending = std::move(done);
		};
	}
	void reply(QJsonObject value) {
		auto callback = std::move(pending);
		callback(std::move(value));
	}
};

class BackupExportTests final : public QObject {
	Q_OBJECT
private slots:
	void uniqueExportDirectoriesPreserveParent() {
		QTemporaryDir root;
		QVERIFY(root.isValid());
		QFile canary(root.filePath("keep.txt"));
		QVERIFY(canary.open(QIODevice::WriteOnly));
		canary.write("keep");
		canary.close();
		const auto first = Delta::CreateBackupExportDirectory(root.path());
		const auto second = Delta::CreateBackupExportDirectory(root.path());
		QVERIFY(!first.isEmpty() && !second.isEmpty());
		QVERIFY(first != second);
		QCOMPARE(QFileInfo(first).absolutePath(), root.path());
		QVERIFY(QFileInfo::exists(root.filePath("keep.txt")));
#if defined(Q_OS_UNIX)
		const auto shared = QFileDevice::ReadGroup | QFileDevice::WriteGroup
			| QFileDevice::ExeGroup | QFileDevice::ReadOther
			| QFileDevice::WriteOther | QFileDevice::ExeOther;
		QVERIFY(!(QFileInfo(first).permissions() & shared));
#endif
		QVERIFY(Delta::CreateBackupExportDirectory({}).isEmpty());
		QVERIFY(Delta::CreateBackupExportDirectory(root.filePath("missing")).isEmpty());
		QVERIFY(Delta::CreateBackupExportDirectory(root.filePath("keep.txt")).isEmpty());
	}

	void plainExport() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		auto result = std::optional<Delta::BackupExportResult>();
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {},
			[&](auto value) { result = value; });
		QCOMPARE(rpc.methods, QStringList{ "export_backup" });
		QCOMPARE(rpc.params[0], (QJsonArray{ 5, "/backup", QJsonValue() }));
		QVERIFY(state->busy);
		QVERIFY(!result);
		rpc.reply({ { "result", QJsonValue() } });
		QVERIFY(result && result->confirmed && result->error.isEmpty());
		QVERIFY(!state->busy);
	}
	void exactPassphrase() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		const auto secret = QString::fromUtf8("  fixture بازی\t ");
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", secret, [](auto) {});
		QCOMPARE(rpc.params[0], (QJsonArray{ 5, "/backup", secret }));
		rpc.reply({ { "result", QJsonValue() } });
	}
	void invalidInputDoesNotWrite() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		auto denied = 0;
		const auto done = [&](auto result) { if (!result.confirmed) ++denied; };
		Delta::ExportBackupFile(rpc.request(), state, scope, 0, "/backup", {}, done);
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, {}, {}, done);
		Delta::ExportBackupFile({}, state, scope, 5, "/backup", {}, done);
		Delta::ExportBackupFile(rpc.request(), {}, scope, 5, "/backup", {}, done);
		QCOMPARE(denied, 4);
		QVERIFY(rpc.methods.isEmpty());
		QVERIFY(!state->busy);
	}
	void malformedAndErrorReplies_data() {
		QTest::addColumn<QJsonObject>("reply");
		QTest::newRow("missing") << QJsonObject();
		QTest::newRow("bool") << QJsonObject{ { "result", true } };
		QTest::newRow("string") << QJsonObject{ { "result", "ok" } };
		QTest::newRow("error-string") << QJsonObject{ { "error", "rejected" } };
		QTest::newRow("error-object") << QJsonObject{ { "error", QJsonObject{ { "message", "rejected" } } } };
		QTest::newRow("error-and-null") << QJsonObject{ { "result", QJsonValue() }, { "error", "rejected" } };
	}
	void malformedAndErrorReplies() {
		QFETCH(QJsonObject, reply);
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		auto result = std::optional<Delta::BackupExportResult>();
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {},
			[&](auto value) { result = value; });
		rpc.reply(reply);
		QVERIFY(result && !result->confirmed && !result->error.isEmpty());
		QVERIFY(!state->busy);
		QCOMPARE(rpc.methods.size(), 1); // no implicit retry
	}
	void exportsAcrossAccountsAreSerialized() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {}, [](auto) {});
		auto blocked = false;
		Delta::ExportBackupFile(rpc.request(), state, scope, 6, "/backup", {},
			[&](auto result) { blocked = !result.confirmed; });
		QVERIFY(blocked);
		QCOMPARE(rpc.methods.size(), 1);
		rpc.reply({ { "result", QJsonValue() } });
		Delta::ExportBackupFile(rpc.request(), state, scope, 6, "/backup", {}, [](auto) {});
		QCOMPARE(rpc.params[1][0].toInt(), 6);
		rpc.reply({ { "result", QJsonValue() } });
	}
	void profileSwitchKeepsReservationUntilReplyAndDropsUi() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = std::make_shared<Delta::BackupExportState>();
		auto displayed = false;
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {},
			[&](auto) { displayed = true; });
		scope.invalidate();
		QVERIFY(state->busy);
		rpc.reply({ { "result", QJsonValue() } });
		QVERIFY(!state->busy);
		QVERIFY(!displayed);
	}
	void pendingExportOutlivesUiScopeAndRegistryCaller() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = Delta::BackupExportStateFor(&rpc);
		auto displayed = false;
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {},
			[&](auto) { displayed = true; });
		state.reset();
		scope.invalidate();
		QVERIFY(Delta::AnyBackupExportInProgress());
		rpc.reply({ { "result", QJsonValue() } });
		QVERIFY(!displayed);
		QVERIFY(!Delta::AnyBackupExportInProgress());
	}
	void globalPendingWaitsForEveryRpc() {
		ExportFixture first, second;
		Delta::AccountScope scope;
		auto one = Delta::BackupExportStateFor(&first);
		auto two = Delta::BackupExportStateFor(&second);
		Delta::ExportBackupFile(first.request(), one, scope, 5, "/backup", {}, [](auto) {});
		Delta::ExportBackupFile(second.request(), two, scope, 6, "/backup", {}, [](auto) {});
		QVERIFY(Delta::AnyBackupExportInProgress());
		first.reply({ { "result", QJsonValue() } });
		QVERIFY(Delta::AnyBackupExportInProgress());
		second.reply({ { "error", "rejected" } });
		QVERIFY(!Delta::AnyBackupExportInProgress());
	}
	void expiredIdleRegistryDoesNotBlockExit() {
		int rpc = 0;
		{
			const auto idle = Delta::BackupExportStateFor(&rpc);
			QVERIFY(!Delta::AnyBackupExportInProgress());
		}
		QVERIFY(!Delta::AnyBackupExportInProgress());
	}

	void exportingProfileCannotBeRemoved() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = Delta::BackupExportStateFor(&rpc);
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {}, [](auto) {});
		QVERIFY(!Delta::AcquireBackupProfileRemoval(state));
		rpc.reply({ { "result", QJsonValue() } });
		QVERIFY(Delta::AcquireBackupProfileRemoval(state));
	}
	void removingProfileCannotBeExportedOrRemovedTwice() {
		ExportFixture rpc;
		Delta::AccountScope scope;
		auto state = Delta::BackupExportStateFor(&rpc);
		const auto removal = Delta::AcquireBackupProfileRemoval(state);
		QVERIFY(removal);
		QVERIFY(!Delta::AcquireBackupProfileRemoval(state));
		auto denied = false;
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {},
			[&](auto result) { denied = !result.confirmed && !result.error.isEmpty(); });
		QVERIFY(denied);
		QVERIFY(rpc.methods.isEmpty());
		removal->release();
		removal->release(); // Terminal cleanup can also destroy the owner.
		Delta::ExportBackupFile(rpc.request(), state, scope, 5, "/backup", {}, [](auto) {});
		QCOMPARE(rpc.methods.size(), 1);
		rpc.reply({ { "result", QJsonValue() } });
	}
	void droppedRemovalCallbackReleasesReservation() {
		ExportFixture rpc;
		auto state = Delta::BackupExportStateFor(&rpc);
		auto removal = Delta::AcquireBackupProfileRemoval(state);
		DeltaRpc::Callback pending = [removal](auto) {};
		removal.reset();
		QVERIFY(state->removingProfile);
		pending = {};
		QVERIFY(!state->removingProfile);
		QVERIFY(Delta::AcquireBackupProfileRemoval(state));
	}
	void removalKeepsSharedRegistryStateAlive() {
		int rpc = 0;
		auto state = Delta::BackupExportStateFor(&rpc);
		auto removal = Delta::AcquireBackupProfileRemoval(state);
		state.reset();
		QVERIFY(Delta::BackupExportStateFor(&rpc)->removingProfile);
		removal.reset();
		QVERIFY(!Delta::BackupExportStateFor(&rpc)->removingProfile);
	}

	void rpcStateIsSharedOnlyForSameRpc() {
		int first = 1, second = 2;
		auto one = Delta::BackupExportStateFor(&first);
		auto same = Delta::BackupExportStateFor(&first);
		auto other = Delta::BackupExportStateFor(&second);
		QVERIFY(one == same);
		QVERIFY(one != other);
	}
};

QTEST_GUILESS_MAIN(BackupExportTests)
#include "backup_export_tests.moc"
