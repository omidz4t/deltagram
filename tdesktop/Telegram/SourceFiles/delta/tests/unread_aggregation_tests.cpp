#include "../unread_aggregation.h"

#include <QtTest/QtTest>

class UnreadAggregationTests final : public QObject {
	Q_OBJECT

private slots:
	void combineAddsCoreAndTelegramCounts() {
		const auto combined = Delta::CombineUnread(
			{ .count = 4, .muted = false },
			{ .count = 3, .muted = true });
		QCOMPARE(combined.count, 7);
		QCOMPARE(combined.muted, false);
	}

	void mutedWhenCoreCountPositive() {
		const auto combined = Delta::CombineUnread(
			{ .count = 2, .muted = true },
			{ .count = 9, .muted = true });
		QCOMPARE(combined.count, 11);
		QCOMPARE(combined.muted, false);
	}

	void mutedHonorsTelegramWhenCoreCountZero() {
		const auto muted = Delta::CombineUnread(
			{ .count = 0, .muted = true },
			{ .count = 5, .muted = true });
		QCOMPARE(muted.count, 5);
		QCOMPARE(muted.muted, true);
		const auto unmuted = Delta::CombineUnread(
			{ .count = 0, .muted = true },
			{ .count = 5, .muted = false });
		QCOMPARE(unmuted.count, 5);
		QCOMPARE(unmuted.muted, false);
	}

	void telegramDomainSkipsCoreSlotToAvoidDoubleCount() {
		const std::vector<Delta::DomainAccountUnread> accounts = {
			{ .coreBackend = true, .count = 11, .muted = false },
			{ .coreBackend = false, .count = 4, .muted = false },
			{ .coreBackend = false, .count = 2, .muted = true },
		};
		const auto telegram = Delta::TelegramDomainUnread(accounts);
		QCOMPARE(telegram.count, 6);
		QCOMPARE(telegram.muted, false);
		const auto combined = Delta::CombineUnread(
			{ .count = 11, .muted = false },
			telegram);
		QCOMPARE(combined.count, 17);
		QCOMPARE(combined.muted, false);
	}

	void otherTelegramSkipsCurrentAndCore() {
		const std::vector<Delta::DomainAccountUnread> accounts = {
			{ .coreBackend = true, .count = 8, .muted = false },
			{ .coreBackend = false, .count = 5, .muted = false },
			{ .coreBackend = false, .count = 1, .muted = true },
		};
		const auto others = Delta::OtherTelegramUnread(accounts, 1);
		QCOMPARE(others.count, 1);
		QCOMPARE(others.muted, true);
		const auto fromCoreWindow = Delta::CombineUnread(
			{ .count = 3, .muted = false },
			others);
		QCOMPARE(fromCoreWindow.count, 4);
		QCOMPARE(fromCoreWindow.muted, false);
		const auto fromTelegramWindow = Delta::CombineUnread(
			{ .count = 8, .muted = false },
			Delta::OtherTelegramUnread(accounts, 1));
		QCOMPARE(fromTelegramWindow.count, 9);
		QCOMPARE(fromTelegramWindow.muted, false);
	}

	void emptySlicesStayMutedZero() {
		const auto combined = Delta::CombineUnread({}, {});
		QCOMPARE(combined.count, 0);
		QCOMPARE(combined.muted, true);
		QCOMPARE(Delta::TelegramDomainUnread({}).count, 0);
		QCOMPARE(Delta::OtherTelegramUnread({}, 0).count, 0);
	}

};

QTEST_GUILESS_MAIN(UnreadAggregationTests)
#include "unread_aggregation_tests.moc"
