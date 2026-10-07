// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include "webrtc/details/webrtc_openal_adm.h"
#include "webrtc/platform/linux/webrtc_loopback_capture_linux.h"

namespace Webrtc::details {

class AudioDeviceLoopbackLinux : public AudioDeviceOpenAL {
public:
	explicit AudioDeviceLoopbackLinux(webrtc::TaskQueueFactory *taskQueueFactory);

	[[nodiscard]] static bool IsSupported();

private:
	[[nodiscard]] static std::vector<QString> LoopbackCaptureDeviceIds();
};

} // namespace Webrtc::details
