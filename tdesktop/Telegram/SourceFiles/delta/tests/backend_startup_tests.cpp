#include "../../main/backend_startup.h"

#include <QtTest/QtTest>

using Main::AccountBackend;
using Main::AccountBackendBindResult;
using Main::AccountBackendChoiceAction;
using Main::AccountStartupSessionAction;
using Main::CoreSessionStartResult;

class BackendStartupTests final : public QObject {
	Q_OBJECT

private slots:
	void onlyChosenTelegramMayDestroyAuthorizationKeys() {
		QVERIFY(Main::AccountBackendAllowsKeyDestruction(
			AccountBackend::Telegram));
		QVERIFY(!Main::AccountBackendAllowsKeyDestruction(
			AccountBackend::Core));
		QVERIFY(!Main::AccountBackendAllowsKeyDestruction(
			AccountBackend::Unspecified));
		QVERIFY(!Main::AccountBackendAllowsKeyDestruction(
			static_cast<AccountBackend>(255)));
	}

	void unspecifiedSavedSelfNotPublished() {
		const auto decision = Main::ResolveAccountStartupSession(
			AccountBackend::Unspecified,
			true);
		QCOMPARE(
			decision.action,
			AccountStartupSessionAction::HoldPending);
		QVERIFY(decision.keepPendingSelf);
		QVERIFY(decision.keepPendingSettings);
		QVERIFY(decision.keepPersistedAuthBytes);
	}

	void telegramStoredSessionPreserved() {
		const auto withSelf = Main::ResolveAccountStartupSession(
			AccountBackend::Telegram,
			true);
		QCOMPARE(
			withSelf.action,
			AccountStartupSessionAction::PublishStoredTelegram);
		QVERIFY(!withSelf.keepPendingSelf);
		QVERIFY(!withSelf.keepPendingSettings);
		QVERIFY(withSelf.keepPersistedAuthBytes);

		const auto withoutSelf = Main::ResolveAccountStartupSession(
			AccountBackend::Telegram,
			false);
		QCOMPARE(withoutSelf.action, AccountStartupSessionAction::None);
		QVERIFY(withoutSelf.keepPersistedAuthBytes);
	}

	void coreUsesRpcSessionNotDonorSelf() {
		const auto decision = Main::ResolveAccountStartupSession(
			AccountBackend::Core,
			true);
		QCOMPARE(
			decision.action,
			AccountStartupSessionAction::SkipDonorSelfForCore);
		QVERIFY(!decision.keepPendingSelf);
		QVERIFY(!decision.keepPendingSettings);
		QVERIFY(decision.keepPersistedAuthBytes);
	}

	void unspecifiedWithoutSelfDoesNotPublish() {
		const auto decision = Main::ResolveAccountStartupSession(
			AccountBackend::Unspecified,
			false);
		QCOMPARE(decision.action, AccountStartupSessionAction::None);
		QVERIFY(!decision.keepPendingSelf);
		QVERIFY(decision.keepPersistedAuthBytes);
	}

	void bindLiveWithoutSessionRebuildAllowedWithSessionDenied() {
		QCOMPARE(
			Main::EvaluateAccountBackendBind({
				AccountBackend::Unspecified,
				AccountBackend::Telegram,
				true,
				false,
			}),
			AccountBackendBindResult::RejectedLiveMtp);
		QCOMPARE(
			Main::EvaluateAccountBackendRebuild({
				AccountBackend::Unspecified,
				AccountBackend::Telegram,
				true,
				false,
				false,
			}),
			AccountBackendBindResult::Bound);
		QCOMPARE(
			Main::EvaluateAccountBackendRebuild({
				AccountBackend::Unspecified,
				AccountBackend::Core,
				true,
				false,
				false,
			}),
			AccountBackendBindResult::Bound);
		QCOMPARE(
			Main::EvaluateAccountBackendRebuild({
				AccountBackend::Unspecified,
				AccountBackend::Telegram,
				true,
				true,
				false,
			}),
			AccountBackendBindResult::RejectedLiveSession);
	}

	void boundChangeDenied() {
		QCOMPARE(
			Main::EvaluateAccountBackendRebuild({
				AccountBackend::Core,
				AccountBackend::Telegram,
				true,
				false,
				false,
			}),
			AccountBackendBindResult::RejectedChange);
		QCOMPARE(
			Main::EvaluateAccountBackendBind({
				AccountBackend::Telegram,
				AccountBackend::Core,
				false,
				false,
			}),
			AccountBackendBindResult::RejectedChange);
	}

	void coreWithKeysDeniedWithoutDestroy() {
		Main::AccountBackendBindState state;
		state.mtpAuthorizationKeysPresent = true;
		QCOMPARE(
			Main::TryBindAccountBackend(state, AccountBackend::Core),
			AccountBackendBindResult::RejectedCoreWithKeys);
		QCOMPARE(state.backend, AccountBackend::Unspecified);
		QCOMPARE(
			Main::EvaluateAccountBackendRebuild({
				AccountBackend::Unspecified,
				AccountBackend::Core,
				true,
				false,
				true,
			}),
			AccountBackendBindResult::RejectedCoreWithKeys);
		QVERIFY(!Main::AccountBackendBindActivates(
			AccountBackendBindResult::RejectedCoreWithKeys));
	}

	void failedPersistNoActivation() {
		const auto durable = Main::AccountBackendBindPersistOutcome(
			AccountBackendBindResult::Bound,
			AccountBackend::Core,
			false);
		QCOMPARE(durable, AccountBackendBindResult::RejectedPersistFailed);
		QVERIFY(!Main::AccountBackendChoiceActivates(
			AccountBackendChoiceAction::BindCurrent,
			durable));
		QVERIFY(!Main::AccountBackendChoiceActivates(
			AccountBackendChoiceAction::Rejected,
			AccountBackendBindResult::Bound));
		QVERIFY(Main::AccountBackendChoiceActivates(
			AccountBackendChoiceAction::RouteToExistingCore,
			AccountBackendBindResult::Unchanged));
	}

	void singletonChoicePolicy() {
		const auto existingCore = Main::EvaluateAccountBackendChoice({
			AccountBackend::Unspecified,
			AccountBackend::Core,
			true,
			false,
			true,
			true,
		});
		QCOMPARE(
			existingCore.action,
			AccountBackendChoiceAction::RouteToExistingCore);
		QCOMPARE(existingCore.bind, AccountBackendBindResult::Unchanged);

		const auto firstCore = Main::EvaluateAccountBackendChoice({
			AccountBackend::Unspecified,
			AccountBackend::Core,
			true,
			false,
			false,
			false,
		});
		QCOMPARE(firstCore.action, AccountBackendChoiceAction::BindCurrent);
		QCOMPARE(firstCore.bind, AccountBackendBindResult::Bound);

		const auto telegramBesideCore = Main::EvaluateAccountBackendChoice({
			AccountBackend::Unspecified,
			AccountBackend::Telegram,
			true,
			false,
			false,
			true,
		});
		QCOMPARE(
			telegramBesideCore.action,
			AccountBackendChoiceAction::BindCurrent);
		QCOMPARE(telegramBesideCore.bind, AccountBackendBindResult::Bound);

		const auto coreWithKeysNoContainer = Main::EvaluateAccountBackendChoice({
			AccountBackend::Unspecified,
			AccountBackend::Core,
			true,
			false,
			true,
			false,
		});
		QCOMPARE(
			coreWithKeysNoContainer.action,
			AccountBackendChoiceAction::Rejected);
		QCOMPARE(
			coreWithKeysNoContainer.bind,
			AccountBackendBindResult::RejectedCoreWithKeys);
	}

	void coreSessionStartPolicy() {
		QCOMPARE(
			Main::EvaluateCoreSessionStart({
				AccountBackend::Core,
				false,
				false,
			}),
			CoreSessionStartResult::Allowed);
		QCOMPARE(
			Main::EvaluateCoreSessionStart({
				AccountBackend::Telegram,
				false,
				false,
			}),
			CoreSessionStartResult::RejectedNotCore);
		QCOMPARE(
			Main::EvaluateCoreSessionStart({
				AccountBackend::Unspecified,
				false,
				false,
			}),
			CoreSessionStartResult::RejectedNotCore);
		QCOMPARE(
			Main::EvaluateCoreSessionStart({
				AccountBackend::Core,
				true,
				false,
			}),
			CoreSessionStartResult::RejectedAlreadyStarted);
		QCOMPARE(
			Main::EvaluateCoreSessionStart({
				AccountBackend::Core,
				false,
				true,
			}),
			CoreSessionStartResult::RejectedOtherBridge);
	}

};

QTEST_GUILESS_MAIN(BackendStartupTests)
#include "backend_startup_tests.moc"
