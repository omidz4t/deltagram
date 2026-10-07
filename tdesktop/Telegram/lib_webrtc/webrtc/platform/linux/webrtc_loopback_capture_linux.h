// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <crl/crl_time.h>

#include <QtCore/QByteArray>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace webrtc {
class AudioFrame;
} // namespace webrtc

namespace Webrtc::details {

[[nodiscard]] bool IsLoopbackCaptureActiveLinux();

void SetLoopbackCaptureActiveLinux(bool active);

void LoopbackCapturePushFarEndLinux(
	crl::time when,
	const QByteArray &samples,
	int frequency,
	int channels);

void SetLoopbackCaptureSamplesCallbackLinux(
	std::function<void(std::vector<uint8_t> &&samples)> callback);

[[nodiscard]] std::optional<crl::time> LoopbackCaptureTakeFarEndLinux(
	webrtc::AudioFrame &to,
	crl::time nearEndWhen);

} // namespace Webrtc::details
