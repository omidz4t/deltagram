// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Webrtc {

using SystemAudioSamplesCallback = std::function<void(std::vector<uint8_t> &&)>;

class SystemAudioCapture {
public:
	virtual ~SystemAudioCapture() = default;

	virtual void start() = 0;
	virtual void stop() = 0;
};

[[nodiscard]] bool SystemAudioCaptureSupported();

[[nodiscard]] std::unique_ptr<SystemAudioCapture> CreateSystemAudioCapture(
	SystemAudioSamplesCallback callback);

} // namespace Webrtc
