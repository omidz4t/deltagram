#include "profile_setup_avatar.h"

#include <functional>

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

namespace {

const auto kSharedAvatarName = u"/delta-tel-avatar.png"_s;

[[nodiscard]] QByteArray ReadAll(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

[[nodiscard]] bool IsOwnerPrivate(const QString &path) {
	const auto perms = QFileInfo(path).permissions();
	if (!perms.testFlag(QFileDevice::ReadOwner)
		|| !perms.testFlag(QFileDevice::WriteOwner)) {
		return false;
	}
	const auto shared = QFileDevice::ReadGroup
		| QFileDevice::WriteGroup
		| QFileDevice::ExeGroup
		| QFileDevice::ReadOther
		| QFileDevice::WriteOther
		| QFileDevice::ExeOther;
	return !(perms & shared);
}

} // namespace

class ProfileSetupAvatarTests final : public QObject {
	Q_OBJECT

private slots:
	void emptyPayloadDoesNotStage() {
		QVERIFY(!Delta::StageProfileSetupAvatar({}));
	}

	void concurrentStagesKeepDistinctBytes() {
		const auto first = QByteArray("setup-avatar-one");
		const auto second = QByteArray("setup-avatar-two-different");
		const auto a = Delta::StageProfileSetupAvatar(first);
		const auto b = Delta::StageProfileSetupAvatar(second);
		QVERIFY(a);
		QVERIFY(b);
		QVERIFY(!a->path().isEmpty());
		QVERIFY(!b->path().isEmpty());
		QVERIFY(a->path() != b->path());
		const auto shared = QDir::tempPath() + kSharedAvatarName;
		QVERIFY(a->path() != shared);
		QVERIFY(b->path() != shared);
		QCOMPARE(ReadAll(a->path()), first);
		QCOMPARE(ReadAll(b->path()), second);
	}

	void stagedFileIsOwnerPrivate() {
		const auto owner = Delta::StageProfileSetupAvatar(
			QByteArray("private-setup-avatar"));
		QVERIFY(owner);
		QVERIFY(QFileInfo::exists(owner->path()));
#if !defined(Q_OS_WIN)
		QVERIFY(IsOwnerPrivate(owner->path()));
#endif
	}

	void pendingCallbackRetainsThenTerminalRemoves() {
		std::function<void()> pending;
		QString path;
		{
			auto owner = Delta::StageProfileSetupAvatar(
				QByteArray("held-setup-avatar"));
			QVERIFY(owner);
			path = owner->path();
			QVERIFY(QFileInfo::exists(path));
			pending = [owner] {
				QCOMPARE(ReadAll(owner->path()), QByteArray("held-setup-avatar"));
			};
			owner.reset();
			QVERIFY(QFileInfo::exists(path));
		}
		QVERIFY(QFileInfo::exists(path));
		pending();
		pending = nullptr;
		QVERIFY(!QFileInfo::exists(path));
	}

	void dropOwnerRemovesFile() {
		QString path;
		{
			const auto owner = Delta::StageProfileSetupAvatar(
				QByteArray("drop-setup-avatar"));
			QVERIFY(owner);
			path = owner->path();
			QVERIFY(QFileInfo::exists(path));
		}
		QVERIFY(!QFileInfo::exists(path));
	}

	void stagingCopiesPayload() {
		auto original = QByteArray("user-original-bytes");
		const auto snapshot = original;
		const auto owner = Delta::StageProfileSetupAvatar(original);
		QVERIFY(owner);
		QCOMPARE(original, snapshot);
		QCOMPARE(ReadAll(owner->path()), snapshot);
		QVERIFY(owner->path() != QDir::tempPath() + kSharedAvatarName);
	}

};

QTEST_GUILESS_MAIN(ProfileSetupAvatarTests)
#include "profile_setup_avatar_tests.moc"
