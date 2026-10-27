/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QStandardPaths>

namespace Delta {

[[nodiscard]] inline QString CoreAccountsPathFile() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
		+ QStringLiteral("/delta-tel/accounts_path");
}

[[nodiscard]] inline QString ResolveCoreAccountsPath() {
	const auto args = QCoreApplication::arguments();
	for (auto i = 0; i + 1 < args.size(); ++i) {
		if (args[i] == QStringLiteral("-delta-accounts")
			|| args[i] == QStringLiteral("--delta-accounts")) {
			return args[i + 1];
		}
	}
	const auto env = qEnvironmentVariable("DELTA_TEL_ACCOUNTS");
	if (!env.isEmpty()) {
		return env;
	}
	auto file = QFile(CoreAccountsPathFile());
	if (file.open(QIODevice::ReadOnly)) {
		const auto saved = QString::fromUtf8(file.readAll()).trimmed();
		if (!saved.isEmpty()) {
			return saved;
		}
	}
	return qEnvironmentVariable("DC_ACCOUNTS_PATH",
		QDir::homePath() + QStringLiteral("/.local/share/delta-tel/accounts"));
}

} // namespace Delta
