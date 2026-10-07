// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "spellcheck/spellcheck_types.h"
#include "ui/text/text_entity.h"

namespace Ui {

enum class TranslateProviderError {
	None = 0,
	Unknown,
	LocalLanguagePackMissing,
};

struct TranslateProviderResult {
	std::optional<TextWithEntities> text;
	TranslateProviderError error = TranslateProviderError::None;
};

struct TranslateProviderRequest {
	using PeerId = uint64;
	using MsgId = int64;

	PeerId peerId = 0;
	MsgId msgId = 0;
	TextWithEntities text;
};

class TranslateProvider {
public:
	virtual ~TranslateProvider() = default;
	[[nodiscard]] virtual bool supportsMessageId() const = 0;
	virtual void request(
		TranslateProviderRequest request,
		LanguageId to,
		Fn<void(TranslateProviderResult)> done) = 0;
	virtual void requestBatch(
			std::vector<TranslateProviderRequest> requests,
			const LanguageId &to,
			Fn<void(int, TranslateProviderResult)> doneOne,
			Fn<void()> doneAll) {
		if (requests.empty()) {
			doneAll();
			return;
		}
		if (requests.size() == 1) {
			request(
				std::move(requests.front()),
				to,
				[doneOne = std::move(doneOne), doneAll = std::move(doneAll)](
						TranslateProviderResult result) {
					doneOne(0, std::move(result));
					doneAll();
				});
			return;
		}
		struct State {
			int remaining = 0;
			Fn<void(int, TranslateProviderResult)> doneOne;
			Fn<void()> doneAll;
		};
		auto state = std::make_shared<State>(State{
			.remaining = int(requests.size()),
			.doneOne = std::move(doneOne),
			.doneAll = std::move(doneAll),
		});
		for (auto i = 0; i != requests.size(); ++i) {
			request(
				std::move(requests[i]),
				to,
				[=](TranslateProviderResult result) {
					state->doneOne(i, std::move(result));
					if (!--state->remaining) {
						state->doneAll();
					}
				});
		}
	}
};

} // namespace Ui
