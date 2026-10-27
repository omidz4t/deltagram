/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend.h"

namespace Main {

struct SessionPremiumInputs {
	AccountBackend backend = AccountBackend::Unspecified;
	bool userIsPremium = false;
	bool premiumPurchaseAllowed = false;
};

struct SessionPremiumPurchaseConfig {
	AccountBackend backend = AccountBackend::Unspecified;
	bool macStoreBuild = false;
	bool premiumPurchaseBlocked = true;
};

// WHY: Paid Telegram premium follows the owning AccountBackend tag, never
// GlobalBridge/Active. Core and Unspecified stay fail-closed.
[[nodiscard]] inline bool SessionPremiumUsesTelegramBackend(
		AccountBackend backend) {
	return backend == AccountBackend::Telegram;
}

[[nodiscard]] inline bool SessionPremiumActive(
		const SessionPremiumInputs &inputs) {
	return SessionPremiumUsesTelegramBackend(inputs.backend)
		&& inputs.userIsPremium;
}

[[nodiscard]] inline bool SessionPremiumCanBuy(
		const SessionPremiumInputs &inputs) {
	return SessionPremiumUsesTelegramBackend(inputs.backend)
		&& inputs.premiumPurchaseAllowed;
}

[[nodiscard]] inline bool SessionPremiumPossible(
		const SessionPremiumInputs &inputs) {
	return SessionPremiumActive(inputs) || SessionPremiumCanBuy(inputs);
}

[[nodiscard]] inline bool SessionPremiumPurchaseAllowedFromAppConfig(
		const SessionPremiumPurchaseConfig &config) {
	if (!SessionPremiumUsesTelegramBackend(config.backend)
		|| config.macStoreBuild) {
		return false;
	}
	return !config.premiumPurchaseBlocked;
}

} // namespace Main
