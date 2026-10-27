/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend_store.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QLatin1Char>
#include <QtCore/QLatin1String>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <cstring>
#include <limits>

namespace Main {

inline constexpr auto kAccountStorageIndexSearchLimit = 1024;
inline constexpr auto kMaxAccountStorageIndex
	= std::numeric_limits<int>::max() - 1;

[[nodiscard]] inline bool IsRepresentableAccountStorageIndex(int index) {
	return index >= 0 && index <= kMaxAccountStorageIndex;
}

[[nodiscard]] inline QString ComposeAccountStorageDataName(
		const QString &dataName,
		int index) {
	auto result = dataName;
	result.replace(QLatin1Char('#'), QString());
	if (index > 0) {
		result += QLatin1Char('#') + QString::number(index + 1);
	}
	return result;
}

[[nodiscard]] inline quint64 AccountStorageDataNameKey(
		const QString &dataName) {
	const auto utf8 = dataName.toUtf8();
	const auto digest = QCryptographicHash::hash(
		utf8,
		QCryptographicHash::Md5);
	quint64 key = 0;
	if (digest.size() >= int(sizeof(key))) {
		std::memcpy(&key, digest.constData(), sizeof(key));
	}
	return key;
}

[[nodiscard]] inline QString AccountStorageFilePart(quint64 val) {
	QString result;
	result.reserve(0x10);
	for (int i = 0; i < 0x10; ++i) {
		const auto v = int(val & 0x0F);
		result.push_back((v < 0x0A)
			? QChar('0' + v)
			: QChar('A' + (v - 0x0A)));
		val >>= 4;
	}
	return result;
}

[[nodiscard]] inline QByteArray AccountStorageIdentityFromKey(quint64 key) {
	return AccountStorageFilePart(key).toLatin1();
}

[[nodiscard]] inline QByteArray AccountStorageIdentity(
		const QString &composedDataName) {
	return AccountStorageIdentityFromKey(
		AccountStorageDataNameKey(composedDataName));
}

[[nodiscard]] inline QString AccountStorageTdataJoin(
		const QString &tdataRoot,
		const QString &name) {
	if (tdataRoot.isEmpty()) {
		return {};
	}
	auto root = tdataRoot;
	if (!root.endsWith(QLatin1Char('/'))
		&& !root.endsWith(QLatin1Char('\\'))) {
		root += QLatin1Char('/');
	}
	return root + name;
}

[[nodiscard]] inline QString AccountStorageAuthStem(
		const QString &tdataRoot,
		const QByteArray &identity) {
	if (!IsSafeAccountBackendStoreIdentity(identity)) {
		return {};
	}
	return AccountStorageTdataJoin(
		tdataRoot,
		QString::fromLatin1(identity.constData(), identity.size()));
}

[[nodiscard]] inline QString AccountStorageMapDirectory(
		const QString &tdataRoot,
		const QByteArray &identity) {
	const auto stem = AccountStorageAuthStem(tdataRoot, identity);
	if (stem.isEmpty()) {
		return {};
	}
	return stem + QLatin1Char('/');
}

[[nodiscard]] inline bool AccountStorageIdentityOccupied(
		const QString &tdataRoot,
		const QByteArray &identity) {
	if (tdataRoot.isEmpty()
		|| !IsSafeAccountBackendStoreIdentity(identity)) {
		return false;
	}
	const auto sidecar = AccountBackendStorePath(tdataRoot, identity);
	if (!sidecar.isEmpty() && QFileInfo::exists(sidecar)) {
		return true;
	}
	const auto authStem = AccountStorageAuthStem(tdataRoot, identity);
	if (authStem.isEmpty()) {
		return false;
	}
	if (QFileInfo::exists(authStem + QLatin1Char('0'))
		|| QFileInfo::exists(authStem + QLatin1Char('1'))
		|| QFileInfo::exists(authStem + QLatin1Char('s'))) {
		return true;
	}
	const auto mapDir = AccountStorageMapDirectory(tdataRoot, identity);
	if (mapDir.isEmpty()) {
		return false;
	}
	return QFileInfo::exists(mapDir + QLatin1String("map0"))
		|| QFileInfo::exists(mapDir + QLatin1String("map1"))
		|| QFileInfo::exists(mapDir + QLatin1String("maps"))
		|| QDir(mapDir).exists();
}

[[nodiscard]] inline bool AccountStorageNamespaceOccupied(
		const QString &tdataRoot,
		const QString &dataName,
		int index) {
	if (!IsRepresentableAccountStorageIndex(index)) {
		return true;
	}
	return AccountStorageIdentityOccupied(
		tdataRoot,
		AccountStorageIdentity(
			ComposeAccountStorageDataName(dataName, index)));
}

[[nodiscard]] inline int ChooseUnusedAccountStorageIndex(
		const QString &tdataRoot,
		const QString &dataName,
		const QList<int> &activeIndices,
		int searchLimit = kAccountStorageIndexSearchLimit) {
	if (searchLimit <= 0) {
		return -1;
	}
	const auto exclusive = (searchLimit > kMaxAccountStorageIndex)
		? (kMaxAccountStorageIndex + 1)
		: searchLimit;
	for (int index = 0; index < exclusive; ++index) {
		if (activeIndices.contains(index)) {
			continue;
		}
		if (!AccountStorageNamespaceOccupied(tdataRoot, dataName, index)) {
			return index;
		}
	}
	return -1;
}

} // namespace Main
