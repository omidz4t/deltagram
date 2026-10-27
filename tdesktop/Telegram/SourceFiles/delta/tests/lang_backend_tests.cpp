#include "lang/lang_backend.h"

#include <QtTest/QtTest>

using Main::AccountBackend;
using Lang::CombineLanguageRestartCopy;
using Lang::LanguageBackingEvidence;
using Lang::LanguageBackingPick;
using Lang::LanguageCloudRequest;
using Lang::LanguageCloudRequestPolicy;
using Lang::LanguageCustomConfirmCopy;
using Lang::LanguagePickerFeatures;
using Lang::LanguageRequestOrigin;
using Lang::PlanLanguageBacking;
using Lang::PlanLanguageCloudRequest;
using Lang::PlanLanguageCustomConfirm;
using Lang::PlanLanguagePicker;
using Lang::PlanTranslateProvider;
using Lang::TranslateBackendAvailable;
using Lang::TranslateProviderKind;
using Lang::TranslateProviderPlan;

class LangBackendTests final : public QObject {
	Q_OBJECT

private slots:
	void coreWithoutEngineNeverSelectsMtproto() {
		const auto kind = PlanTranslateProvider({
			.backend = AccountBackend::Core,
			.urlTemplateValid = false,
			.platformAvailable = false,
			.usePlatformTranslation = false,
		});
		QCOMPARE(kind, TranslateProviderKind::Unavailable);
		QVERIFY(kind != TranslateProviderKind::Mtproto);
		QVERIFY(!TranslateBackendAvailable(AccountBackend::Core, false, false));
	}

	void coreValidUrlBeatsPlatformAndMtproto() {
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Core,
				.urlTemplateValid = true,
				.platformAvailable = true,
				.usePlatformTranslation = false,
			}),
			TranslateProviderKind::Url);
		QVERIFY(TranslateBackendAvailable(AccountBackend::Core, true, false));
	}

	void corePlatformWithoutUrlStaysOffMtproto() {
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Core,
				.urlTemplateValid = false,
				.platformAvailable = true,
				.usePlatformTranslation = false,
			}),
			TranslateProviderKind::Platform);
		QVERIFY(TranslateBackendAvailable(AccountBackend::Core, false, true));
	}

	void telegramWithoutEngineKeepsMtprotoWhileCoreWouldBeLive() {
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Telegram,
				.urlTemplateValid = false,
				.platformAvailable = false,
				.usePlatformTranslation = false,
			}),
			TranslateProviderKind::Mtproto);
		QVERIFY(TranslateBackendAvailable(
			AccountBackend::Telegram,
			false,
			false));
	}

	void telegramHonorsPlatformSettingBeforeMtproto() {
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Telegram,
				.urlTemplateValid = false,
				.platformAvailable = true,
				.usePlatformTranslation = true,
			}),
			TranslateProviderKind::Platform);
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Telegram,
				.urlTemplateValid = false,
				.platformAvailable = true,
				.usePlatformTranslation = false,
			}),
			TranslateProviderKind::Mtproto);
	}

	void unspecifiedIntroNeverGainsMtproto() {
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Unspecified,
				.urlTemplateValid = false,
				.platformAvailable = false,
				.usePlatformTranslation = true,
			}),
			TranslateProviderKind::Unavailable);
		QCOMPARE(
			PlanTranslateProvider({
				.backend = AccountBackend::Unspecified,
				.urlTemplateValid = false,
				.platformAvailable = true,
				.usePlatformTranslation = false,
			}),
			TranslateProviderKind::Platform);
		QVERIFY(!TranslateBackendAvailable(
			AccountBackend::Unspecified,
			false,
			false));
	}

	void coreAndUnspecifiedKeepLocalPicker() {
		const auto core = PlanLanguagePicker(AccountBackend::Core);
		QVERIFY(core.localCatalog);
		QVERIFY(!core.cloudCatalog);
		QVERIFY(!core.premiumChatTranslate);

		const auto intro = PlanLanguagePicker(AccountBackend::Unspecified);
		QVERIFY(intro.localCatalog);
		QVERIFY(!intro.cloudCatalog);
		QVERIFY(!intro.premiumChatTranslate);
	}

	void telegramPickerKeepsCloudCatalogAndPremiumControls() {
		const auto telegram = PlanLanguagePicker(AccountBackend::Telegram);
		QVERIFY(telegram.cloudCatalog);
		QVERIFY(telegram.premiumChatTranslate);
		QVERIFY(!telegram.localCatalog);
	}

	void coreWindowDoesNotUseTelegramBackingForCatalog() {
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::ExplicitAccount,
				.initiating = AccountBackend::Core,
				.backing = AccountBackend::Telegram,
			}),
			LanguageCloudRequestPolicy::Skip);
	}

	void unspecifiedIntroDoesNotRequestCloudEvenWithTelegramBacking() {
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::ExplicitAccount,
				.initiating = AccountBackend::Unspecified,
				.backing = AccountBackend::Telegram,
			}),
			LanguageCloudRequestPolicy::Skip);
	}

	void telegramCatalogRequestSurvivesCorePresence() {
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::ExplicitAccount,
				.initiating = AccountBackend::Telegram,
				.backing = AccountBackend::Telegram,
			}),
			LanguageCloudRequestPolicy::Allow);
	}

	void systemLangPackWorkUsesTelegramBackingNotFocusedCore() {
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::System,
				.initiating = AccountBackend::Unspecified,
				.backing = AccountBackend::Telegram,
			}),
			LanguageCloudRequestPolicy::Allow);
	}

	void missingOrCoreBackingNeverSendsCloud() {
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::System,
				.backing = AccountBackend::Unspecified,
			}),
			LanguageCloudRequestPolicy::Skip);
		QCOMPARE(
			PlanLanguageCloudRequest({
				.origin = LanguageRequestOrigin::ExplicitAccount,
				.initiating = AccountBackend::Telegram,
				.backing = AccountBackend::Core,
			}),
			LanguageCloudRequestPolicy::Skip);
	}

	void coreFocusKeepsExistingTelegramBacking() {
		QCOMPARE(
			PlanLanguageBacking({
				.active = AccountBackend::Core,
				.existingBackingIsLiveTelegram = true,
				.telegramAccountAvailable = true,
			}),
			LanguageBackingPick::KeepExistingTelegram);
	}

	void telegramWindowPrefersItsOwnBacking() {
		QCOMPARE(
			PlanLanguageBacking({
				.active = AccountBackend::Telegram,
				.existingBackingIsLiveTelegram = true,
				.telegramAccountAvailable = true,
			}),
			LanguageBackingPick::ActiveTelegram);
	}

	void noTelegramAccountLeavesLanguageCloudUnbound() {
		QCOMPARE(
			PlanLanguageBacking({
				.active = AccountBackend::Core,
				.existingBackingIsLiveTelegram = false,
				.telegramAccountAvailable = false,
			}),
			LanguageBackingPick::None);
		QCOMPARE(
			PlanLanguageBacking({
				.active = AccountBackend::Unspecified,
				.existingBackingIsLiveTelegram = false,
				.telegramAccountAvailable = true,
			}),
			LanguageBackingPick::FirstTelegram);
	}

	void coreFocusPicksRemainingTelegramBacking() {
		QCOMPARE(
			PlanLanguageBacking({
				.active = AccountBackend::Core,
				.existingBackingIsLiveTelegram = false,
				.telegramAccountAvailable = true,
			}),
			LanguageBackingPick::FirstTelegram);
	}

	void customImportCopyFollowsInitiatingBackend() {
		QCOMPARE(
			PlanLanguageCustomConfirm(AccountBackend::Core),
			LanguageCustomConfirmCopy::CoreRestart);
		QCOMPARE(
			PlanLanguageCustomConfirm(AccountBackend::Unspecified),
			LanguageCustomConfirmCopy::CoreRestart);
		QCOMPARE(
			PlanLanguageCustomConfirm(AccountBackend::Telegram),
			LanguageCustomConfirmCopy::TelegramBilingual);

		const auto restart = QString(
			"The app will restart to change the language.");
		QCOMPARE(
			CombineLanguageRestartCopy(
				LanguageCustomConfirmCopy::CoreRestart,
				restart,
				restart),
			restart);
		QCOMPARE(
			CombineLanguageRestartCopy(
				LanguageCustomConfirmCopy::CoreRestart,
				QString("current"),
				QString("target")),
			QString("current\n\ntarget"));
		QCOMPARE(
			CombineLanguageRestartCopy(
				LanguageCustomConfirmCopy::TelegramBilingual,
				QString("current"),
				QString("target")),
			QString("current\n\ntarget"));
		QCOMPARE(
			CombineLanguageRestartCopy(
				LanguageCustomConfirmCopy::TelegramBilingual,
				restart,
				restart),
			restart + QString("\n\n") + restart);
	}

};

QTEST_GUILESS_MAIN(LangBackendTests)
#include "lang_backend_tests.moc"
