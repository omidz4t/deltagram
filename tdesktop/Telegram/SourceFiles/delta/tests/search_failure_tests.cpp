#include "search_failure.h"

#include <QtTest/QtTest>

using namespace Qt::StringLiterals;

class SearchFailureTests final : public QObject {
	Q_OBJECT

private slots:
	void initialIdErrorIsFailureNotEmptySuccess() {
		Delta::CoreSearchPager pager;
		pager.begin();
		QVERIFY(pager.loading);
		const auto ui = pager.finishIds({}, u"Core returned invalid search results."_s);
		QCOMPARE(ui, Delta::CoreSearchUi::FailedInitial);
		QVERIFY(pager.failed);
		QVERIFY(!pager.loading);
		QCOMPARE(pager.offset, 0);
		QVERIFY(pager.ids.isEmpty());
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::FindIds);
		QVERIFY(!pager.hasMore());
		QCOMPARE(
			Delta::ClassifyFindIds({}, 0),
			Delta::CoreSearchUi::SuccessEmpty);
	}

	void metadataFailKeepsOffsetZeroAndRetriesPage() {
		Delta::CoreSearchPager pager;
		QCOMPARE(
			pager.finishIds({ 11, 12, 13 }, {}),
			Delta::CoreSearchUi::IdsReady);
		pager.begin();
		const auto page = pager.pageIds();
		QCOMPARE(page.size(), 3);
		const auto ui = pager.finishPage(
			page.size(),
			u"Search result names could not be confirmed."_s);
		QCOMPARE(ui, Delta::CoreSearchUi::FailedInitial);
		QCOMPARE(pager.offset, 0);
		QCOMPARE(pager.ids.size(), 3);
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::LoadPage);
		QVERIFY(pager.hasMore());
	}

	void messageFetchFailMatchesMetadataFail() {
		Delta::CoreSearchPager pager;
		QCOMPARE(pager.finishIds({ 21 }, {}), Delta::CoreSearchUi::IdsReady);
		pager.begin();
		const auto ui = pager.finishPage(
			1,
			u"Core returned invalid search results."_s);
		QCOMPARE(ui, Delta::CoreSearchUi::FailedInitial);
		QCOMPARE(pager.offset, 0);
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::LoadPage);
	}

	void loadMoreErrorKeepsPreviousHitsOffset() {
		auto ids = QVector<int>();
		for (auto i = 1; i <= 60; ++i) {
			ids.push_back(i);
		}
		Delta::CoreSearchPager pager;
		QCOMPARE(pager.finishIds(ids, {}), Delta::CoreSearchUi::IdsReady);
		pager.begin();
		const auto first = pager.pageIds();
		QCOMPARE(int(first.size()), Delta::kCoreSearchPageLimit);
		QCOMPARE(
			pager.finishPage(first.size(), {}),
			Delta::CoreSearchUi::PageReady);
		QCOMPARE(pager.offset, Delta::kCoreSearchPageLimit);
		QVERIFY(pager.hasMore());
		pager.begin();
		const auto second = pager.pageIds();
		QCOMPARE(int(second.size()), 10);
		const auto ui = pager.finishPage(second.size(), u"unavailable"_s);
		QCOMPARE(ui, Delta::CoreSearchUi::FailedLoadMore);
		QCOMPARE(pager.offset, Delta::kCoreSearchPageLimit);
		QCOMPARE(pager.ids.size(), 60);
		QVERIFY(pager.hasMore());
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::LoadPage);
	}

	void retryCurrentPageOnceAdvancesOnlyOnSuccess() {
		auto ids = QVector<int>();
		for (auto i = 1; i <= 60; ++i) {
			ids.push_back(i);
		}
		Delta::CoreSearchPager pager;
		QCOMPARE(pager.finishIds(ids, {}), Delta::CoreSearchUi::IdsReady);
		pager.begin();
		QCOMPARE(
			pager.finishPage(Delta::kCoreSearchPageLimit, {}),
			Delta::CoreSearchUi::PageReady);
		pager.begin();
		QCOMPARE(pager.finishPage(10, u"fail"_s), Delta::CoreSearchUi::FailedLoadMore);
		QCOMPARE(pager.offset, Delta::kCoreSearchPageLimit);
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::LoadPage);
		pager.begin();
		QVERIFY(pager.loading);
		QVERIFY(!pager.failed);
		QCOMPARE(pager.offset, Delta::kCoreSearchPageLimit);
		const auto page = pager.pageIds();
		QCOMPARE(int(page.size()), 10);
		QCOMPARE(pager.finishPage(page.size(), {}), Delta::CoreSearchUi::PageReady);
		QCOMPARE(pager.offset, 60);
		QVERIFY(!pager.hasMore());
		QVERIFY(!pager.failed);
	}

	void newQueryAndAccountSwitchDropStaleOutcomes() {
		auto generation = std::uint64_t(4);
		auto account = 3;
		auto query = u"device"_s;
		Delta::CoreSearchPager pager;
		pager.begin();
		const auto staleGeneration = generation;
		const auto staleAccount = account;
		const auto staleQuery = query;
		++generation;
		account = 8;
		query = u"other"_s;
		pager.reset();
		pager.begin();
		QVERIFY(!Delta::CoreSearchCallbackLive(
			staleGeneration,
			generation,
			staleAccount,
			account,
			staleQuery,
			query));
		QVERIFY(pager.loading);
		QVERIFY(!pager.failed);
		QCOMPARE(pager.offset, 0);
		QVERIFY(pager.ids.isEmpty());
		QCOMPARE(
			pager.finishIds({ 11 }, {}),
			Delta::CoreSearchUi::IdsReady);
		QVERIFY(Delta::CoreSearchCallbackLive(
			generation,
			generation,
			account,
			account,
			query,
			query));
	}

	void pendingCancelClearsLoadingAndFailure() {
		Delta::CoreSearchPager pager;
		pager.begin();
		QCOMPARE(
			pager.finishIds({}, u"offline"_s),
			Delta::CoreSearchUi::FailedInitial);
		QVERIFY(pager.failed);
		pager.reset();
		QVERIFY(!pager.loading);
		QVERIFY(!pager.failed);
		QCOMPARE(pager.offset, 0);
		QVERIFY(pager.ids.isEmpty());
	}

	void normalEmptyResultsStayDistinctFromFailure() {
		Delta::CoreSearchPager empty;
		QCOMPARE(empty.finishIds({}, {}), Delta::CoreSearchUi::SuccessEmpty);
		QVERIFY(!empty.failed);
		QVERIFY(!empty.loading);
		QCOMPARE(empty.offset, 0);
		QVERIFY(empty.ids.isEmpty());
		QVERIFY(!empty.hasMore());

		Delta::CoreSearchPager failed;
		QCOMPARE(
			failed.finishIds({}, u"Delta Chat is not running."_s),
			Delta::CoreSearchUi::FailedInitial);
		QVERIFY(failed.failed);
		QCOMPARE(failed.retryStage(), Delta::CoreSearchStage::FindIds);
		QVERIFY(Delta::CoreSearchUiFailed(failed.finishIds(
			{},
			u"Delta Chat is not running."_s)));
		QVERIFY(!Delta::CoreSearchUiFailed(Delta::CoreSearchUi::SuccessEmpty));
	}

	void successPageDoesNotShortenAsComplete() {
		auto ids = QVector<int>();
		for (auto i = 1; i <= 51; ++i) {
			ids.push_back(i);
		}
		Delta::CoreSearchPager pager;
		QCOMPARE(pager.finishIds(ids, {}), Delta::CoreSearchUi::IdsReady);
		pager.begin();
		const auto page = pager.pageIds();
		QCOMPARE(int(page.size()), Delta::kCoreSearchPageLimit);
		QCOMPARE(pager.finishPage(page.size(), {}), Delta::CoreSearchUi::PageReady);
		QVERIFY(pager.hasMore());
		QCOMPARE(pager.ids.size(), 51);
		QCOMPARE(
			Delta::OffsetAfterLoadPage(0, 50, u"fail"_s),
			0);
	}

	void finishIdsLeavesFirstPageForWidgetDispatch() {
		Delta::CoreSearchPager pager;
		pager.reset();
		pager.begin();
		QVERIFY(pager.loading);
		QCOMPARE(
			pager.finishIds({ 11, 12 }, {}),
			Delta::CoreSearchUi::IdsReady);
		QCOMPARE(pager.offset, 0);
		QVERIFY(!pager.loading);
		QVERIFY(!pager.failed);
		QCOMPARE(pager.pageIds(), (QVector<int>{ 11, 12 }));
		QVERIFY(pager.hasMore());
	}

	void loadMoreIdleWhileBusyOrFailed() {
		Delta::CoreSearchPager pager;
		pager.begin();
		QVERIFY(pager.loading);
		QVERIFY(!pager.failed);
		QCOMPARE(pager.finishIds({ 1 }, {}), Delta::CoreSearchUi::IdsReady);
		pager.begin();
		QCOMPARE(
			pager.finishPage(1, u"fail"_s),
			Delta::CoreSearchUi::FailedInitial);
		QVERIFY(pager.failed);
		QVERIFY(!pager.loading);
		QCOMPARE(pager.retryStage(), Delta::CoreSearchStage::LoadPage);
		QCOMPARE(pager.offset, 0);
		QCOMPARE(pager.pageIds(), (QVector<int>{ 1 }));
		QVERIFY(pager.hasMore());
	}

};

QTEST_GUILESS_MAIN(SearchFailureTests)
#include "search_failure_tests.moc"
