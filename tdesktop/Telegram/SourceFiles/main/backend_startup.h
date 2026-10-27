/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "main/account_backend.h"

namespace Main {

enum class AccountStartupSessionAction : quint8 {
	None = 0,
	HoldPending = 1,
	PublishStoredTelegram = 2,
	SkipDonorSelfForCore = 3,
};

struct AccountStartupSessionDecision {
	AccountStartupSessionAction action = AccountStartupSessionAction::None;
	bool keepPendingSelf = false;
	bool keepPendingSettings = false;
	bool keepPersistedAuthBytes = true;
};

enum class AccountBackendChoiceAction : quint8 {
	Rejected = 0,
	BindCurrent = 1,
	RouteToExistingCore = 2,
};

struct AccountBackendChoiceRequest {
	AccountBackend current = AccountBackend::Unspecified;
	AccountBackend requested = AccountBackend::Unspecified;
	bool mtpAlreadyExists = false;
	bool sessionExists = false;
	bool mtpAuthorizationKeysPresent = false;
	bool existingCoreContainerPresent = false;
};

struct AccountBackendChoiceDecision {
	AccountBackendChoiceAction action = AccountBackendChoiceAction::Rejected;
	AccountBackendBindResult bind = AccountBackendBindResult::RejectedInvalid;
};

enum class CoreSessionStartResult : quint8 {
	Allowed = 0,
	RejectedNotCore = 1,
	RejectedOtherBridge = 2,
	RejectedAlreadyStarted = 3,
};

struct CoreSessionStartRequest {
	AccountBackend backend = AccountBackend::Unspecified;
	bool sessionExists = false;
	bool otherAccountOwnsCoreBridge = false;
};

[[nodiscard]] inline bool AccountBackendAllowsKeyDestruction(
		AccountBackend backend) {
	return backend == AccountBackend::Telegram;
}

[[nodiscard]] inline AccountStartupSessionDecision ResolveAccountStartupSession(
		AccountBackend backend,
		bool hasStoredSessionUserId) {
	AccountStartupSessionDecision out;
	out.keepPersistedAuthBytes = true;
	if (!hasStoredSessionUserId) {
		return out;
	} else if (backend == AccountBackend::Unspecified) {
		out.action = AccountStartupSessionAction::HoldPending;
		out.keepPendingSelf = true;
		out.keepPendingSettings = true;
		return out;
	} else if (backend == AccountBackend::Telegram) {
		out.action = AccountStartupSessionAction::PublishStoredTelegram;
		return out;
	} else if (backend == AccountBackend::Core) {
		out.action = AccountStartupSessionAction::SkipDonorSelfForCore;
		return out;
	}
	out.action = AccountStartupSessionAction::HoldPending;
	out.keepPendingSelf = true;
	out.keepPendingSettings = true;
	return out;
}

[[nodiscard]] inline AccountBackendChoiceDecision EvaluateAccountBackendChoice(
		const AccountBackendChoiceRequest &request) {
	AccountBackendChoiceDecision out;
	if (request.requested == AccountBackend::Core
		&& request.existingCoreContainerPresent) {
		out.action = AccountBackendChoiceAction::RouteToExistingCore;
		out.bind = AccountBackendBindResult::Unchanged;
		return out;
	}
	out.bind = EvaluateAccountBackendRebuild({
		request.current,
		request.requested,
		request.mtpAlreadyExists,
		request.sessionExists,
		request.mtpAuthorizationKeysPresent,
	});
	if (AccountBackendBindActivates(out.bind)) {
		out.action = AccountBackendChoiceAction::BindCurrent;
	}
	return out;
}

[[nodiscard]] inline bool AccountBackendChoiceActivates(
		AccountBackendChoiceAction action,
		AccountBackendBindResult durableBind) {
	if (action == AccountBackendChoiceAction::RouteToExistingCore) {
		return true;
	} else if (action != AccountBackendChoiceAction::BindCurrent) {
		return false;
	}
	return AccountBackendBindActivates(durableBind);
}

[[nodiscard]] inline CoreSessionStartResult EvaluateCoreSessionStart(
		const CoreSessionStartRequest &request) {
	if (request.backend != AccountBackend::Core) {
		return CoreSessionStartResult::RejectedNotCore;
	} else if (request.sessionExists) {
		return CoreSessionStartResult::RejectedAlreadyStarted;
	} else if (request.otherAccountOwnsCoreBridge) {
		return CoreSessionStartResult::RejectedOtherBridge;
	}
	return CoreSessionStartResult::Allowed;
}

} // namespace Main
