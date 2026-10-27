#include "main/backend_session_identity.h"

#include <QtTest/QtTest>

using Main::AccountBackend;
using Main::ComposeSessionUniqueId;
using Main::kCoreSessionSelfBare;
using Main::kSessionUniqueIdCorePrefix;
using Main::kSessionUniqueIdDonorTestBit;
using Main::kSessionUniqueIdUnspecifiedPrefix;
using Main::kSessionUniqueIdUserMask;
using Main::PredictedSessionUniqueIdStoredSelf;
using Main::PredictSessionUniqueId;
using Main::SessionUniqueIdBackendPrefix;
using Main::SessionUniqueIdInputs;

namespace {

constexpr auto kCoreFakeSelf = kCoreSessionSelfBare;
constexpr auto kHighValidUser = kSessionUniqueIdUserMask;
constexpr auto kLeftoverForeignSelf = quint64(42);
constexpr auto kTelegramHighBits = ~(kSessionUniqueIdUserMask
	| kSessionUniqueIdDonorTestBit);

[[nodiscard]] quint64 Id(
		AccountBackend backend,
		quint64 storedSelfBare,
		bool testMode = false) {
	return ComposeSessionUniqueId({
		.backend = backend,
		.storedSelfBare = storedSelfBare,
		.testMode = testMode,
	});
}

[[nodiscard]] quint64 Predicted(
		AccountBackend backend,
		quint64 storedSelfBare,
		bool testMode = false) {
	return PredictSessionUniqueId({
		.backend = backend,
		.storedSelfBare = storedSelfBare,
		.testMode = testMode,
	});
}

} // namespace

class BackendSessionIdentityTests final : public QObject {
	Q_OBJECT

private slots:
	void prefixesSitOutsideDonorUserMaskAndTestBit() {
		QCOMPARE(kSessionUniqueIdUserMask, quint64(0xFFFFFFFFFFFFULL));
		QCOMPARE(kSessionUniqueIdDonorTestBit, quint64(0x0100000000000000ULL));
		QVERIFY((kSessionUniqueIdCorePrefix & kSessionUniqueIdUserMask) == 0);
		QVERIFY(
			(kSessionUniqueIdUnspecifiedPrefix & kSessionUniqueIdUserMask)
				== 0);
		QVERIFY(
			(kSessionUniqueIdCorePrefix & kSessionUniqueIdDonorTestBit) == 0);
		QVERIFY(
			(kSessionUniqueIdUnspecifiedPrefix & kSessionUniqueIdDonorTestBit)
				== 0);
		QVERIFY(
			(kSessionUniqueIdCorePrefix & kSessionUniqueIdUnspecifiedPrefix)
				== 0);
		QVERIFY(kSessionUniqueIdCorePrefix > kSessionUniqueIdDonorTestBit);
		QVERIFY(
			kSessionUniqueIdUnspecifiedPrefix > kSessionUniqueIdDonorTestBit);
		QCOMPARE(
			SessionUniqueIdBackendPrefix(AccountBackend::Telegram),
			quint64(0));
		QCOMPARE(
			SessionUniqueIdBackendPrefix(AccountBackend::Core),
			kSessionUniqueIdCorePrefix);
		QCOMPARE(
			SessionUniqueIdBackendPrefix(AccountBackend::Unspecified),
			kSessionUniqueIdUnspecifiedPrefix);
		QCOMPARE(kCoreSessionSelfBare, quint64(1'000'000));
	}

	void absentStoredSelfIsZeroForEveryBackendAndTestMode() {
		for (const auto backend : {
			AccountBackend::Telegram,
			AccountBackend::Core,
			AccountBackend::Unspecified,
		}) {
			QCOMPARE(Id(backend, 0, false), quint64(0));
			QCOMPARE(Id(backend, 0, true), quint64(0));
		}
	}

	void telegramProductionAndTestMatchExactDonorOutputs() {
		QCOMPARE(Id(AccountBackend::Telegram, 1), quint64(1));
		QCOMPARE(Id(AccountBackend::Telegram, kCoreFakeSelf), kCoreFakeSelf);
		QCOMPARE(
			Id(AccountBackend::Telegram, kCoreFakeSelf, true),
			kCoreFakeSelf | kSessionUniqueIdDonorTestBit);
		QCOMPARE(
			Id(AccountBackend::Telegram, kHighValidUser),
			kHighValidUser);
		QCOMPARE(
			Id(AccountBackend::Telegram, kHighValidUser, true),
			kHighValidUser | kSessionUniqueIdDonorTestBit);
		QVERIFY(
			(Id(AccountBackend::Telegram, kCoreFakeSelf) & kTelegramHighBits)
				== 0);
		QVERIFY(
			(Id(AccountBackend::Telegram, kCoreFakeSelf, true)
				& kTelegramHighBits)
				== 0);
		QVERIFY(
			(Id(AccountBackend::Telegram, kHighValidUser, true)
				& kTelegramHighBits)
				== 0);
	}

	void coreFakeSelfDoesNotCollideWithTelegramOrUnspecified() {
		const auto telegram = Id(AccountBackend::Telegram, kCoreFakeSelf);
		const auto core = Id(AccountBackend::Core, kCoreFakeSelf);
		const auto unspecified = Id(
			AccountBackend::Unspecified,
			kCoreFakeSelf);
		QCOMPARE(telegram, kCoreFakeSelf);
		QCOMPARE(core, kCoreFakeSelf | kSessionUniqueIdCorePrefix);
		QCOMPARE(
			unspecified,
			kCoreFakeSelf | kSessionUniqueIdUnspecifiedPrefix);
		QVERIFY(core != telegram);
		QVERIFY(unspecified != telegram);
		QVERIFY(core != unspecified);
		QVERIFY(
			(core & kSessionUniqueIdCorePrefix)
				== kSessionUniqueIdCorePrefix);
		QVERIFY((core & kSessionUniqueIdUnspecifiedPrefix) == 0);
		QVERIFY(
			(unspecified & kSessionUniqueIdUnspecifiedPrefix)
				== kSessionUniqueIdUnspecifiedPrefix);
		QVERIFY((unspecified & kSessionUniqueIdCorePrefix) == 0);
		QVERIFY((core & kTelegramHighBits) == kSessionUniqueIdCorePrefix);
		QVERIFY(
			(unspecified & kTelegramHighBits)
				== kSessionUniqueIdUnspecifiedPrefix);
	}

	void testModeKeepsDonorBitInsideEachBackendNamespace() {
		const auto telegram = Id(
			AccountBackend::Telegram,
			kCoreFakeSelf,
			true);
		const auto core = Id(AccountBackend::Core, kCoreFakeSelf, true);
		const auto unspecified = Id(
			AccountBackend::Unspecified,
			kCoreFakeSelf,
			true);
		QCOMPARE(telegram, kCoreFakeSelf | kSessionUniqueIdDonorTestBit);
		QCOMPARE(
			core,
			kCoreFakeSelf
				| kSessionUniqueIdDonorTestBit
				| kSessionUniqueIdCorePrefix);
		QCOMPARE(
			unspecified,
			kCoreFakeSelf
				| kSessionUniqueIdDonorTestBit
				| kSessionUniqueIdUnspecifiedPrefix);
		QVERIFY(core != telegram);
		QVERIFY(unspecified != telegram);
		QVERIFY(core != unspecified);
		QVERIFY(core != Id(AccountBackend::Core, kCoreFakeSelf));
		QVERIFY(
			unspecified != Id(AccountBackend::Unspecified, kCoreFakeSelf));
	}

	void highValidUserIdsStayInsideTheirNamespace() {
		const auto core = Id(AccountBackend::Core, kHighValidUser);
		const auto unspecified = Id(
			AccountBackend::Unspecified,
			kHighValidUser,
			true);
		QCOMPARE(
			core,
			kHighValidUser | kSessionUniqueIdCorePrefix);
		QCOMPARE(
			unspecified,
			kHighValidUser
				| kSessionUniqueIdDonorTestBit
				| kSessionUniqueIdUnspecifiedPrefix);
		QVERIFY(
			Id(AccountBackend::Telegram, kHighValidUser)
				!= core);
		QVERIFY(
			(core & kSessionUniqueIdUserMask) == kHighValidUser);
		QVERIFY(
			(unspecified & kSessionUniqueIdUserMask) == kHighValidUser);
	}

	void neverClassifyFromFakeUserIdAlone() {
		QCOMPARE(
			Id(AccountBackend::Telegram, kCoreFakeSelf),
			kCoreFakeSelf);
		QVERIFY(
			Id(AccountBackend::Core, kCoreFakeSelf)
				!= kCoreFakeSelf);
		QVERIFY(
			Id(AccountBackend::Unspecified, kCoreFakeSelf)
				!= kCoreFakeSelf);
		QCOMPARE(
			SessionUniqueIdBackendPrefix(AccountBackend::Telegram),
			quint64(0));
	}

	void predictedAndRuntimeShareTheSameHelperCall() {
		const SessionUniqueIdInputs cases[] = {
			{
				.backend = AccountBackend::Telegram,
				.storedSelfBare = kCoreFakeSelf,
			},
			{
				.backend = AccountBackend::Telegram,
				.storedSelfBare = kCoreFakeSelf,
				.testMode = true,
			},
			{
				.backend = AccountBackend::Core,
				.storedSelfBare = kCoreFakeSelf,
			},
			{
				.backend = AccountBackend::Unspecified,
				.storedSelfBare = kCoreFakeSelf,
				.testMode = true,
			},
			{
				.backend = AccountBackend::Core,
				.storedSelfBare = 0,
				.testMode = true,
			},
			{
				.backend = AccountBackend::Core,
				.storedSelfBare = kLeftoverForeignSelf,
			},
			{
				.backend = AccountBackend::Telegram,
				.storedSelfBare = kHighValidUser,
			},
			{
				.backend = AccountBackend::Telegram,
				.storedSelfBare = 0,
			},
			{
				.backend = AccountBackend::Unspecified,
				.storedSelfBare = 0,
			},
		};
		for (const auto &entry : cases) {
			const auto predicted = PredictSessionUniqueId(entry);
			const auto runtime = PredictSessionUniqueId({
				.backend = entry.backend,
				.storedSelfBare = (entry.backend == AccountBackend::Core)
					? kCoreSessionSelfBare
					: entry.storedSelfBare,
				.testMode = entry.testMode,
			});
			QCOMPARE(predicted, runtime);
			QCOMPARE(predicted, ComposeSessionUniqueId({
				.backend = entry.backend,
				.storedSelfBare = PredictedSessionUniqueIdStoredSelf(
					entry.backend,
					entry.storedSelfBare),
				.testMode = entry.testMode,
			}));
		}
	}

	void composeZeroStaysZeroIncludingCore() {
		QCOMPARE(Id(AccountBackend::Core, 0), quint64(0));
		QCOMPARE(Id(AccountBackend::Core, 0, true), quint64(0));
	}

	void coreContainerPredictionMatchesRuntimeSelfAcrossStoredIds() {
		const auto runtime = Id(AccountBackend::Core, kCoreSessionSelfBare);
		QCOMPARE(kCoreSessionSelfBare, quint64(1'000'000));
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(AccountBackend::Core, 0),
			kCoreSessionSelfBare);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Core,
				kCoreSessionSelfBare),
			kCoreSessionSelfBare);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Core,
				kLeftoverForeignSelf),
			kCoreSessionSelfBare);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Core,
				kHighValidUser),
			kCoreSessionSelfBare);
		QCOMPARE(Predicted(AccountBackend::Core, 0), runtime);
		QCOMPARE(
			Predicted(AccountBackend::Core, kCoreSessionSelfBare),
			runtime);
		QCOMPARE(
			Predicted(AccountBackend::Core, kLeftoverForeignSelf),
			runtime);
		QCOMPARE(Predicted(AccountBackend::Core, kHighValidUser), runtime);
		QVERIFY(runtime != 0);
		QCOMPARE(runtime, kCoreSessionSelfBare | kSessionUniqueIdCorePrefix);
		QVERIFY(
			Predicted(AccountBackend::Core, kLeftoverForeignSelf)
				!= Id(AccountBackend::Core, kLeftoverForeignSelf));
		QCOMPARE(
			Predicted(AccountBackend::Core, 0, true),
			Id(AccountBackend::Core, kCoreSessionSelfBare, true));
		QCOMPARE(
			Predicted(AccountBackend::Core, kLeftoverForeignSelf, true),
			Predicted(AccountBackend::Core, 0, true));
		QVERIFY(
			Predicted(AccountBackend::Core, 0, true)
				!= Predicted(AccountBackend::Core, 0));
	}

	void duplicateCoreContainerPredictionsAreEqual() {
		QCOMPARE(
			Predicted(AccountBackend::Core, 0),
			Predicted(AccountBackend::Core, kCoreSessionSelfBare));
		QCOMPARE(
			Predicted(AccountBackend::Core, 0),
			Predicted(AccountBackend::Core, kLeftoverForeignSelf));
		QCOMPARE(
			Predicted(AccountBackend::Core, kLeftoverForeignSelf),
			Predicted(AccountBackend::Core, kHighValidUser));
		QCOMPARE(
			Predicted(AccountBackend::Core, 0, true),
			Predicted(AccountBackend::Core, kLeftoverForeignSelf, true));
	}

	void telegramPredictionPreservesStoredUserTestBitsAndAbsentZero() {
		QCOMPARE(Predicted(AccountBackend::Telegram, 0), quint64(0));
		QCOMPARE(Predicted(AccountBackend::Telegram, 0, true), quint64(0));
		QCOMPARE(Predicted(AccountBackend::Telegram, 1), quint64(1));
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kCoreFakeSelf),
			kCoreFakeSelf);
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kCoreFakeSelf, true),
			kCoreFakeSelf | kSessionUniqueIdDonorTestBit);
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kHighValidUser),
			kHighValidUser);
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kHighValidUser, true),
			kHighValidUser | kSessionUniqueIdDonorTestBit);
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kCoreFakeSelf),
			Id(AccountBackend::Telegram, kCoreFakeSelf));
		QCOMPARE(
			Predicted(AccountBackend::Telegram, kHighValidUser, true),
			Id(AccountBackend::Telegram, kHighValidUser, true));
		QVERIFY(
			(Predicted(AccountBackend::Telegram, kCoreFakeSelf)
				& kTelegramHighBits)
				== 0);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Telegram,
				kLeftoverForeignSelf),
			kLeftoverForeignSelf);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(AccountBackend::Telegram, 0),
			quint64(0));
		QVERIFY(
			Predicted(AccountBackend::Telegram, kCoreFakeSelf)
				!= Predicted(AccountBackend::Core, kCoreFakeSelf));
		QVERIFY(
			Predicted(AccountBackend::Telegram, kCoreFakeSelf)
				!= Predicted(AccountBackend::Core, 0));
	}

	void unspecifiedHeldSelfStaysIsolatedAndAbsentZero() {
		QCOMPARE(Predicted(AccountBackend::Unspecified, 0), quint64(0));
		QCOMPARE(
			Predicted(AccountBackend::Unspecified, 0, true),
			quint64(0));
		QCOMPARE(
			Predicted(AccountBackend::Unspecified, kCoreFakeSelf),
			kCoreFakeSelf | kSessionUniqueIdUnspecifiedPrefix);
		QCOMPARE(
			Predicted(AccountBackend::Unspecified, kLeftoverForeignSelf),
			Id(AccountBackend::Unspecified, kLeftoverForeignSelf));
		QCOMPARE(
			Predicted(AccountBackend::Unspecified, kCoreFakeSelf, true),
			Id(AccountBackend::Unspecified, kCoreFakeSelf, true));
		QVERIFY(
			Predicted(AccountBackend::Unspecified, kCoreFakeSelf)
				!= Predicted(AccountBackend::Telegram, kCoreFakeSelf));
		QVERIFY(
			Predicted(AccountBackend::Unspecified, kCoreFakeSelf)
				!= Predicted(AccountBackend::Core, kCoreFakeSelf));
		QVERIFY(
			Predicted(AccountBackend::Unspecified, kLeftoverForeignSelf)
				!= Predicted(AccountBackend::Core, kLeftoverForeignSelf));
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Unspecified,
				kLeftoverForeignSelf),
			kLeftoverForeignSelf);
		QCOMPARE(
			PredictedSessionUniqueIdStoredSelf(
				AccountBackend::Unspecified,
				0),
			quint64(0));
	}

};

QTEST_GUILESS_MAIN(BackendSessionIdentityTests)
#include "backend_session_identity_tests.moc"
