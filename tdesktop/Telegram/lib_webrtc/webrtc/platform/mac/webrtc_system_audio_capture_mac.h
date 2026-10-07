// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_system_audio_capture.h"

#include <memory>

namespace Webrtc::details {

class SystemAudioCaptureMac final : public SystemAudioCapture {
public:
	explicit SystemAudioCaptureMac(SystemAudioSamplesCallback callback);
	~SystemAudioCaptureMac() override;

	void start() override;
	void stop() override;

	[[nodiscard]] static bool IsSupported();

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;

};

} // namespace Webrtc::details
