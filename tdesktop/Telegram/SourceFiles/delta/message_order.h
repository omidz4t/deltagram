#pragma once

#include <algorithm>
#include <optional>
#include <vector>

namespace Delta {

// Core supplies oldest first. Donor history pages consume newest first.
inline std::optional<std::vector<int>> HistoryPage(
		std::vector<int> ids,
		int anchor,
		int offset,
		int limit) {
	std::reverse(ids.begin(), ids.end());
	auto start = 0;
	if (anchor > 0) {
		const auto i = std::find(ids.begin(), ids.end(), anchor);
		if (i == ids.end()) return std::nullopt;
		start = int(i - ids.begin()) + 1;
	}
	const auto end = std::clamp(start + offset + limit, 0, int(ids.size()));
	start = std::clamp(start + offset, 0, end);
	return std::vector<int>(ids.begin() + start, ids.begin() + end);
}

} // namespace Delta
