// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

#include <modules/audio_device/include/audio_device.h>
#include <modules/audio_device/audio_device_buffer.h>

#include <memory>

namespace Webrtc::details {

class AudioDeviceLoopbackMac : public webrtc::AudioDeviceModule {
public:
	explicit AudioDeviceLoopbackMac(
		webrtc::TaskQueueFactory *taskQueueFactory);
	~AudioDeviceLoopbackMac();

	int32_t ActiveAudioLayer(AudioLayer *audioLayer) const override;
	int32_t RegisterAudioCallback(
		webrtc::AudioTransport *audioCallback) override;

	// Main initialization and termination.
	int32_t Init() override;
	int32_t Terminate() override;
	bool Initialized() const override;

	// Device enumeration.
	int16_t PlayoutDevices() override;
	int16_t RecordingDevices() override;
	int32_t PlayoutDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) override;
	int32_t RecordingDeviceName(
		uint16_t index,
		char name[webrtc::kAdmMaxDeviceNameSize],
		char guid[webrtc::kAdmMaxGuidSize]) override;

	// Device selection.
	int32_t SetPlayoutDevice(uint16_t index) override;
	int32_t SetPlayoutDevice(WindowsDeviceType device) override;
	int32_t SetRecordingDevice(uint16_t index) override;
	int32_t SetRecordingDevice(WindowsDeviceType device) override;

	// Audio transport initialization.
	int32_t PlayoutIsAvailable(bool *available) override;
	int32_t InitPlayout() override;
	bool PlayoutIsInitialized() const override;
	int32_t RecordingIsAvailable(bool *available) override;
	int32_t InitRecording() override;
	bool RecordingIsInitialized() const override;

	// Audio transport control.
	int32_t StartPlayout() override;
	int32_t StopPlayout() override;
	bool Playing() const override;
	int32_t StartRecording() override;
	int32_t StopRecording() override;
	bool Recording() const override;

	// Audio mixer initialization.
	int32_t InitSpeaker() override;
	bool SpeakerIsInitialized() const override;
	int32_t InitMicrophone() override;
	bool MicrophoneIsInitialized() const override;

	// Speaker volume controls.
	int32_t SpeakerVolumeIsAvailable(bool *available) override;
	int32_t SetSpeakerVolume(uint32_t volume) override;
	int32_t SpeakerVolume(uint32_t *volume) const override;
	int32_t MaxSpeakerVolume(uint32_t *maxVolume) const override;
	int32_t MinSpeakerVolume(uint32_t *minVolume) const override;

	// Microphone volume controls.
	int32_t MicrophoneVolumeIsAvailable(bool *available) override;
	int32_t SetMicrophoneVolume(uint32_t volume) override;
	int32_t MicrophoneVolume(uint32_t *volume) const override;
	int32_t MaxMicrophoneVolume(uint32_t *maxVolume) const override;
	int32_t MinMicrophoneVolume(uint32_t *minVolume) const override;

	// Microphone mute control.
	int32_t MicrophoneMuteIsAvailable(bool *available) override;
	int32_t SetMicrophoneMute(bool enable) override;
	int32_t MicrophoneMute(bool *enabled) const override;

	// Speaker mute control.
	int32_t SpeakerMuteIsAvailable(bool *available) override;
	int32_t SetSpeakerMute(bool enable) override;
	int32_t SpeakerMute(bool *enabled) const override;

	// Stereo support.
	int32_t StereoPlayoutIsAvailable(bool *available) const override;
	int32_t SetStereoPlayout(bool enable) override;
	int32_t StereoPlayout(bool *enabled) const override;
	int32_t StereoRecordingIsAvailable(bool *available) const override;
	int32_t SetStereoRecording(bool enable) override;
	int32_t StereoRecording(bool *enabled) const override;

	// Delay information and control.
	int32_t PlayoutDelay(uint16_t *delayMS) const override;

	// Only supported on Android.
	bool BuiltInAECIsAvailable() const override;
	bool BuiltInAGCIsAvailable() const override;
	bool BuiltInNSIsAvailable() const override;

	int32_t EnableBuiltInAEC(bool enable) override;
	int32_t EnableBuiltInAGC(bool enable) override;
	int32_t EnableBuiltInNS(bool enable) override;

	[[nodiscard]] static bool IsSupported();

private:
	webrtc::AudioDeviceBuffer _audioDeviceBuffer;

	struct ObjCState;
	std::unique_ptr<ObjCState> _objc;

	bool _initialized = false;
	bool _microphoneInitialized = false;
	bool _recordingInitialized = false;
	bool _recording = false;

};

} // namespace Webrtc::details
