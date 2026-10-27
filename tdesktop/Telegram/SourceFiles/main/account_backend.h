/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

namespace Main {

enum class AccountBackend : quint8 {
	Unspecified = 0,
	Core = 1,
	Telegram = 2,
};

inline constexpr quint32 kAccountBackendTagUnspecified = 0;
inline constexpr quint32 kAccountBackendTagCore = 1;
inline constexpr quint32 kAccountBackendTagTelegram = 2;

inline constexpr char kAccountBackendLegacyCoreMarker[] =
	"deltagram.legacy-core-marker.v1";

static_assert(quint32(AccountBackend::Unspecified)
	== kAccountBackendTagUnspecified);
static_assert(quint32(AccountBackend::Core) == kAccountBackendTagCore);
static_assert(quint32(AccountBackend::Telegram)
	== kAccountBackendTagTelegram);

enum class AccountBackendRecordKind : quint8 {
	FreshMissing = 0,
	LegacyExisting = 1,
	NewDomainSlot = 2,
};

enum class AccountBackendMigrationNeed : quint8 {
	None = 0,
	Chooser = 1,
};

enum class AccountBackendBindResult : quint8 {
	Bound = 0,
	Unchanged = 1,
	RejectedChange = 2,
	RejectedClear = 3,
	RejectedLiveMtp = 4,
	RejectedInvalid = 5,
	RejectedPersistFailed = 6,
	RejectedLiveSession = 7,
	RejectedCoreWithKeys = 8,
};

struct AccountBackendDecodedTag {
	AccountBackend backend = AccountBackend::Unspecified;
	bool valid = false;
};

struct AccountBackendMigrationEvidence {
	AccountBackendRecordKind record = AccountBackendRecordKind::FreshMissing;
	bool tagPresent = false;
	quint32 storedTag = 0;
	bool telegramAuthorizationKeysPresent = false;
	bool provenLegacyCoreMarker = false;
};

struct AccountBackendMigration {
	AccountBackend backend = AccountBackend::Unspecified;
	AccountBackendMigrationNeed need = AccountBackendMigrationNeed::Chooser;
	bool needsProvenLegacyCoreMarker = false;
};

struct AccountBackendBindRequest {
	AccountBackend current = AccountBackend::Unspecified;
	AccountBackend requested = AccountBackend::Unspecified;
	bool mtpAlreadyExists = false;
	bool mtpAuthorizationKeysPresent = false;
};

struct AccountBackendBindState {
	AccountBackend backend = AccountBackend::Unspecified;
	bool mtpAlreadyExists = false;
	bool mtpAuthorizationKeysPresent = false;
};

struct AccountBackendRebuildRequest {
	AccountBackend current = AccountBackend::Unspecified;
	AccountBackend requested = AccountBackend::Unspecified;
	bool mtpAlreadyExists = false;
	bool sessionExists = false;
	bool mtpAuthorizationKeysPresent = false;
};

[[nodiscard]] inline bool IsBoundAccountBackend(AccountBackend backend) {
	return backend == AccountBackend::Core
		|| backend == AccountBackend::Telegram;
}

[[nodiscard]] inline bool IsValidAccountBackend(AccountBackend backend) {
	return backend == AccountBackend::Unspecified
		|| IsBoundAccountBackend(backend);
}

[[nodiscard]] inline bool IsValidAccountBackendTag(quint32 tag) {
	return tag == kAccountBackendTagUnspecified
		|| tag == kAccountBackendTagCore
		|| tag == kAccountBackendTagTelegram;
}

[[nodiscard]] inline AccountBackendDecodedTag InspectAccountBackendTag(
		quint32 tag) {
	AccountBackendDecodedTag out;
	out.valid = IsValidAccountBackendTag(tag);
	out.backend = out.valid
		? AccountBackend(tag)
		: AccountBackend::Unspecified;
	return out;
}

[[nodiscard]] inline AccountBackend DecodeAccountBackendTag(quint32 tag) {
	return InspectAccountBackendTag(tag).backend;
}

[[nodiscard]] inline quint32 EncodeAccountBackendTag(
		AccountBackend backend) {
	return IsValidAccountBackend(backend)
		? quint32(backend)
		: kAccountBackendTagUnspecified;
}

[[nodiscard]] inline bool IsProvenLegacyCoreMarker(
		const QByteArray &stored) {
	return stored
		== QByteArray::fromRawData(
			kAccountBackendLegacyCoreMarker,
			int(sizeof(kAccountBackendLegacyCoreMarker) - 1));
}

[[nodiscard]] inline AccountBackendMigration ResolveStoredAccountBackend(
		const AccountBackendMigrationEvidence &evidence) {
	AccountBackendMigration result;
	if (evidence.record == AccountBackendRecordKind::NewDomainSlot) {
		// WHY: Domain::add must not inherit orphan sidecar or unread Telegram keys.
		result.need = AccountBackendMigrationNeed::Chooser;
		return result;
	}
	const auto keys = evidence.telegramAuthorizationKeysPresent;
	const auto coreMarker = evidence.provenLegacyCoreMarker;
	const auto legacyUntagged = !evidence.tagPresent
		&& evidence.record == AccountBackendRecordKind::LegacyExisting;

	if (evidence.tagPresent) {
		const auto inspected = InspectAccountBackendTag(evidence.storedTag);
		if (!inspected.valid) {
			result.need = AccountBackendMigrationNeed::Chooser;
			return result;
		} else if (inspected.backend == AccountBackend::Telegram) {
			result.backend = AccountBackend::Telegram;
			result.need = AccountBackendMigrationNeed::None;
			return result;
		} else if (inspected.backend == AccountBackend::Core) {
			if (keys) {
				// WHY: Core tag plus stored Telegram keys is unprovable; never flip those keys to Core.
				result.need = AccountBackendMigrationNeed::Chooser;
				return result;
			}
			result.backend = AccountBackend::Core;
			result.need = AccountBackendMigrationNeed::None;
			return result;
		}
	}

	if (keys) {
		result.backend = AccountBackend::Telegram;
		result.need = AccountBackendMigrationNeed::None;
		return result;
	}
	if (coreMarker) {
		result.backend = AccountBackend::Core;
		result.need = AccountBackendMigrationNeed::None;
		return result;
	}

	result.need = AccountBackendMigrationNeed::Chooser;
	result.needsProvenLegacyCoreMarker = legacyUntagged;
	return result;
}

[[nodiscard]] inline AccountBackendBindResult EvaluateAccountBackendBindBase(
		AccountBackend current,
		AccountBackend requested) {
	if (!IsValidAccountBackend(requested)
		|| !IsValidAccountBackend(current)) {
		return AccountBackendBindResult::RejectedInvalid;
	} else if (requested == AccountBackend::Unspecified) {
		return (current == AccountBackend::Unspecified)
			? AccountBackendBindResult::Unchanged
			: AccountBackendBindResult::RejectedClear;
	} else if (current == requested) {
		return AccountBackendBindResult::Unchanged;
	} else if (current != AccountBackend::Unspecified) {
		return AccountBackendBindResult::RejectedChange;
	}
	return AccountBackendBindResult::Bound;
}

[[nodiscard]] inline AccountBackendBindResult EvaluateAccountBackendBind(
		const AccountBackendBindRequest &request) {
	const auto result = EvaluateAccountBackendBindBase(
		request.current,
		request.requested);
	if (result != AccountBackendBindResult::Bound) {
		return result;
	} else if (request.requested == AccountBackend::Core
		&& request.mtpAuthorizationKeysPresent) {
		return AccountBackendBindResult::RejectedCoreWithKeys;
	} else if (request.mtpAlreadyExists) {
		return AccountBackendBindResult::RejectedLiveMtp;
	}
	return AccountBackendBindResult::Bound;
}

[[nodiscard]] inline AccountBackendBindResult EvaluateAccountBackendRebuild(
		const AccountBackendRebuildRequest &request) {
	const auto result = EvaluateAccountBackendBindBase(
		request.current,
		request.requested);
	if (result != AccountBackendBindResult::Bound) {
		return result;
	} else if (request.sessionExists) {
		return AccountBackendBindResult::RejectedLiveSession;
	} else if (request.requested == AccountBackend::Core
		&& request.mtpAuthorizationKeysPresent) {
		return AccountBackendBindResult::RejectedCoreWithKeys;
	}
	return AccountBackendBindResult::Bound;
}

[[nodiscard]] inline AccountBackendBindResult TryBindAccountBackend(
		AccountBackendBindState &state,
		AccountBackend requested) {
	const auto result = EvaluateAccountBackendBind({
		state.backend,
		requested,
		state.mtpAlreadyExists,
		state.mtpAuthorizationKeysPresent,
	});
	if (result == AccountBackendBindResult::Bound) {
		state.backend = requested;
	}
	return result;
}

[[nodiscard]] inline bool ShouldPersistAccountBackendTag(
		AccountBackendBindResult result,
		AccountBackend backend) {
	return result == AccountBackendBindResult::Bound
		&& IsBoundAccountBackend(backend);
}

// WHY: a failed sidecar write must not report Bound; in-memory state stays Unspecified.
[[nodiscard]] inline AccountBackendBindResult AccountBackendBindPersistOutcome(
		AccountBackendBindResult result,
		AccountBackend backend,
		bool persistSucceeded) {
	if (result == AccountBackendBindResult::Bound
		&& ShouldPersistAccountBackendTag(result, backend)
		&& !persistSucceeded) {
		return AccountBackendBindResult::RejectedPersistFailed;
	}
	return result;
}

[[nodiscard]] inline bool AccountBackendBindNeedsMtpRebuild(
		AccountBackendBindResult result) {
	return result == AccountBackendBindResult::RejectedLiveMtp;
}

[[nodiscard]] inline bool AccountBackendBindActivates(
		AccountBackendBindResult result) {
	return result == AccountBackendBindResult::Bound
		|| result == AccountBackendBindResult::Unchanged;
}

} // namespace Main
