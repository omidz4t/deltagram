// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/webrtc_system_audio_capture.h"

#ifdef WEBRTC_LINUX
#include "webrtc/platform/linux/webrtc_system_audio_capture_linux.h"
#elif defined WEBRTC_MAC // WEBRTC_LINUX
#include "webrtc/platform/mac/webrtc_system_audio_capture_mac.h"
#endif // WEBRTC_LINUX || WEBRTC_MAC

#include <utility>

namespace Webrtc {

bool SystemAudioCaptureSupported() {
#ifdef WEBRTC_LINUX
	return details::SystemAudioCaptureLinux::IsSupported();
#elif defined WEBRTC_MAC // WEBRTC_LINUX
	return details::SystemAudioCaptureMac::IsSupported();
#else // WEBRTC_LINUX || WEBRTC_MAC
	return false;
#endif // WEBRTC_LINUX || WEBRTC_MAC
}

std::unique_ptr<SystemAudioCapture> CreateSystemAudioCapture(
		SystemAudioSamplesCallback callback) {
#ifdef WEBRTC_LINUX
	if (!details::SystemAudioCaptureLinux::IsSupported()) {
		return nullptr;
	}
	return std::make_unique<details::SystemAudioCaptureLinux>(
		std::move(callback));
#elif defined WEBRTC_MAC // WEBRTC_LINUX
	if (!details::SystemAudioCaptureMac::IsSupported()) {
		return nullptr;
	}
	return std::make_unique<details::SystemAudioCaptureMac>(
		std::move(callback));
#else // WEBRTC_LINUX || WEBRTC_MAC
	return nullptr;
#endif // WEBRTC_LINUX || WEBRTC_MAC
}

} // namespace Webrtc
