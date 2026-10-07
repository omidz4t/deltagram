// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/webrtc_system_audio_capture.h"

namespace Webrtc::details {

class SystemAudioCaptureLinux final : public SystemAudioCapture {
public:
	explicit SystemAudioCaptureLinux(SystemAudioSamplesCallback callback);
	~SystemAudioCaptureLinux() override;

	void start() override;
	void stop() override;

	[[nodiscard]] static bool IsSupported();

private:
	SystemAudioSamplesCallback _callback;
	bool _started = false;
};

} // namespace Webrtc::details
