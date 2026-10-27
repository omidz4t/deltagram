#include "main/session_backend_features.h"

#include <QtTest/QtTest>

using Main::AccountBackend;
using Main::SessionPremiumCanBuy;
using Main::SessionPremiumInputs;
using Main::SessionPremiumPossible;
using Main::SessionPremiumActive;
using Main::SessionPremiumPurchaseAllowedFromAppConfig;
using Main::SessionPremiumUsesTelegramBackend;

namespace {

[[nodiscard]] SessionPremiumInputs TelegramInputs(
		bool userIsPremium,
		bool purchaseAllowed = false) {
	return {
		.backend = AccountBackend::Telegram,
		.userIsPremium = userIsPremium,
		.premiumPurchaseAllowed = purchaseAllowed,
	};
}

[[nodiscard]] SessionPremiumInputs CoreInputs(
		bool userIsPremium,
		bool purchaseAllowed = false) {
	return {
		.backend = AccountBackend::Core,
		.userIsPremium = userIsPremium,
		.premiumPurchaseAllowed = purchaseAllowed,
	};
}

[[nodiscard]] SessionPremiumInputs UnspecifiedInputs(
		bool userIsPremium,
		bool purchaseAllowed = false) {
	return {
		.backend = AccountBackend::Unspecified,
		.userIsPremium = userIsPremium,
		.premiumPurchaseAllowed = purchaseAllowed,
	};
}

} // namespace

class SessionBackendFeaturesTests final : public QObject {
	Q_OBJECT

private slots:
	void onlyTelegramBackendUsesDonorPremiumEntitlement() {
		QVERIFY(SessionPremiumUsesTelegramBackend(AccountBackend::Telegram));
		QVERIFY(!SessionPremiumUsesTelegramBackend(AccountBackend::Core));
		QVERIFY(!SessionPremiumUsesTelegramBackend(
			AccountBackend::Unspecified));
	}

	void telegramPremiumMatchesDonorUserFlag() {
		QVERIFY(SessionPremiumActive(TelegramInputs(true)));
		QVERIFY(!SessionPremiumActive(TelegramInputs(false)));
		QVERIFY(SessionPremiumPossible(TelegramInputs(true)));
		QVERIFY(!SessionPremiumPossible(TelegramInputs(false)));
		QVERIFY(!SessionPremiumCanBuy(TelegramInputs(true)));
	}

	void telegramCanBuyFollowsAppConfigPurchaseFlag() {
		QVERIFY(SessionPremiumCanBuy(TelegramInputs(false, true)));
		QVERIFY(SessionPremiumPossible(TelegramInputs(false, true)));
		QVERIFY(!SessionPremiumActive(TelegramInputs(false, true)));
		QVERIFY(!SessionPremiumCanBuy(TelegramInputs(false, false)));
	}

	void telegramPremiumStillPossibleWhenPurchaseBlocked() {
		const auto blockedPremium = TelegramInputs(true, false);
		QVERIFY(SessionPremiumActive(blockedPremium));
		QVERIFY(!SessionPremiumCanBuy(blockedPremium));
		QVERIFY(SessionPremiumPossible(blockedPremium));
	}

	void coreUserPremiumDoesNotGrantTelegramEntitlement() {
		const auto corePremium = CoreInputs(true, true);
		QVERIFY(!SessionPremiumActive(corePremium));
		QVERIFY(!SessionPremiumCanBuy(corePremium));
		QVERIFY(!SessionPremiumPossible(corePremium));
	}

	void unspecifiedUserPremiumDoesNotGrantTelegramEntitlement() {
		const auto chooser = UnspecifiedInputs(true, true);
		QVERIFY(!SessionPremiumActive(chooser));
		QVERIFY(!SessionPremiumCanBuy(chooser));
		QVERIFY(!SessionPremiumPossible(chooser));
	}

	void donorPurchaseBlockedFallbackDisallowsBuyUntilServerAllows() {
		QVERIFY(!SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Telegram,
		}));
		QVERIFY(SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Telegram,
			.premiumPurchaseBlocked = false,
		}));
		QVERIFY(!SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Telegram,
			.premiumPurchaseBlocked = true,
		}));
	}

	void macStoreBuildNeverEnablesTelegramPurchase() {
		QVERIFY(!SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Telegram,
			.macStoreBuild = true,
			.premiumPurchaseBlocked = false,
		}));
	}

	void coreAndUnspecifiedNeverTakeTelegramPurchaseConfig() {
		QVERIFY(!SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Core,
			.premiumPurchaseBlocked = false,
		}));
		QVERIFY(!SessionPremiumPurchaseAllowedFromAppConfig({
			.backend = AccountBackend::Unspecified,
			.premiumPurchaseBlocked = false,
		}));
	}

};

QTEST_GUILESS_MAIN(SessionBackendFeaturesTests)
#include "session_backend_features_tests.moc"
