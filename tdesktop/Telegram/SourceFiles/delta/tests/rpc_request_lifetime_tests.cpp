#include "backup_export.h"
#include "delta_rpc.h"
#include "profile_setup_actions.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QPointer>
#include <QtCore/QTemporaryDir>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtTest/QtTest>

#include <iostream>
#include <optional>

namespace {

constexpr auto kChild = "DELTA_RPC_REQUEST_LIFETIME_TEST_CHILD";

bool Touch(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly);
}

int RunChild() {
	const auto root = qEnvironmentVariable("DC_ACCOUNTS_PATH");
	const auto mode = qEnvironmentVariable(kChild);
	for (std::string line; std::getline(std::cin, line);) {
		const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
		auto second = QJsonObject();
		if (mode == "batch") {
			std::string next;
			if (!std::getline(std::cin, next)) return 25;
			second = QJsonDocument::fromJson(QByteArray::fromStdString(next)).object();
		}
		const auto method = request.value("method").toString();
		const auto readback = (method == "is_configured");
		const auto suffix = readback ? QString("-readback") : QString();
		QFile methods(root + "/methods");
		if (!methods.open(QIODevice::Append)) return 24;
		methods.write(method.toUtf8() + '\n');
		methods.close();
		if (!Touch(root + "/started" + suffix)) return 20;
		for (auto i = 0; !QFileInfo::exists(root + "/release" + suffix); ++i) {
			if (i == 500) return 21;
			QThread::msleep(10);
		}
		if (mode == "crash" || (readback && mode == "setup-readback-crash")) return 22;
		if (!Touch(root + "/completed")) return 23;
		if (mode == "malformed") {
			std::cout << "{invalid" << std::endl;
			continue;
		}
		auto reply = QJsonObject{ { "id", request.value("id") } };
		if (mode == "error" || (!readback && mode.startsWith("setup-") && mode != "setup-success")) {
			reply.insert("error", QJsonObject{ { "message", "fixture rejection" } });
		} else if (readback && mode == "setup-fresh") {
			reply.insert("result", false);
		} else if (readback && mode == "setup-configured") {
			reply.insert("result", true);
		} else {
			reply.insert("result", QJsonValue());
		}
		auto payload = QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
		if (mode == "batch") {
			reply.insert("id", second.value("id"));
			payload += QJsonDocument(reply).toJson(QJsonDocument::Compact) + '\n';
		} else if (mode == "batch-malformed") {
			payload += "{invalid\n";
		}
		std::cout << payload.constData() << std::flush;
	}
	return 0;
}

std::shared_ptr<DeltaRpc> StartRpc(const QString &root, const QByteArray &mode) {
	const auto previous = qgetenv(kChild);
	const auto wasSet = qEnvironmentVariableIsSet(kChild);
	qputenv(kChild, mode);
	auto rpc = std::make_shared<DeltaRpc>();
	rpc->start(QCoreApplication::applicationFilePath(), root);
	if (wasSet) qputenv(kChild, previous); else qunsetenv(kChild);
	return rpc;
}

} // namespace

class RpcRequestLifetimeTests final : public QObject {
	Q_OBJECT
private slots:
	void exportSurvivesOwnerLoss_data() {
		QTest::addColumn<QByteArray>("mode");
		for (const auto mode : { "success", "error", "crash", "malformed" }) {
			QTest::newRow(mode) << QByteArray(mode);
		}
	}
	void exportSurvivesOwnerLoss() {
		QFETCH(QByteArray, mode);
		QTemporaryDir root;
		QVERIFY(root.isValid());
		auto rpc = StartRpc(root.path(), mode);
		const auto weak = std::weak_ptr<DeltaRpc>(rpc);
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		auto state = Delta::BackupExportStateFor(rpc.get());
		Delta::AccountScope scope;
		auto displayed = false;
		Delta::ExportBackupFile(
			[rpc](QString method, QJsonArray args, DeltaRpc::Callback done) {
				CallOwnedDeltaRpc(rpc, method, args, std::move(done));
			}, state, scope, 5, root.path(), {}, [&](auto) { displayed = true; });
		QTRY_VERIFY(QFileInfo::exists(root.filePath("started")));
		scope.invalidate();
		rpc.reset();
		QVERIFY(!weak.expired());
		QVERIFY(state->busy);
		QVERIFY(Delta::AnyBackupExportInProgress());
		QVERIFY(Touch(root.filePath("release")));
		QTRY_VERIFY(!state->busy);
		QTRY_VERIFY(weak.expired());
		QTRY_VERIFY(pointer.isNull());
		QVERIFY(!displayed);
		QVERIFY(!Delta::AnyBackupExportInProgress());
		QCOMPARE(QFileInfo::exists(root.filePath("completed")), mode != "crash");
	}
	void callbackCannotDestroyRpcInsideReadHandler() {
		QTemporaryDir root;
		auto rpc = StartRpc(root.path(), "success");
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		const auto weak = std::weak_ptr<DeltaRpc>(rpc);
		auto replied = false;
		auto aliveAfterNestedLoop = false;
		CallOwnedDeltaRpc(rpc, "fixture", {}, [&](QJsonObject reply) {
			replied = reply.contains("result");
			QEventLoop nested;
			QTimer::singleShot(10, &nested, &QEventLoop::quit);
			nested.exec();
			aliveAfterNestedLoop = !weak.expired() && pointer;
		});
		QTRY_VERIFY(QFileInfo::exists(root.filePath("started")));
		rpc.reset();
		QVERIFY(Touch(root.filePath("release")));
		QTRY_VERIFY(replied);
		QVERIFY(aliveAfterNestedLoop);
		QTRY_VERIFY(pointer.isNull());
		QVERIFY(weak.expired());
	}
	void bufferedCallbacksKeepOwnerUntilDispatchReturns_data() {
		QTest::addColumn<QByteArray>("mode");
		QTest::newRow("second-reply") << QByteArray("batch");
		QTest::newRow("transport-error") << QByteArray("batch-malformed");
	}
	void bufferedCallbacksKeepOwnerUntilDispatchReturns() {
		QFETCH(QByteArray, mode);
		QTemporaryDir root;
		QVERIFY(root.isValid());
		auto rpc = StartRpc(root.path(), mode);
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		const auto weak = std::weak_ptr<DeltaRpc>(rpc);
		auto first = false;
		auto second = false;
		auto aliveAfterNestedLoop = false;
		const auto observe = [&](QJsonObject reply) {
			QEventLoop nested;
			QTimer::singleShot(10, &nested, &QEventLoop::quit);
			nested.exec();
			aliveAfterNestedLoop = !weak.expired() && pointer;
			second = mode == "batch" ? reply.contains("result") : reply.contains("error");
		};
		if (mode != "batch") rpc->setEventHandler(observe);
		CallOwnedDeltaRpc(rpc, "first", {}, [&](QJsonObject reply) {
			first = reply.contains("result");
		});
		if (mode == "batch") rpc->call("second", {}, observe);
		QTRY_VERIFY(QFileInfo::exists(root.filePath("started")));
		rpc.reset();
		QVERIFY(Touch(root.filePath("release")));
		QTRY_VERIFY(second);
		QVERIFY(first);
		QVERIFY(aliveAfterNestedLoop);
		QTRY_VERIFY(pointer.isNull());
		QVERIFY(weak.expired());
	}

	void explicitStopCompletesAndReleasesOwner() {
		QTemporaryDir root;
		auto rpc = StartRpc(root.path(), "success");
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		auto replied = false;
		CallOwnedDeltaRpc(rpc, "export_backup", {}, [&](QJsonObject reply) {
			replied = reply.contains("error");
		});
		QTRY_VERIFY(QFileInfo::exists(root.filePath("started")));
		rpc.reset();
		QVERIFY(pointer);
		pointer->stop();
		QVERIFY(replied);
		QVERIFY(pointer); // Not destroyed while stop() is unwinding.
		QTRY_VERIFY(pointer.isNull());
		QVERIFY(!QFileInfo::exists(root.filePath("completed")));
	}
	void setupSurvivesUiAndRpcOwnerLoss_data() {
		QTest::addColumn<QString>("method");
		QTest::addColumn<QByteArray>("mode");
		QTest::addColumn<int>("outcome");
		for (const auto method : { "import_backup", "get_backup", "init_transports", "add_or_update_transport" }) {
			for (const auto mode : { "setup-success", "setup-fresh", "setup-configured", "setup-readback-malformed", "setup-readback-crash" }) {
				const auto outcome = QByteArray(mode) == "setup-success"
					? Delta::ProfileSetupOutcome::Succeeded
					: QByteArray(mode) == "setup-fresh"
					? Delta::ProfileSetupOutcome::Retryable
					: QByteArray(mode) == "setup-configured"
					? Delta::ProfileSetupOutcome::OpenOnly
					: Delta::ProfileSetupOutcome::Closed;
				QTest::newRow(qPrintable(QString(method) + '/' + mode))
					<< QString(method) << QByteArray(mode) << int(outcome);
			}
		}
	}
	void setupSurvivesUiAndRpcOwnerLoss() {
		QFETCH(QString, method);
		QFETCH(QByteArray, mode);
		QFETCH(int, outcome);
		QTemporaryDir root;
		QVERIFY(root.isValid());
		auto rpc = StartRpc(root.path(), mode);
		const auto weak = std::weak_ptr<DeltaRpc>(rpc);
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		auto ui = std::make_unique<QObject>();
		const auto weakUi = QPointer<QObject>(ui.get());
		const auto table = Delta::ProfileSetupTableFor(rpc.get());
		auto lease = Delta::TryAcquireProfileSetup(table, 5,
			method == "import_backup" ? Delta::ProfileSetupKind::Import
			: method == "get_backup" ? Delta::ProfileSetupKind::Copy
			: Delta::ProfileSetupKind::Create);
		QVERIFY(lease);
		auto result = std::optional<Delta::ProfileSetupResult>();
		auto displayed = false;
		auto aliveInCallback = false;
		const auto done = [&](Delta::ProfileSetupResult reply) {
			result = std::move(reply);
			displayed = bool(weakUi);
			aliveInCallback = !weak.expired() && pointer;
		};
		if (method == "import_backup") {
			Delta::ImportProfileBackup(Delta::MakeOwnedProfileSetupRequest(rpc),
				std::move(lease), "/fixture.tar", done);
		} else if (method == "get_backup") {
			Delta::CopyProfileBackup(Delta::MakeOwnedProfileSetupRequest(rpc),
				std::move(lease), "fixture-setup-code", done);
		} else {
			Delta::CommitProfileSetup(Delta::MakeOwnedProfileSetupRequest(rpc),
				std::move(lease), method, { 5, QJsonValue() }, done);
		}
		QTRY_VERIFY(QFileInfo::exists(root.filePath("started")));
		ui.reset();
		rpc.reset();
		QVERIFY(!weak.expired());
		QVERIFY(Delta::ProfileSetupHeld(table, 5));
		QVERIFY(Touch(root.filePath("release")));
		if (mode != "setup-success") {
			QTRY_VERIFY(QFileInfo::exists(root.filePath("started-readback")));
			QVERIFY(!result);
			QVERIFY(!weak.expired());
			QVERIFY(Delta::ProfileSetupHeld(table, 5));
			QVERIFY(Touch(root.filePath("release-readback")));
		}
		QTRY_VERIFY(result.has_value());
		QCOMPARE(int(result->outcome), outcome);
		QCOMPARE(result->accountId, 5);
		QVERIFY(!displayed);
		QVERIFY(aliveInCallback);
		QVERIFY(!Delta::ProfileSetupHeld(table, 5));
		QTRY_VERIFY(weak.expired());
		QTRY_VERIFY(pointer.isNull());
		QFile calls(root.filePath("methods"));
		QVERIFY(calls.open(QIODevice::ReadOnly));
		const auto expected = method + (mode == "setup-success" ? "\n" : "\nis_configured\n");
		QCOMPARE(QString::fromUtf8(calls.readAll()), expected);
	}

	void parentedRpcIsRejectedWithoutStartingRequest() {
		QObject parent;
		auto rpc = std::make_shared<DeltaRpc>(&parent);
		auto rejected = false;
		CallOwnedDeltaRpc(rpc, "fixture", {}, [&](QJsonObject reply) {
			rejected = reply.contains("error");
		});
		QVERIFY(rejected);
		QVERIFY(!rpc->running());
		rpc.reset();
		QVERIFY(parent.children().isEmpty());
	}
	void stoppedAndNullRpcCompleteWithoutLeak() {
		auto rpc = std::make_shared<DeltaRpc>();
		const auto pointer = QPointer<DeltaRpc>(rpc.get());
		auto errors = 0;
		const auto done = [&](QJsonObject reply) { if (reply.contains("error")) ++errors; };
		CallOwnedDeltaRpc(rpc, "fixture", {}, done);
		rpc.reset();
		CallOwnedDeltaRpc({}, "fixture", {}, done);
		QCOMPARE(errors, 2);
		QTRY_VERIFY(pointer.isNull());
	}
};

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	if (qEnvironmentVariableIsSet(kChild)) return RunChild();
	RpcRequestLifetimeTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "rpc_request_lifetime_tests.moc"
