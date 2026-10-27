/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "main/account_backend_store.h"

#include <QtCore/QSet>
#include <vector>

namespace Delta {

enum class PasscodeRecovery : quint8 {
	TelegramLogout,
	PreserveChatmail,
	PreserveUnknown,
};

// Read-only evidence. Never open a Core database or launch its server while
// the local UI is locked. Missing metadata cannot prove Telegram-only storage.
[[nodiscard]] inline PasscodeRecovery InspectPasscodeRecoveryStorage(
		const QString &tdataRoot,
		const QString &corePath,
		bool savedCorePathPresent) {
	if (savedCorePathPresent) {
		return PasscodeRecovery::PreserveChatmail;
	}
	const auto core = QFileInfo(corePath);
	if (corePath.isEmpty() || core.isSymLink()
		|| (core.exists() && (!core.isDir() || !core.isReadable()))) {
		return PasscodeRecovery::PreserveUnknown;
	}
	if (core.exists() && !QDir(corePath).entryList(
		QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) {
		return PasscodeRecovery::PreserveChatmail;
	}
	const auto root = QFileInfo(tdataRoot);
	if (!root.isDir() || !root.isReadable() || root.isSymLink()) {
		return PasscodeRecovery::PreserveUnknown;
	}
	const auto metadataPath = Main::AccountBackendStoreDirectory(tdataRoot);
	const auto metadata = QFileInfo(metadataPath);
	if (!metadata.isDir() || !metadata.isReadable() || metadata.isSymLink()) {
		return PasscodeRecovery::PreserveUnknown;
	}
	auto telegram = QSet<QString>();
	const auto entries = QDir(metadataPath).entryInfoList(
		QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
	for (const auto &entry : entries) {
		if (!entry.isFile() || entry.isSymLink() || entry.size() > 1024) {
			return PasscodeRecovery::PreserveUnknown;
		}
		const auto record = Main::ReadAccountBackendStore(
			entry.filePath(), entry.fileName().toUtf8());
		if (record.status != Main::AccountBackendStoreReadStatus::Valid) {
			return PasscodeRecovery::PreserveUnknown;
		}
		if (record.record.backend == Main::AccountBackend::Core) {
			return PasscodeRecovery::PreserveChatmail;
		} else if (record.record.backend != Main::AccountBackend::Telegram) {
			return PasscodeRecovery::PreserveUnknown;
		}
		telegram.insert(entry.fileName());
	}
	if (telegram.isEmpty()) {
		return PasscodeRecovery::PreserveUnknown;
	}
	const auto isIdentity = [](const QString &name) {
		return name.size() == 16
			&& Main::IsSafeAccountBackendStoreIdentity(name.toUtf8());
	};
	// An untagged legacy account beside a tagged Telegram account is unknown,
	// even though its encrypted map cannot be inspected before unlock.
	for (const auto &entry : QDir(tdataRoot).entryInfoList(
			QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot)) {
		const auto name = entry.fileName();
		auto identity = QString();
		if (isIdentity(name) && (entry.isDir() || entry.isSymLink())) {
			identity = name;
		} else if (name.size() == 17 && isIdentity(name.left(16))
			&& QStringLiteral("01s").contains(name.back())) {
			identity = name.left(16);
		}
		if (!identity.isEmpty()
			&& (entry.isSymLink() || !telegram.contains(identity))) {
			return PasscodeRecovery::PreserveUnknown;
		}
	}
	return PasscodeRecovery::TelegramLogout;
}

[[nodiscard]] inline PasscodeRecovery PlanPasscodeRecovery(
		bool protectedByPasscode,
		const std::vector<Main::AccountBackend> &loaded,
		PasscodeRecovery storage) {
	if (!protectedByPasscode) {
		return PasscodeRecovery::TelegramLogout;
	}
	for (const auto backend : loaded) {
		if (backend == Main::AccountBackend::Core) {
			return PasscodeRecovery::PreserveChatmail;
		} else if (backend != Main::AccountBackend::Telegram) {
			return PasscodeRecovery::PreserveUnknown;
		}
	}
	return storage;
}

} // namespace Delta
