/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/translate_provider.h"

#include "base/options.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "lang/lang_backend.h"
#include "lang/translate_mtproto_provider.h"
#include "lang/translate_url_provider.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "platform/platform_translate_provider.h"

namespace {

base::options::option<QString> OptionTranslateUrlTemplate({
	.id = "translate-url-template",
	.name = "Translate URL template",
	.description = "Template URL for custom translation provider."
		" Supports %q text, %f source language and %t target language.",
});

[[nodiscard]] QString TranslateUrlTemplate() {
	return OptionTranslateUrlTemplate.value();
}

[[nodiscard]] bool TranslateUrlTemplateValid(const QString &urlTemplate) {
	return !urlTemplate.isEmpty() && urlTemplate.contains(u"%q"_q);
}

[[nodiscard]] bool CachedPlatformTranslateAvailable() {
	static const auto available = Platform::IsTranslateProviderAvailable();
	return available;
}

class UnavailableTranslateProvider final : public Ui::TranslateProvider {
public:
	[[nodiscard]] bool supportsMessageId() const override {
		return false;
	}

	void request(
			Ui::TranslateProviderRequest,
			LanguageId,
			Fn<void(Ui::TranslateProviderResult)> done) override {
		done(Ui::TranslateProviderResult{
			.error = Ui::TranslateProviderError::Unknown,
		});
	}

};

} // namespace

namespace Ui {

bool IsTranslateBackendAvailable(Main::Account *account) {
	return Lang::TranslateBackendAvailable(
		account
			? account->backend()
			: Main::AccountBackend::Unspecified,
		TranslateUrlTemplateValid(TranslateUrlTemplate()),
		CachedPlatformTranslateAvailable());
}

bool IsTranslateBackendAvailable(not_null<Main::Account*> account) {
	return IsTranslateBackendAvailable(account.get());
}

bool IsTranslateBackendAvailable(not_null<Main::Session*> session) {
	return IsTranslateBackendAvailable(&session->account());
}

std::unique_ptr<TranslateProvider> CreateTranslateProvider(
		not_null<Main::Session*> session) {
	const auto urlTemplate = TranslateUrlTemplate();
	const auto backend = session->account().backend();
	const auto kind = Lang::PlanTranslateProvider({
		.backend = backend,
		.urlTemplateValid = TranslateUrlTemplateValid(urlTemplate),
		.platformAvailable = (backend == Main::AccountBackend::Telegram)
			? Platform::IsTranslateProviderAvailable()
			: CachedPlatformTranslateAvailable(),
		.usePlatformTranslation
			= Core::App().settings().usePlatformTranslation(),
	});
	switch (kind) {
	case Lang::TranslateProviderKind::Url:
		return CreateUrlTranslateProvider(urlTemplate);
	case Lang::TranslateProviderKind::Platform:
		if (auto provider = Platform::CreateTranslateProvider()) {
			return provider;
		}
		if (backend == Main::AccountBackend::Telegram) {
			return CreateMTProtoTranslateProvider(session);
		}
		return std::make_unique<UnavailableTranslateProvider>();
	case Lang::TranslateProviderKind::Mtproto:
		return CreateMTProtoTranslateProvider(session);
	case Lang::TranslateProviderKind::Unavailable:
		return std::make_unique<UnavailableTranslateProvider>();
	}
	return std::make_unique<UnavailableTranslateProvider>();
}

TranslateProviderRequest PrepareTranslateProviderRequest(
		not_null<TranslateProvider*> provider,
		not_null<PeerData*> peer,
		MsgId msgId,
		TextWithEntities text) {
	auto result = TranslateProviderRequest{
		.peerId = uint64(peer->id.value),
		.msgId = IsServerMsgId(msgId) ? msgId.bare : 0,
		.text = std::move(text),
	};
	if (provider->supportsMessageId()) {
		return result;
	}
	if (result.msgId) {
		if (const auto i = peer->owner().message(peer, MsgId(result.msgId))) {
			result.text = i->originalText();
		}
		result.msgId = 0;
	}
	return result;
}

} // namespace Ui
