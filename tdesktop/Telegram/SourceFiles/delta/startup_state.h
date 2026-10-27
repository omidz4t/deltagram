#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVector>

namespace Delta {

inline const QString kStartupLoading = QStringLiteral("Loading profiles…");
inline const QString kStartupInvalidSelected = QStringLiteral(
	"Core returned an invalid selected profile.");

struct SelectedAccountRead {
	bool ok = false;
	int selectedId = 0;
	QString error;
};

struct StartupState {
	enum class Discovery : unsigned char {
		Pending,
		Failed,
		Ready,
	};
	enum class Action : unsigned char {
		None,
		RetryDiscovery,
		OpenKnown,
		Create,
	};

	Discovery discovery = Discovery::Pending;
	int knownAccountId = 0;
	bool completed = false;
	std::uint64_t generation = 0;
	QString error;

	std::uint64_t beginDiscovery() {
		++generation;
		discovery = Discovery::Pending;
		if (!completed) {
			knownAccountId = 0;
		}
		error.clear();
		return generation;
	}

	void failDiscovery(QString text) {
		discovery = Discovery::Failed;
		if (!completed) {
			knownAccountId = 0;
		}
		error = std::move(text);
	}

	void readyUnconfigured() {
		discovery = Discovery::Ready;
		knownAccountId = 0;
		completed = false;
		error.clear();
	}

	void readyConfigured(int accountId) {
		discovery = Discovery::Ready;
		knownAccountId = accountId;
		completed = false;
		error.clear();
	}

	void failOpen(QString text) {
		error = std::move(text);
	}

	void markCompleted(int accountId) {
		discovery = Discovery::Ready;
		knownAccountId = accountId;
		completed = true;
		error.clear();
	}

	void markLiveAdd() {
		discovery = Discovery::Ready;
		knownAccountId = 0;
		completed = false;
		error.clear();
	}

	[[nodiscard]] bool live(std::uint64_t seen) const {
		return seen == generation;
	}

	[[nodiscard]] bool completedOpen() const {
		return completed && knownAccountId > 0;
	}

	[[nodiscard]] bool canCreate() const {
		return discovery == Discovery::Ready
			&& knownAccountId == 0
			&& !completed;
	}

	[[nodiscard]] Action primaryAction() const {
		if (discovery == Discovery::Pending) {
			return Action::None;
		} else if (discovery == Discovery::Failed) {
			return Action::RetryDiscovery;
		} else if (knownAccountId != 0) {
			return Action::OpenKnown;
		}
		return Action::Create;
	}

	[[nodiscard]] bool nextEnabled() const {
		return primaryAction() != Action::None;
	}

	[[nodiscard]] QString nextLabel(bool welcomePage) const {
		switch (primaryAction()) {
		case Action::None:
			return QStringLiteral("Loading…");
		case Action::RetryDiscovery:
			return QStringLiteral("Retry");
		case Action::OpenKnown:
			return QStringLiteral("Open Profile");
		case Action::Create:
			return welcomePage
				? QStringLiteral("Create New Profile")
				: QStringLiteral("Agree & Create Profile");
		}
		return {};
	}
};

[[nodiscard]] inline std::optional<int> StartupExactId(const QJsonValue &value) {
	if (!value.isDouble()) {
		return std::nullopt;
	}
	const auto id = value.toInt();
	if (id <= 0 || value.toDouble() != double(id)) {
		return std::nullopt;
	}
	return id;
}

[[nodiscard]] inline SelectedAccountRead ReadSelectedAccount(
		const QJsonObject &reply) {
	if (reply.contains(QStringLiteral("error"))) {
		return { false, 0, kStartupInvalidSelected };
	}
	const auto value = reply.value(QStringLiteral("result"));
	if (value.isNull()) {
		return { true, 0, {} };
	}
	if (const auto id = StartupExactId(value)) {
		return { true, *id, {} };
	}
	return { false, 0, kStartupInvalidSelected };
}

[[nodiscard]] inline int ConfiguredOpenIdOrZero(
		const QVector<int> &configured,
		const SelectedAccountRead &read) {
	if (!read.ok || configured.isEmpty()) {
		return 0;
	}
	if (read.selectedId > 0 && configured.contains(read.selectedId)) {
		return read.selectedId;
	}
	return configured.front();
}

} // namespace Delta
