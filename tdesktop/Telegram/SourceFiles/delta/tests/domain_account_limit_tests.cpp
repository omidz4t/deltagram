#include "main/domain_account_limit.h"

#include <QtTest/QtTest>

using Main::AccountBackend;
using Main::DomainAccountLimitSlot;
using Main::DomainAccountCanRemoveAsRedundant;
using Main::DomainAccountCanClearPasscode;
using Main::DomainAddActivatedReuseCandidate;
using Main::DomainAccountLimitCountsTelegramPremium;
using Main::ResolveDomainMaxAccounts;

namespace {

constexpr auto kFree = 3;
constexpr auto kPremiumCap = 6;

[[nodiscard]] DomainAccountLimitSlot TelegramSlot(
		bool sessionExists,
		bool premium = false,
		bool testMode = false) {
	return {
		.backend = AccountBackend::Telegram,
		.sessionExists = sessionExists,
		.premium = premium,
		.testMode = testMode,
	};
}

[[nodiscard]] DomainAccountLimitSlot CoreSlot(
		bool sessionExists,
		bool premium = false,
		bool testMode = false) {
	return {
		.backend = AccountBackend::Core,
		.sessionExists = sessionExists,
		.premium = premium,
		.testMode = testMode,
	};
}

[[nodiscard]] DomainAccountLimitSlot UnspecifiedSlot(
		bool sessionExists,
		bool premium = false,
		bool testMode = false) {
	return {
		.backend = AccountBackend::Unspecified,
		.sessionExists = sessionExists,
		.premium = premium,
		.testMode = testMode,
	};
}

} // namespace

class DomainAccountLimitTests final : public QObject {
	Q_OBJECT

private slots:
	void telegramOnlyNoPremiumIsDonorFreeLimit() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			TelegramSlot(true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 3);
	}

	void emptySlotsStillUseFreeLimit() {
		QCOMPARE(ResolveDomainMaxAccounts({}, kFree, kPremiumCap), 3);
	}

	void eachTelegramPremiumRaisesLimitUntilCap() {
		QCOMPARE(
			ResolveDomainMaxAccounts(
				{ TelegramSlot(true, true) },
				kFree,
				kPremiumCap),
			4);
		QCOMPARE(
			ResolveDomainMaxAccounts(
				{
					TelegramSlot(true, true),
					TelegramSlot(true, true),
				},
				kFree,
				kPremiumCap),
			5);
		QCOMPARE(
			ResolveDomainMaxAccounts(
				{
					TelegramSlot(true, true),
					TelegramSlot(true, true),
					TelegramSlot(true, true),
				},
				kFree,
				kPremiumCap),
			6);
		QCOMPARE(
			ResolveDomainMaxAccounts(
				{
					TelegramSlot(true, true),
					TelegramSlot(true, true),
					TelegramSlot(true, true),
					TelegramSlot(true, true),
				},
				kFree,
				kPremiumCap),
			6);
	}

	void telegramTestModeCountsAsPremiumEntitlement() {
		QCOMPARE(
			ResolveDomainMaxAccounts(
				{ TelegramSlot(true, false, true) },
				kFree,
				kPremiumCap),
			4);
		QVERIFY(DomainAccountLimitCountsTelegramPremium(
			TelegramSlot(true, false, true)));
	}

	void telegramWithoutSessionDoesNotCount() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			TelegramSlot(false, true, true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 3);
		QVERIFY(!DomainAccountLimitCountsTelegramPremium(accountStates.front()));
	}

	void corePremiumAndTestDoNotExpandTelegramEntitlement() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			CoreSlot(true, true, true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 3);
		QVERIFY(!DomainAccountLimitCountsTelegramPremium(accountStates.front()));
	}

	void unspecifiedChooserPremiumDoesNotExpand() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			UnspecifiedSlot(true, true, true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 3);
		QVERIFY(!DomainAccountLimitCountsTelegramPremium(accountStates.front()));
	}

	void mixedCoreLeavesRoomToAddTelegramAtFreeLimit() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			CoreSlot(true),
		};
		const auto maxAccounts = ResolveDomainMaxAccounts(
			accountStates,
			kFree,
			kPremiumCap);
		QCOMPARE(maxAccounts, 3);
		QVERIFY(int(accountStates.size()) < maxAccounts);
	}

	void mixedCorePlusTelegramPremiumDoesNotExceedStorageCap() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			CoreSlot(true, true, true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 6);
	}

	void mixedCoreDoesNotInventSeventhSlot() {
		const std::vector<DomainAccountLimitSlot> accountStates = {
			CoreSlot(true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
			TelegramSlot(true, true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(accountStates, kFree, kPremiumCap), 6);
		QVERIFY(int(accountStates.size()) <= kPremiumCap);
	}

	void telegramOnlyMatchesDonorWhenNoCorePresent() {
		const std::vector<DomainAccountLimitSlot> freeSlots = {
			TelegramSlot(true),
			TelegramSlot(true),
			TelegramSlot(true),
		};
		QCOMPARE(ResolveDomainMaxAccounts(freeSlots, kFree, kPremiumCap), 3);
		const std::vector<DomainAccountLimitSlot> premiumSlots = {
			TelegramSlot(true, true),
			TelegramSlot(true),
			TelegramSlot(true),
		};
		QCOMPARE(
			ResolveDomainMaxAccounts(premiumSlots, kFree, kPremiumCap),
			4);
	}

	void reuseSkipsCoreAndAuthedSessions() {
		QVERIFY(!DomainAddActivatedReuseCandidate(
			AccountBackend::Core,
			false,
			true));
		QVERIFY(!DomainAddActivatedReuseCandidate(
			AccountBackend::Telegram,
			true,
			true));
		QVERIFY(!DomainAddActivatedReuseCandidate(
			AccountBackend::Unspecified,
			true,
			true));
		QVERIFY(!DomainAddActivatedReuseCandidate(
			AccountBackend::Telegram,
			false,
			false));
	}

	void reuseAllowsEmptyUnspecifiedAndTelegram() {
		QVERIFY(DomainAddActivatedReuseCandidate(
			AccountBackend::Unspecified,
			false,
			true));
		QVERIFY(DomainAddActivatedReuseCandidate(
			AccountBackend::Telegram,
			false,
			true));
	}

	void cleanupKeepsCoreContainerBeforeSessionOpens() {
		QVERIFY(!DomainAccountCanRemoveAsRedundant(
			AccountBackend::Core, false, false));
		QVERIFY(!DomainAccountCanRemoveAsRedundant(
			AccountBackend::Core, true, false));
		QVERIFY(!DomainAccountCanRemoveAsRedundant(
			AccountBackend::Core, false, true));
	}

	void cleanupRetainsDonorLoginSlotRules() {
		for (const auto backend : {
				AccountBackend::Telegram,
				AccountBackend::Unspecified }) {
			QVERIFY(DomainAccountCanRemoveAsRedundant(
				backend, false, false));
			QVERIFY(!DomainAccountCanRemoveAsRedundant(
				backend, true, false));
			QVERIFY(!DomainAccountCanRemoveAsRedundant(
				backend, false, true));
		}
	}

	void unopenedCoreAndUnresolvedAccountsKeepPasscode() {
		for (const auto backend : {
				AccountBackend::Core,
				AccountBackend::Unspecified }) {
			QVERIFY(!DomainAccountCanClearPasscode(backend, false, 1));
			QVERIFY(!DomainAccountCanClearPasscode(backend, true, 1));
		}
	}

	void onlyLastLoggedOutTelegramAccountClearsPasscode() {
		QVERIFY(DomainAccountCanClearPasscode(
			AccountBackend::Telegram, false, 1));
		QVERIFY(!DomainAccountCanClearPasscode(
			AccountBackend::Telegram, true, 1));
		QVERIFY(!DomainAccountCanClearPasscode(
			AccountBackend::Telegram, false, 2));
		QVERIFY(!DomainAccountCanClearPasscode(
			AccountBackend::Telegram, false, 0));
	}

};

QTEST_GUILESS_MAIN(DomainAccountLimitTests)
#include "domain_account_limit_tests.moc"
