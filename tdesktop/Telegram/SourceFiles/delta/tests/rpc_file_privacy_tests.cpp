#include "delta_rpc.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>
#include <QtCore/QFile>

#include <iostream>
#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr auto kChild = "DELTA_RPC_FILE_PRIVACY_TEST_CHILD";

int RunChild() {
#if defined(Q_OS_UNIX)
	for (std::string line; std::getline(std::cin, line);) {
		const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
		const auto directory = QFile::encodeName(qEnvironmentVariable("DC_ACCOUNTS_PATH") + "/created");
		const auto path = directory + "/backup.tar";
		const auto made = (::mkdir(directory.constData(), 0777) == 0);
		const auto fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
		if (fd >= 0) ::close(fd);
		const auto reply = QJsonObject{
			{ "jsonrpc", "2.0" }, { "id", request.value("id") },
			{ "result", made && fd >= 0 },
		};
		std::cout << QJsonDocument(reply).toJson(QJsonDocument::Compact).constData() << std::endl;
	}
	return 0;
#else
	return 2;
#endif
}

} // namespace

class RpcFilePrivacyTests final : public QObject {
	Q_OBJECT
private slots:
	void privateChildFiles_data() {
		QTest::addColumn<int>("parentMask");
		QTest::newRow("permissive-parent") << 0000;
		QTest::newRow("usual-parent") << 0022;
		QTest::newRow("private-parent") << 0077;
	}
	void privateChildFiles() {
#if defined(Q_OS_UNIX)
		QFETCH(int, parentMask);
		struct MaskRestore {
			mode_t previous;
			~MaskRestore() { ::umask(previous); }
		} restore{ ::umask(mode_t(parentMask)) };
		QTemporaryDir root;
		QVERIFY(root.isValid());
		const auto previous = qgetenv(kChild);
		const auto wasSet = qEnvironmentVariableIsSet(kChild);
		qputenv(kChild, "1");
		DeltaRpc rpc;
		rpc.start(QCoreApplication::applicationFilePath(), root.path());
		if (wasSet) qputenv(kChild, previous); else qunsetenv(kChild);
		auto replied = false;
		auto succeeded = false;
		rpc.call("write_fixture", {}, [&](QJsonObject reply) {
			replied = true;
			succeeded = reply.value("result").toBool();
		});
		QTRY_VERIFY(replied);
		QVERIFY(succeeded);
		struct stat file = {}, directory = {};
		QVERIFY(::stat(QFile::encodeName(root.path() + "/created/backup.tar").constData(), &file) == 0);
		QVERIFY(::stat(QFile::encodeName(root.path() + "/created").constData(), &directory) == 0);
		QCOMPARE(file.st_mode & 0777, mode_t(0600));
		QCOMPARE(directory.st_mode & 0777, mode_t(0700));
		const auto unchanged = ::umask(mode_t(parentMask));
		QCOMPARE(unchanged, mode_t(parentMask));
		rpc.stop();
#else
		QSKIP("POSIX file masks do not establish Windows ACL privacy.");
#endif
	}
};

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	if (qEnvironmentVariableIsSet(kChild)) return RunChild();
	RpcFilePrivacyTests tests;
	return QTest::qExec(&tests, argc, argv);
}

#include "rpc_file_privacy_tests.moc"
