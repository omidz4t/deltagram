// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#include "webrtc/platform/linux/webrtc_loopback_adm_linux.h"

#include <al.h>
#include <alc.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Webrtc::details {
namespace {

[[nodiscard]] std::string ToLower(std::string value) {
	for (auto &ch : value) {
		ch = char(std::tolower(static_cast<unsigned char>(ch)));
	}
	return value;
}

[[nodiscard]] std::string TrimOpenALPrefix(std::string value) {
	constexpr auto kPrefix = "OpenAL Soft on ";
	constexpr auto kPrefixSize = sizeof(kPrefix) - 1;
	if (value.rfind(kPrefix, 0) == 0) {
		return value.substr(kPrefixSize);
	}
	return value;
}

[[nodiscard]] bool LooksLikeLoopbackDevice(
		const std::string &name,
		const std::string &id) {
	const auto lowerName = ToLower(name);
	const auto lowerId = ToLower(id);
	return (lowerName.find("monitor") != std::string::npos)
		|| (lowerId.find("monitor") != std::string::npos)
		|| (lowerName.find("loopback") != std::string::npos)
		|| (lowerId.find("loopback") != std::string::npos)
		|| (lowerName.find("stereo mix") != std::string::npos)
		|| (lowerName.find("what u hear") != std::string::npos);
}

[[nodiscard]] std::vector<std::string> FindLoopbackCaptureDeviceIdsOpenAL() {
	auto result = std::vector<std::string>();
	const auto pushUnique = [&](const std::string &id) {
		if (id.empty()
			|| std::find(begin(result), end(result), id) != end(result)) {
			return;
		}
		result.push_back(id);
	};

	auto all = std::vector<std::string>();
	if (const auto devices = alcGetString(nullptr, ALC_CAPTURE_DEVICE_SPECIFIER)
		; devices) {
		for (auto i = devices; *i != 0;) {
			const auto id = std::string(i);
			if (LooksLikeLoopbackDevice(id, id)) {
				all.push_back(id);
			}
			i += id.size() + 1;
		}
	}
	if (all.empty()) {
		return result;
	}

	if (const auto defaultCapture = alcGetString(
			nullptr,
			ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
		defaultCapture
		&& LooksLikeLoopbackDevice(defaultCapture, defaultCapture)) {
		pushUnique(defaultCapture);
	}

	const auto playbackName = [&] {
		const auto defaultPlayback = alcGetString(
			nullptr,
			ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
		return defaultPlayback
			? ToLower(TrimOpenALPrefix(defaultPlayback))
			: std::string();
	}();
	if (!playbackName.empty()) {
		for (const auto &id : all) {
			if (ToLower(id).find(playbackName) != std::string::npos) {
				pushUnique(id);
			}
		}
	}
	for (const auto &id : all) {
		pushUnique(id);
	}
	return result;
}

} // namespace

AudioDeviceLoopbackLinux::AudioDeviceLoopbackLinux(
		webrtc::TaskQueueFactory *taskQueueFactory)
: AudioDeviceOpenAL(taskQueueFactory) {
	setLoopbackCaptureDeviceIds(LoopbackCaptureDeviceIds());
}

bool AudioDeviceLoopbackLinux::IsSupported() {
	return !LoopbackCaptureDeviceIds().empty();
}

std::vector<QString> AudioDeviceLoopbackLinux::LoopbackCaptureDeviceIds() {
	const auto ids = FindLoopbackCaptureDeviceIdsOpenAL();
	auto result = std::vector<QString>();
	result.reserve(ids.size());
	for (const auto &id : ids) {
		result.push_back(QString::fromUtf8(id.c_str()));
	}
	return result;
}

} // namespace Webrtc::details
