#pragma once

#include <cstdint>

namespace Delta {

// The bridge uses positive signed 32-bit Core IDs. Keep both identities in
// native media/cache keys, including across process restarts and profile order.
[[nodiscard]] constexpr std::uint64_t MediaIdentity(
		std::int32_t accountId,
		std::int32_t messageId) {
	return (accountId > 0 && messageId > 0)
		? (std::uint64_t(1) << 62)
			| (std::uint64_t(accountId) << 31)
			| std::uint64_t(messageId)
		: 0;
}

} // namespace Delta
