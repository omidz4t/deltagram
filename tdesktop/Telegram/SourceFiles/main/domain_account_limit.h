/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend.h"

#include <vector>

namespace Main {

struct DomainAccountLimitSlot {
	AccountBackend backend = AccountBackend::Unspecified;
	bool sessionExists = false;
	bool premium = false;
	bool testMode = false;
};

// WHY: Core sessions must not contribute Telegram premium/test entitlement.
// Unspecified chooser accountStates also cannot mint a premium expansion.
[[nodiscard]] inline bool DomainAccountLimitCountsTelegramPremium(
		const DomainAccountLimitSlot &slot) {
	return slot.backend == AccountBackend::Telegram
		&& slot.sessionExists
		&& (slot.premium || slot.testMode);
}

// Donor: min(telegramPremiumOrTestCount + kMaxAccounts, kPremiumMaxAccounts).
// WHY: The storage reader rejects live count > kPremiumMaxAccounts, Domain::add
// Expects size < that same cap, and donor loops compare accounts().size() with
// maxAccounts() / kMaxAccounts. A Core occupancy bonus would yield 4 without
// Telegram premium and 7 before the min() in mixed premium, which disagrees
// with those loops. One Core container still leaves add room because 1 live
// account is below kMaxAccounts (3).
[[nodiscard]] inline int ResolveDomainMaxAccounts(
		const std::vector<DomainAccountLimitSlot> &accountStates,
		int freeLimit,
		int premiumLimit) {
	auto premiumCount = 0;
	for (const auto &slot : accountStates) {
		if (DomainAccountLimitCountsTelegramPremium(slot)) {
			++premiumCount;
		}
	}
	const auto value = premiumCount + freeLimit;
	return (value < premiumLimit) ? value : premiumLimit;
}

// WHY: Capacity reuse must not adopt or replace the Core container, and must
// not steal an authed Telegram session. Empty Unspecified chooser accountStates and
// logged-out Telegram accountStates matching the requested environment may be reused.
[[nodiscard]] inline bool DomainAddActivatedReuseCandidate(
		AccountBackend backend,
		bool sessionExists,
		bool environmentMatches) {
	return !sessionExists
		&& backend != AccountBackend::Core
		&& environmentMatches;
}

// A Core container owns a persistent profile catalog even before its UI session
// opens. Donor cleanup of abandoned login slots must retain that container.
[[nodiscard]] inline bool DomainAccountCanRemoveAsRedundant(
		AccountBackend backend,
		bool sessionExists,
		bool separateWindowExists) {
	return backend != AccountBackend::Core
		&& !sessionExists
		&& !separateWindowExists;
}

[[nodiscard]] inline bool DomainAccountCanClearPasscode(
		AccountBackend backend,
		bool sessionExists,
		int accountCount) {
	// Only donor Telegram logout proves that the last account became empty.
	// An unopened Core catalog or unresolved stored account may still own data.
	return backend == AccountBackend::Telegram
		&& !sessionExists
		&& accountCount == 1;
}

} // namespace Main
