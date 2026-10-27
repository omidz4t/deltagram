#pragma once

#include <vector>

namespace Delta {

struct UnreadSlice {
	int count = 0;
	bool muted = true;
};

struct DomainAccountUnread {
	bool coreBackend = false;
	int count = 0;
	bool muted = true;
};

[[nodiscard]] inline UnreadSlice CombineUnread(
		UnreadSlice core,
		UnreadSlice telegram) {
	return {
		.count = core.count + telegram.count,
		.muted = (core.count > 0) ? false : telegram.muted,
	};
}

[[nodiscard]] inline UnreadSlice TelegramDomainUnread(
		const std::vector<DomainAccountUnread> &accounts) {
	UnreadSlice out;
	out.muted = true;
	for (const auto &account : accounts) {
		if (account.coreBackend) {
			continue;
		}
		out.count += account.count;
		if (!account.muted) {
			out.muted = false;
		}
	}
	return out;
}

[[nodiscard]] inline UnreadSlice OtherTelegramUnread(
		const std::vector<DomainAccountUnread> &accounts,
		int skipIndex) {
	UnreadSlice out;
	out.muted = true;
	auto index = 0;
	for (const auto &account : accounts) {
		const auto skip = (index == skipIndex);
		++index;
		if (account.coreBackend || skip) {
			continue;
		}
		out.count += account.count;
		if (!account.muted) {
			out.muted = false;
		}
	}
	return out;
}

} // namespace Delta
