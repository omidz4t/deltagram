/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend.h"

#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileDevice>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtCore/QLatin1Char>
#include <QtCore/QLatin1String>
#include <QtCore/QSaveFile>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

namespace Main {

// App-owned per-account backend metadata. Lives beside tdata, never inside
// the donor encrypted map or MTP authorization files.
inline constexpr char kAccountBackendStoreMagic[] = "DTBM";
inline constexpr int kAccountBackendStoreMagicSize = 4;
inline constexpr quint32 kAccountBackendStoreVersion = 1;
inline constexpr auto kAccountBackendStoreDirectoryName = "deltagram_backend";
inline constexpr auto kAccountBackendStoreStreamVersion = QDataStream::Qt_5_15;
[[nodiscard]] inline QFileDevice::Permissions AccountBackendStoreFilePermissions() {
	return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}
inline constexpr auto kAccountBackendStoreInvalidTag = ~quint32(0);

[[nodiscard]] inline bool AccountBackendStoreSupportsPosixPrivacy() {
#if defined(Q_OS_WIN)
	return false;
#else
	return true;
#endif
}

[[nodiscard]] inline bool AccountBackendStorePermissionsArePrivate(
		QFileDevice::Permissions permissions) {
	if (!permissions.testFlag(QFileDevice::ReadOwner)
		|| !permissions.testFlag(QFileDevice::WriteOwner)) {
		return false;
	}
	if (!AccountBackendStoreSupportsPosixPrivacy()) {
		return true;
	}
	const auto shared = QFileDevice::ReadGroup
		| QFileDevice::WriteGroup
		| QFileDevice::ExeGroup
		| QFileDevice::ReadOther
		| QFileDevice::WriteOther
		| QFileDevice::ExeOther;
	return !(permissions & shared);
}

[[nodiscard]] inline bool AccountBackendStoreHasPrivatePermissions(
		const QString &path) {
	if (path.isEmpty() || !QFileInfo::exists(path)) {
		return false;
	}
	return AccountBackendStorePermissionsArePrivate(
		QFileInfo(path).permissions());
}

[[nodiscard]] inline bool AccountBackendStoreApplyPrivatePermissions(
		const QString &path) {
	if (path.isEmpty() || !QFile::exists(path)) {
		return false;
	}
	if (!QFile::setPermissions(path, AccountBackendStoreFilePermissions())) {
		return false;
	}
	if (!AccountBackendStoreSupportsPosixPrivacy()) {
		return true;
	}
	return AccountBackendStoreHasPrivatePermissions(path);
}

enum class AccountBackendStoreReadStatus : quint8 {
	Missing = 0,
	Valid = 1,
	Invalid = 2,
};

enum class AccountBackendStoreWriteStatus : quint8 {
	Written = 0,
	Unchanged = 1,
	Failed = 2,
	Rejected = 3,
};

struct AccountBackendStoreRecord {
	quint32 version = 0;
	QByteArray identity;
	AccountBackend backend = AccountBackend::Unspecified;
};

struct AccountBackendStoreRead {
	AccountBackendStoreReadStatus status
		= AccountBackendStoreReadStatus::Missing;
	AccountBackendStoreRecord record;
};

[[nodiscard]] inline bool IsSafeAccountBackendStoreIdentity(
		const QByteArray &identity) {
	if (identity.isEmpty() || identity.size() > 64) {
		return false;
	}
	for (const auto ch : identity) {
		const auto c = uchar(ch);
		const auto hex = (c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'F')
			|| (c >= 'a' && c <= 'f');
		if (!hex) {
			return false;
		}
	}
	return true;
}

[[nodiscard]] inline QString AccountBackendStoreDirectory(
		const QString &tdataRoot) {
	if (tdataRoot.isEmpty()) {
		return {};
	}
	auto root = tdataRoot;
	if (!root.endsWith(QLatin1Char('/'))
		&& !root.endsWith(QLatin1Char('\\'))) {
		root += QLatin1Char('/');
	}
	return root
		+ QLatin1String(kAccountBackendStoreDirectoryName)
		+ QLatin1Char('/');
}

[[nodiscard]] inline QString AccountBackendStorePath(
		const QString &tdataRoot,
		const QByteArray &identity) {
	if (!IsSafeAccountBackendStoreIdentity(identity)) {
		return {};
	}
	const auto directory = AccountBackendStoreDirectory(tdataRoot);
	if (directory.isEmpty()) {
		return {};
	}
	return directory + QString::fromLatin1(identity.constData(), identity.size());
}

[[nodiscard]] inline QByteArray SerializeAccountBackendStorePayload(
		quint32 version,
		const QByteArray &identity,
		quint32 backendTag) {
	auto bytes = QByteArray();
	QDataStream stream(&bytes, QIODevice::WriteOnly);
	stream.setVersion(kAccountBackendStoreStreamVersion);
	stream.setByteOrder(QDataStream::BigEndian);
	stream.writeRawData(
		kAccountBackendStoreMagic,
		kAccountBackendStoreMagicSize);
	stream << version << identity << backendTag;
	return bytes;
}

[[nodiscard]] inline AccountBackendStoreRead DecodeAccountBackendStorePayload(
		const QByteArray &bytes,
		const QByteArray &expectedIdentity) {
	AccountBackendStoreRead out;
	out.status = AccountBackendStoreReadStatus::Invalid;
	if (!bytes.startsWith(QByteArray::fromRawData(
			kAccountBackendStoreMagic,
			kAccountBackendStoreMagicSize))) {
		return out;
	}
	QDataStream stream(bytes);
	stream.setVersion(kAccountBackendStoreStreamVersion);
	stream.setByteOrder(QDataStream::BigEndian);
	if (stream.skipRawData(kAccountBackendStoreMagicSize)
		!= kAccountBackendStoreMagicSize) {
		return out;
	}
	quint32 version = 0;
	QByteArray identity;
	quint32 tag = 0;
	stream >> version >> identity >> tag;
	if (stream.status() != QDataStream::Ok || !stream.atEnd()) {
		return out;
	} else if (version != kAccountBackendStoreVersion) {
		return out;
	} else if (!IsSafeAccountBackendStoreIdentity(identity)
		|| identity != expectedIdentity) {
		return out;
	} else if (!IsValidAccountBackendTag(tag)) {
		return out;
	}
	out.status = AccountBackendStoreReadStatus::Valid;
	out.record.version = version;
	out.record.identity = identity;
	out.record.backend = DecodeAccountBackendTag(tag);
	return out;
}

[[nodiscard]] inline AccountBackendStoreRead ReadAccountBackendStore(
		const QString &path,
		const QByteArray &expectedIdentity) {
	AccountBackendStoreRead out;
	if (path.isEmpty() || !IsSafeAccountBackendStoreIdentity(expectedIdentity)) {
		out.status = AccountBackendStoreReadStatus::Invalid;
		return out;
	}
	QFile file(path);
	if (!file.exists()) {
		out.status = AccountBackendStoreReadStatus::Missing;
		return out;
	}
	if (!file.open(QIODevice::ReadOnly)) {
		out.status = AccountBackendStoreReadStatus::Invalid;
		return out;
	}
	return DecodeAccountBackendStorePayload(file.readAll(), expectedIdentity);
}

[[nodiscard]] inline AccountBackendStoreWriteStatus WriteAccountBackendStore(
		const QString &path,
		const QByteArray &identity,
		AccountBackend backend) {
	if (path.isEmpty()
		|| !IsSafeAccountBackendStoreIdentity(identity)
		|| !IsBoundAccountBackend(backend)) {
		return AccountBackendStoreWriteStatus::Rejected;
	}
	const auto payload = SerializeAccountBackendStorePayload(
		kAccountBackendStoreVersion,
		identity,
		EncodeAccountBackendTag(backend));
	QFile existing(path);
	if (existing.exists()) {
		if (!existing.open(QIODevice::ReadOnly)) {
			return AccountBackendStoreWriteStatus::Failed;
		}
		const auto same = existing.readAll() == payload;
		existing.close();
		if (same) {
			if (!AccountBackendStoreApplyPrivatePermissions(path)) {
				return AccountBackendStoreWriteStatus::Failed;
			}
			return AccountBackendStoreWriteStatus::Unchanged;
		}
	}
	const auto directory = QFileInfo(path).absolutePath();
	if (directory.isEmpty() || !QDir().mkpath(directory)) {
		return AccountBackendStoreWriteStatus::Failed;
	}
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly)) {
		return AccountBackendStoreWriteStatus::Failed;
	}
	if (file.write(payload) != payload.size()) {
		file.cancelWriting();
		return AccountBackendStoreWriteStatus::Failed;
	}
	if (QFileInfo::exists(path)
		&& !AccountBackendStoreApplyPrivatePermissions(path)) {
		file.cancelWriting();
		return AccountBackendStoreWriteStatus::Failed;
	}
	if (!file.commit()) {
		return AccountBackendStoreWriteStatus::Failed;
	}
	if (!AccountBackendStoreApplyPrivatePermissions(path)) {
		return AccountBackendStoreWriteStatus::Failed;
	}
	return AccountBackendStoreWriteStatus::Written;
}

inline void ApplyAccountBackendStoreRead(
		AccountBackendMigrationEvidence &evidence,
		const AccountBackendStoreRead &read) {
	switch (read.status) {
	case AccountBackendStoreReadStatus::Missing:
		evidence.tagPresent = false;
		evidence.storedTag = 0;
		return;
	case AccountBackendStoreReadStatus::Valid:
		evidence.tagPresent = true;
		evidence.storedTag = EncodeAccountBackendTag(read.record.backend);
		return;
	case AccountBackendStoreReadStatus::Invalid:
		// WHY: corrupt/unsupported metadata must not fall through to Telegram keys.
		evidence.tagPresent = true;
		evidence.storedTag = kAccountBackendStoreInvalidTag;
		return;
	}
}

[[nodiscard]] inline AccountBackendMigrationEvidence MakeAccountBackendMigrationEvidence(
		AccountBackendRecordKind record,
		const AccountBackendStoreRead &store,
		bool telegramAuthorizationKeysPresent,
		bool provenLegacyCoreMarker = false) {
	AccountBackendMigrationEvidence evidence;
	evidence.record = record;
	evidence.telegramAuthorizationKeysPresent
		= telegramAuthorizationKeysPresent;
	evidence.provenLegacyCoreMarker = provenLegacyCoreMarker;
	ApplyAccountBackendStoreRead(evidence, store);
	return evidence;
}

} // namespace Main
