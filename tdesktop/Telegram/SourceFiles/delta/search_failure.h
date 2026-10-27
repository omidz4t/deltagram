#pragma once

#include <QString>
#include <QVector>

#include <cstdint>
#include <utility>

namespace Delta {

inline constexpr int kCoreSearchPageLimit = 50;

enum class CoreSearchStage {
	FindIds,
	LoadPage,
};

enum class CoreSearchUi {
	SuccessEmpty,
	IdsReady,
	PageReady,
	FailedInitial,
	FailedLoadMore,
};

[[nodiscard]] inline bool CoreSearchCallbackLive(
		std::uint64_t generation,
		std::uint64_t currentGeneration,
		int accountGeneration,
		int currentAccount,
		const QString &query,
		const QString &currentQuery) {
	return generation == currentGeneration
		&& accountGeneration == currentAccount
		&& query == currentQuery;
}

[[nodiscard]] inline CoreSearchUi ClassifyFindIds(
		const QString &error,
		int idCount) {
	if (!error.isEmpty()) {
		return CoreSearchUi::FailedInitial;
	}
	return (idCount == 0)
		? CoreSearchUi::SuccessEmpty
		: CoreSearchUi::IdsReady;
}

[[nodiscard]] inline CoreSearchUi ClassifyLoadPage(
		const QString &error,
		int offset) {
	if (error.isEmpty()) {
		return CoreSearchUi::PageReady;
	}
	return (offset == 0)
		? CoreSearchUi::FailedInitial
		: CoreSearchUi::FailedLoadMore;
}

[[nodiscard]] inline int OffsetAfterLoadPage(
		int offset,
		int pageSize,
		const QString &error) {
	return error.isEmpty() ? (offset + pageSize) : offset;
}

[[nodiscard]] inline bool CoreSearchUiFailed(CoreSearchUi ui) {
	return ui == CoreSearchUi::FailedInitial
		|| ui == CoreSearchUi::FailedLoadMore;
}

struct CoreSearchPager {
	QVector<int> ids;
	int offset = 0;
	bool loading = false;
	bool failed = false;
	CoreSearchStage failureStage = CoreSearchStage::FindIds;

	void reset() {
		ids.clear();
		offset = 0;
		loading = false;
		failed = false;
		failureStage = CoreSearchStage::FindIds;
	}

	void begin() {
		loading = true;
		failed = false;
	}

	[[nodiscard]] CoreSearchUi finishIds(
			QVector<int> found,
			const QString &error) {
		loading = false;
		const auto ui = ClassifyFindIds(error, found.size());
		if (ui == CoreSearchUi::FailedInitial) {
			ids.clear();
			offset = 0;
			failed = true;
			failureStage = CoreSearchStage::FindIds;
			return ui;
		}
		ids = std::move(found);
		offset = 0;
		failed = false;
		return ui;
	}

	[[nodiscard]] QVector<int> pageIds() const {
		return ids.mid(offset, kCoreSearchPageLimit);
	}

	[[nodiscard]] CoreSearchUi finishPage(int pageSize, const QString &error) {
		loading = false;
		const auto ui = ClassifyLoadPage(error, offset);
		offset = OffsetAfterLoadPage(offset, pageSize, error);
		if (CoreSearchUiFailed(ui)) {
			failed = true;
			failureStage = CoreSearchStage::LoadPage;
		} else {
			failed = false;
		}
		return ui;
	}

	[[nodiscard]] bool hasMore() const {
		return offset < ids.size();
	}

	[[nodiscard]] CoreSearchStage retryStage() const {
		return failureStage;
	}
};

} // namespace Delta
