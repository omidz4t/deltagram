// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
#pragma once

using TranslateProviderMacSwiftCallback = void(*)(
	void *context,
	const char *translatedTextUtf8,
	const char *errorUtf8);

extern "C" {

[[nodiscard]] bool TranslateProviderMacSwiftIsAvailable();

void TranslateProviderMacSwiftTranslate(
	const char *sourceTextUtf8,
	const char *targetLanguageCodeUtf8,
	void *context,
	TranslateProviderMacSwiftCallback callback);

} // extern "C"
