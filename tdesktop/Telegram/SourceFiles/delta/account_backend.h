#pragma once

#include "base/basic_types.h"
#include "main/account_backend.h"
#include "main/main_account.h"
#include "main/main_session.h"

namespace Delta {

[[nodiscard]] inline bool UsesCoreBackend(not_null<Main::Account*> account) {
	return account->backend() == Main::AccountBackend::Core;
}

[[nodiscard]] inline bool UsesCoreBackend(Main::Account *account) {
	return account && UsesCoreBackend(not_null<Main::Account*>(account));
}

[[nodiscard]] inline bool UsesCoreBackend(not_null<Main::Session*> session) {
	return UsesCoreBackend(&session->account());
}

[[nodiscard]] inline bool UsesCoreBackend(Main::Session *session) {
	return session && UsesCoreBackend(not_null<Main::Session*>(session));
}

[[nodiscard]] inline bool UsesTelegramBackend(
		not_null<Main::Account*> account) {
	return account->backend() == Main::AccountBackend::Telegram;
}

[[nodiscard]] inline bool UsesTelegramBackend(Main::Account *account) {
	return account
		&& UsesTelegramBackend(not_null<Main::Account*>(account));
}

[[nodiscard]] inline bool UsesTelegramBackend(
		not_null<Main::Session*> session) {
	return UsesTelegramBackend(&session->account());
}

[[nodiscard]] inline bool UsesTelegramBackend(Main::Session *session) {
	return session && UsesTelegramBackend(not_null<Main::Session*>(session));
}

} // namespace Delta
