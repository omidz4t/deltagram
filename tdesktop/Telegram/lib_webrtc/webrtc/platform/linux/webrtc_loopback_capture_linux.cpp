// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/linux/webrtc_loopback_capture_linux.h"

#include <api/audio/audio_frame.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Webrtc::details {
namespace {

constexpr auto kBufferSizeMs = crl::time(10);

constexpr auto kFarEndFrequency = 48000;
constexpr auto kFarEndChannels = 2;
constexpr auto kFarEndFramesCount = 1000 / kBufferSizeMs;
constexpr auto kFarEndChannelFrameSize = (kFarEndFrequency * kBufferSizeMs)
	/ 1000;
static_assert(kFarEndChannelFrameSize * 1000
	== kFarEndFrequency * kBufferSizeMs);

constexpr auto kMaxEchoDelay = crl::time(1000);

enum class FarEndFrameState : uint8_t {
	Empty,
	Writing,
	Reading,
	Ready,
};

struct FarEndFrame {
	std::array<std::int16_t, kFarEndChannelFrameSize * kFarEndChannels> data;
	crl::time when = 0;
	std::atomic<FarEndFrameState> state = FarEndFrameState::Empty;
};

struct FarEnd {
	std::array<FarEndFrame, kFarEndFramesCount> frames;
	std::atomic<int> writeIndex = 0;
	std::atomic<int> readIndex = 0;
};

std::atomic<int> LoopbackCaptureRefCount = 0;
FarEnd LoopbackFarEnd;
std::mutex LoopbackCaptureSamplesCallbackMutex;
std::function<void(std::vector<uint8_t> &&)> LoopbackCaptureSamplesCallback;

} // namespace

bool IsLoopbackCaptureActiveLinux() {
	return LoopbackCaptureRefCount.load(std::memory_order_relaxed) > 0;
}

void SetLoopbackCaptureActiveLinux(bool active) {
	if (active) {
		LoopbackCaptureRefCount.fetch_add(1, std::memory_order_relaxed);
	} else {
		auto old = LoopbackCaptureRefCount.load(std::memory_order_relaxed);
		while (old > 0 && !LoopbackCaptureRefCount.compare_exchange_weak(
			old,
			old - 1,
			std::memory_order_relaxed)) {
		}
	}
}

void LoopbackCapturePushFarEndLinux(
		crl::time when,
		const QByteArray &samples,
		int frequency,
		int channels) {
	if (frequency != kFarEndFrequency
		|| channels != kFarEndChannels
		|| samples.size()
			!= kFarEndChannelFrameSize
				* kFarEndChannels
				* int(sizeof(std::int16_t))) {
		return;
	}

	using State = FarEndFrameState;

	const auto index = LoopbackFarEnd.writeIndex.load(
		std::memory_order_relaxed);
	auto &frame = LoopbackFarEnd.frames[index];
	auto empty = State::Empty;
	if (!frame.state.compare_exchange_strong(empty, State::Writing)) {
		return;
	}
	memcpy(frame.data.data(), samples.constData(), samples.size());
	frame.when = when;
	frame.state.store(State::Ready, std::memory_order_release);
	LoopbackFarEnd.writeIndex.store(
		(index + 1) % kFarEndFramesCount,
		std::memory_order_relaxed);

	auto callback = std::function<void(std::vector<uint8_t> &&)>();
	{
		const auto guard = std::lock_guard(LoopbackCaptureSamplesCallbackMutex);
		callback = LoopbackCaptureSamplesCallback;
	}
	if (callback) {
		auto copy = std::vector<uint8_t>(samples.size());
		memcpy(copy.data(), samples.constData(), samples.size());
		callback(std::move(copy));
	}
}

void SetLoopbackCaptureSamplesCallbackLinux(
		std::function<void(std::vector<uint8_t> &&samples)> callback) {
	const auto guard = std::lock_guard(LoopbackCaptureSamplesCallbackMutex);
	LoopbackCaptureSamplesCallback = std::move(callback);
}

std::optional<crl::time> LoopbackCaptureTakeFarEndLinux(
		webrtc::AudioFrame &to,
		crl::time nearEndWhen) {
	if (to.sample_rate_hz_ != kFarEndFrequency
		|| to.num_channels_ != kFarEndChannels
		|| to.samples_per_channel_ != kFarEndChannelFrameSize) {
		return std::nullopt;
	}

	using State = FarEndFrameState;

	while (true) {
		const auto index = LoopbackFarEnd.readIndex.load(
			std::memory_order_relaxed);
		auto &frame = LoopbackFarEnd.frames[index];
		auto ready = State::Ready;
		if (!frame.state.compare_exchange_strong(ready, State::Reading)) {
			return std::nullopt;
		}
		const auto delay = frame.when - nearEndWhen;
		if (delay > kMaxEchoDelay) {
			frame.state.store(State::Ready, std::memory_order_relaxed);
			return std::nullopt;
		}
		if (delay >= 0) {
			memcpy(
				to.mutable_data(),
				frame.data.data(),
				frame.data.size() * sizeof(std::int16_t));
		}
		frame.state.store(State::Empty, std::memory_order_release);
		LoopbackFarEnd.readIndex.store(
			(index + 1) % kFarEndFramesCount,
			std::memory_order_relaxed);
		if (delay >= 0) {
			return delay;
		}
	}
}

} // namespace Webrtc::details
