#pragma once

#include "main/account_backend.h"

#include <QtCore/QString>

namespace Lang {

enum class TranslateProviderKind : quint8 {
	Url = 0,
	Platform = 1,
	Mtproto = 2,
	Unavailable = 3,
};

enum class LanguageRequestOrigin : quint8 {
	System = 0,
	ExplicitAccount = 1,
};

enum class LanguageCloudRequestPolicy : quint8 {
	Skip = 0,
	Allow = 1,
};

enum class LanguageCustomConfirmCopy : quint8 {
	CoreRestart = 0,
	TelegramBilingual = 1,
};

enum class LanguageBackingPick : quint8 {
	None = 0,
	ActiveTelegram = 1,
	KeepExistingTelegram = 2,
	FirstTelegram = 3,
};

struct TranslateProviderPlan {
	Main::AccountBackend backend = Main::AccountBackend::Unspecified;
	bool urlTemplateValid = false;
	bool platformAvailable = false;
	bool usePlatformTranslation = false;
};

struct LanguagePickerFeatures {
	bool localCatalog = false;
	bool cloudCatalog = false;
	bool premiumChatTranslate = false;
};

struct LanguageCloudRequest {
	LanguageRequestOrigin origin = LanguageRequestOrigin::System;
	Main::AccountBackend initiating = Main::AccountBackend::Unspecified;
	Main::AccountBackend backing = Main::AccountBackend::Unspecified;
};

struct LanguageBackingEvidence {
	Main::AccountBackend active = Main::AccountBackend::Unspecified;
	bool existingBackingIsLiveTelegram = false;
	bool telegramAccountAvailable = false;
};

[[nodiscard]] inline TranslateProviderKind PlanTranslateProvider(
		const TranslateProviderPlan &plan) {
	if (plan.urlTemplateValid) {
		return TranslateProviderKind::Url;
	} else if (plan.backend == Main::AccountBackend::Telegram) {
		if (plan.usePlatformTranslation && plan.platformAvailable) {
			return TranslateProviderKind::Platform;
		}
		return TranslateProviderKind::Mtproto;
	} else if (plan.platformAvailable) {
		return TranslateProviderKind::Platform;
	}
	return TranslateProviderKind::Unavailable;
}

[[nodiscard]] inline bool TranslateBackendAvailable(
		Main::AccountBackend backend,
		bool urlTemplateValid,
		bool platformAvailable) {
	if (backend == Main::AccountBackend::Telegram) {
		return true;
	}
	return urlTemplateValid || platformAvailable;
}

[[nodiscard]] inline LanguagePickerFeatures PlanLanguagePicker(
		Main::AccountBackend initiating) {
	LanguagePickerFeatures features;
	if (initiating == Main::AccountBackend::Telegram) {
		features.cloudCatalog = true;
		features.premiumChatTranslate = true;
	} else {
		features.localCatalog = true;
	}
	return features;
}

[[nodiscard]] inline LanguageCloudRequestPolicy PlanLanguageCloudRequest(
		const LanguageCloudRequest &request) {
	if (request.backing != Main::AccountBackend::Telegram) {
		return LanguageCloudRequestPolicy::Skip;
	} else if (request.origin == LanguageRequestOrigin::System) {
		return LanguageCloudRequestPolicy::Allow;
	} else if (request.initiating == Main::AccountBackend::Telegram) {
		return LanguageCloudRequestPolicy::Allow;
	}
	return LanguageCloudRequestPolicy::Skip;
}

[[nodiscard]] inline LanguageCustomConfirmCopy PlanLanguageCustomConfirm(
		Main::AccountBackend initiating) {
	return (initiating == Main::AccountBackend::Telegram)
		? LanguageCustomConfirmCopy::TelegramBilingual
		: LanguageCustomConfirmCopy::CoreRestart;
}

[[nodiscard]] inline LanguageBackingPick PlanLanguageBacking(
		const LanguageBackingEvidence &evidence) {
	if (evidence.active == Main::AccountBackend::Telegram) {
		return LanguageBackingPick::ActiveTelegram;
	} else if (evidence.existingBackingIsLiveTelegram) {
		return LanguageBackingPick::KeepExistingTelegram;
	} else if (evidence.telegramAccountAvailable) {
		return LanguageBackingPick::FirstTelegram;
	}
	return LanguageBackingPick::None;
}

[[nodiscard]] inline QString CombineLanguageRestartCopy(
		LanguageCustomConfirmCopy copy,
		const QString &current,
		const QString &target) {
	if (copy == LanguageCustomConfirmCopy::CoreRestart && current == target) {
		return current;
	}
	return current + QString("\n\n") + target;
}

} // namespace Lang
