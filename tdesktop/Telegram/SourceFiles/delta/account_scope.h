#pragma once

#include <functional>
#include <memory>
#include <utility>

namespace Delta {

class AccountScope final {
public:
	void invalidate() {
		_token = std::make_shared<bool>(true);
	}

	[[nodiscard]] std::function<bool()> guard() const {
		return [token = std::weak_ptr<bool>(_token)] { return !token.expired(); };
	}

	template <typename Callback>
	auto bind(Callback callback) const {
		return [guard = std::weak_ptr<bool>(_token), callback = std::move(callback)](auto reply) {
			if (!guard.expired()) callback(std::move(reply));
		};
	}

private:
	std::shared_ptr<bool> _token = std::make_shared<bool>(true);
};

} // namespace Delta
