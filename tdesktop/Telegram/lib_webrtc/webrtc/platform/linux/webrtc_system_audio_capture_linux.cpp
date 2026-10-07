// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/linux/webrtc_system_audio_capture_linux.h"

#include "webrtc/platform/linux/webrtc_loopback_adm_linux.h"
#include "webrtc/platform/linux/webrtc_loopback_capture_linux.h"

#include <utility>

namespace Webrtc::details {

SystemAudioCaptureLinux::SystemAudioCaptureLinux(
		SystemAudioSamplesCallback callback)
: _callback(std::move(callback)) {
}

SystemAudioCaptureLinux::~SystemAudioCaptureLinux() {
	stop();
}

void SystemAudioCaptureLinux::start() {
	if (_started) {
		return;
	}
	_started = true;
	SetLoopbackCaptureSamplesCallbackLinux(_callback);
	SetLoopbackCaptureActiveLinux(true);
}

void SystemAudioCaptureLinux::stop() {
	if (!_started) {
		return;
	}
	_started = false;
	SetLoopbackCaptureSamplesCallbackLinux(nullptr);
	SetLoopbackCaptureActiveLinux(false);
}

bool SystemAudioCaptureLinux::IsSupported() {
	return AudioDeviceLoopbackLinux::IsSupported();
}

} // namespace Webrtc::details
