/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend.h"

namespace Main {

// Donor uniqueId is stored-self userId.bare with optional TestDC bit 56
// (0x0100'0000'0000'0000). Valid UserId occupies PeerId::kChatTypeMask
// (data/data_peer_id.h, bits 0-47 = 0xFFFFFFFFFFFF). Telegram production
// and test uniqueIds keep that exact donor formula.
//
// Core and held-Unspecified stored-self ids use disjoint high-bit prefixes
// outside that user mask and outside donor test bit 56. Classification is
// the immutable AccountBackend tag, never the fake Core userId 1_000_000.
//
// ComposeSessionUniqueId is the general composer: storedSelfBare 0 stays 0
// for every backend, including Core. Production Account::willHaveSessionUniqueId
// and Session::uniqueId use PredictSessionUniqueId. For Core that helper
// substitutes kCoreSessionSelfBare (StartSession MakeUser self) so a
// Core-tagged container has one stable non-zero id even before Core UI,
// including missing stored self and leftover donor self that is not 1_000_000.
inline constexpr quint64 kSessionUniqueIdUserMask = 0xFFFF'FFFFFFFFULL;
inline constexpr quint64 kSessionUniqueIdDonorTestBit = 0x0100'0000'0000'0000ULL;
inline constexpr quint64 kSessionUniqueIdCorePrefix = 0x0200'0000'0000'0000ULL;
inline constexpr quint64 kSessionUniqueIdUnspecifiedPrefix = 0x0400'0000'0000'0000ULL;
inline constexpr quint64 kCoreSessionSelfBare = 1'000'000;

static_assert((kSessionUniqueIdCorePrefix & kSessionUniqueIdUserMask) == 0);
static_assert(
	(kSessionUniqueIdUnspecifiedPrefix & kSessionUniqueIdUserMask) == 0);
static_assert((kSessionUniqueIdCorePrefix & kSessionUniqueIdDonorTestBit) == 0);
static_assert(
	(kSessionUniqueIdUnspecifiedPrefix & kSessionUniqueIdDonorTestBit) == 0);
static_assert(
	(kSessionUniqueIdCorePrefix & kSessionUniqueIdUnspecifiedPrefix) == 0);
static_assert(kSessionUniqueIdCorePrefix > kSessionUniqueIdDonorTestBit);
static_assert(
	kSessionUniqueIdUnspecifiedPrefix > kSessionUniqueIdDonorTestBit);
static_assert(kCoreSessionSelfBare == 1'000'000);
static_assert(kCoreSessionSelfBare != 0);
static_assert(
	(kCoreSessionSelfBare & kSessionUniqueIdUserMask)
		== kCoreSessionSelfBare);
static_assert(
	(kCoreSessionSelfBare & kSessionUniqueIdDonorTestBit) == 0);
static_assert((kCoreSessionSelfBare & kSessionUniqueIdCorePrefix) == 0);
static_assert(
	(kCoreSessionSelfBare & kSessionUniqueIdUnspecifiedPrefix) == 0);

struct SessionUniqueIdInputs {
	AccountBackend backend = AccountBackend::Unspecified;
	quint64 storedSelfBare = 0;
	bool testMode = false;
};

[[nodiscard]] inline quint64 SessionUniqueIdBackendPrefix(
		AccountBackend backend) {
	if (backend == AccountBackend::Core) {
		return kSessionUniqueIdCorePrefix;
	} else if (backend == AccountBackend::Telegram) {
		return 0;
	}
	// Unspecified and invalid tags stay out of the Telegram namespace.
	return kSessionUniqueIdUnspecifiedPrefix;
}

// WHY: Account::willHaveSessionUniqueId (predicted) and Session::uniqueId
// (runtime) must share PredictSessionUniqueId so mixed Core/Telegram/Unspecified
// sessions cannot collide on stored-self 1_000_000, and so a Core container
// with no stored self is not predicted as 0.
[[nodiscard]] inline quint64 ComposeSessionUniqueId(
		const SessionUniqueIdInputs &inputs) {
	if (!inputs.storedSelfBare) {
		return 0;
	}
	return inputs.storedSelfBare
		| (inputs.testMode ? kSessionUniqueIdDonorTestBit : 0ULL)
		| SessionUniqueIdBackendPrefix(inputs.backend);
}

[[nodiscard]] inline quint64 PredictedSessionUniqueIdStoredSelf(
		AccountBackend backend,
		quint64 storedSelfBare) {
	return (backend == AccountBackend::Core)
		? kCoreSessionSelfBare
		: storedSelfBare;
}

[[nodiscard]] inline quint64 PredictSessionUniqueId(
		const SessionUniqueIdInputs &inputs) {
	return ComposeSessionUniqueId({
		.backend = inputs.backend,
		.storedSelfBare = PredictedSessionUniqueIdStoredSelf(
			inputs.backend,
			inputs.storedSelfBare),
		.testMode = inputs.testMode,
	});
}

} // namespace Main
