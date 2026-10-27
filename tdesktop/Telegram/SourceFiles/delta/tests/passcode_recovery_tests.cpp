#include "delta/passcode_recovery.h"

#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>

class PasscodeRecoveryTests final : public QObject {
	Q_OBJECT
private slots:
	void loadedAccounts();
	void coldStorage();
	void legacyAndCorruptMetadata();
	void noWrites();
	void hiddenAndUnreadableCoreStorage();
};

using Backend = Main::AccountBackend;
using Recovery = Delta::PasscodeRecovery;

void PasscodeRecoveryTests::loadedAccounts() {
	QCOMPARE(Delta::PlanPasscodeRecovery(true, { Backend::Core },
		Recovery::TelegramLogout), Recovery::PreserveChatmail);
	QCOMPARE(Delta::PlanPasscodeRecovery(true,
		{ Backend::Telegram, Backend::Core }, Recovery::TelegramLogout),
		Recovery::PreserveChatmail);
	QCOMPARE(Delta::PlanPasscodeRecovery(true, { Backend::Unspecified },
		Recovery::TelegramLogout), Recovery::PreserveUnknown);
	QCOMPARE(Delta::PlanPasscodeRecovery(true, {}, Recovery::PreserveUnknown),
		Recovery::PreserveUnknown);
	QCOMPARE(Delta::PlanPasscodeRecovery(true, { Backend::Telegram },
		Recovery::TelegramLogout), Recovery::TelegramLogout);
	QCOMPARE(Delta::PlanPasscodeRecovery(false, { Backend::Core },
		Recovery::PreserveChatmail), Recovery::TelegramLogout);
}

void PasscodeRecoveryTests::coldStorage() {
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const auto root = dir.path();
	const auto core = root + "/core";
	const auto id = QByteArray("0123456789ABCDEF");
	const auto path = Main::AccountBackendStorePath(root, id);
	const auto inspect = [&] {
		return Delta::InspectPasscodeRecoveryStorage(root, core, false);
	};
	QCOMPARE(inspect(), Recovery::PreserveUnknown);
	QCOMPARE(Main::WriteAccountBackendStore(path, id, Backend::Telegram),
		Main::AccountBackendStoreWriteStatus::Written);
	QCOMPARE(inspect(), Recovery::TelegramLogout);
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, core, true),
		Recovery::PreserveChatmail);
	QVERIFY(QDir().mkpath(core));
	QCOMPARE(inspect(), Recovery::TelegramLogout);
	QFile database(core + "/accounts.toml");
	QVERIFY(database.open(QIODevice::WriteOnly));
	QCOMPARE(database.write("private sentinel"), qint64(16));
	database.close();
	QCOMPARE(inspect(), Recovery::PreserveChatmail);
}

void PasscodeRecoveryTests::legacyAndCorruptMetadata() {
	QTemporaryDir dir;
	const auto root = dir.path();
	const auto core = root + "/absent";
	const auto id = QByteArray("0123456789ABCDEF");
	const auto path = Main::AccountBackendStorePath(root, id);
	QCOMPARE(Main::WriteAccountBackendStore(path, id, Backend::Telegram),
		Main::AccountBackendStoreWriteStatus::Written);
	QVERIFY(QDir().mkpath(root + "/FEDCBA9876543210"));
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, core, false),
		Recovery::PreserveUnknown);
	QVERIFY(QDir(root + "/FEDCBA9876543210").removeRecursively());
	QFile metadata(path);
	QVERIFY(metadata.open(QIODevice::WriteOnly | QIODevice::Truncate));
	metadata.write("invalid");
	metadata.close();
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, core, false),
		Recovery::PreserveUnknown);
}

void PasscodeRecoveryTests::noWrites() {
	QTemporaryDir dir;
	const auto root = dir.path();
	const auto id = QByteArray("0123456789ABCDEF");
	const auto path = Main::AccountBackendStorePath(root, id);
	QCOMPARE(Main::WriteAccountBackendStore(path, id, Backend::Core),
		Main::AccountBackendStoreWriteStatus::Written);
	QFile auth(root + "/0123456789ABCDEFs");
	QVERIFY(auth.open(QIODevice::ReadWrite));
	const auto sentinel = QByteArray("encrypted auth sentinel");
	auth.write(sentinel);
	auth.flush();
	QFile metadata(path);
	QVERIFY(metadata.open(QIODevice::ReadOnly));
	const auto before = metadata.readAll();
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, root + "/absent", false),
		Recovery::PreserveChatmail);
	auth.seek(0);
	QCOMPARE(auth.readAll(), sentinel);
	metadata.seek(0);
	QCOMPARE(metadata.readAll(), before);
}

void PasscodeRecoveryTests::hiddenAndUnreadableCoreStorage() {
	QTemporaryDir dir;
	const auto root = dir.path();
	const auto core = root + "/core";
	const auto id = QByteArray("0123456789ABCDEF");
	QCOMPARE(Main::WriteAccountBackendStore(
		Main::AccountBackendStorePath(root, id), id, Backend::Telegram),
		Main::AccountBackendStoreWriteStatus::Written);
	QVERIFY(QDir().mkpath(core));
	QFile hidden(core + "/.profile");
	QVERIFY(hidden.open(QIODevice::WriteOnly));
	hidden.write("sentinel");
	hidden.close();
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, core, false),
		Recovery::PreserveChatmail);
#if !defined(Q_OS_WIN)
	QVERIFY(QFile::setPermissions(core, QFileDevice::WriteOwner));
	QCOMPARE(Delta::InspectPasscodeRecoveryStorage(root, core, false),
		Recovery::PreserveUnknown);
	QVERIFY(QFile::setPermissions(core, QFileDevice::ReadOwner
		| QFileDevice::WriteOwner | QFileDevice::ExeOwner));
#endif
}

QTEST_GUILESS_MAIN(PasscodeRecoveryTests)
#include "passcode_recovery_tests.moc"
