// This file is part of Desktop App Toolkit,
// a set of libraries for developing nice desktop applications.
//
// For license and copyright information please follow this link:
// https://github.com/desktop-app/legal/blob/master/LEGAL
//
import Foundation
import NaturalLanguage
import Translation

typealias TranslateProviderMacSwiftCallback = @convention(c) (
	UnsafeMutableRawPointer?,
	UnsafePointer<CChar>?,
	UnsafePointer<CChar>?
) -> Void

private struct CallbackContext: @unchecked Sendable {
	let value: UnsafeMutableRawPointer?
}

private struct CallbackFunction: @unchecked Sendable {
	let value: TranslateProviderMacSwiftCallback
}

private func duplicatedCString(_ value: String) -> UnsafePointer<CChar>? {
	guard let duplicated = strdup(value) else {
		return nil
	}
	return UnsafePointer(duplicated)
}

@available(macOS 15.0, *)
private func requestTranslation(
		_ text: String,
		_ targetLanguage: String) async throws -> String {
	guard let sourceLanguage
		= NLLanguageRecognizer.dominantLanguage(for: text) else {
			throw TranslationError.unableToIdentifyLanguage
		}
	if sourceLanguage.rawValue == targetLanguage {
		return text
	}
	let source = Locale.Language(identifier: sourceLanguage.rawValue)
	let target = Locale.Language(identifier: targetLanguage)
	let availability = LanguageAvailability()
	let status = await availability.status(from: source, to: target)
	switch status {
	case .installed:
		break
	case .supported, .unsupported:
		throw TranslationError.unsupportedLanguagePairing
	@unknown default:
		throw TranslationError.unsupportedLanguagePairing
	}
#if compiler(>=6.2)
	if #available(macOS 26.0, *) {
		let session = TranslationSession(installedSource: source, target: target)
		let response = try await session.translate(text)
		return response.targetText
	}
#endif
	throw TranslationError.unsupportedLanguagePairing
}

@available(macOS 15.0, *)
private func translateErrorCode(_ error: Error) -> String {
	guard let translationError = error as? TranslationError else {
		return "unknown"
	}
#if compiler(>=6.2)
	if #available(macOS 26.0, *), case .notInstalled = translationError {
		return "local-language-pack-missing"
	}
#endif
	switch translationError {
	case .unsupportedLanguagePairing:
		return "local-language-pack-missing"
	default:
		return "unknown"
	}
}

@_cdecl("TranslateProviderMacSwiftIsAvailable")
func TranslateProviderMacSwiftIsAvailable() -> Bool {
#if compiler(>=6.2)
	if #available(macOS 26.0, *) {
		return true
	}
#endif
	return false
}

@_cdecl("TranslateProviderMacSwiftTranslate")
func TranslateProviderMacSwiftTranslate(
	_ sourceTextUtf8: UnsafePointer<CChar>?,
	_ targetLanguageCodeUtf8: UnsafePointer<CChar>?,
	_ context: UnsafeMutableRawPointer?,
	_ callback: TranslateProviderMacSwiftCallback?
) {
	guard let callback else {
		return
	}
	guard let sourceTextUtf8, let targetLanguageCodeUtf8 else {
		callback(context, nil, duplicatedCString("invalid-arguments"))
		return
	}
	let sourceText = String(cString: sourceTextUtf8)
	let targetLanguageCode = String(cString: targetLanguageCodeUtf8)
	let callbackFunction = CallbackFunction(value: callback)
	let callbackContext = CallbackContext(value: context)
	if #available(macOS 10.15, *) {
		Task.detached(priority: .utility) {
			let callback = callbackFunction.value
			let context = callbackContext.value
#if compiler(>=6.2)
			if #available(macOS 26.0, *) {
				do {
					let translated = try await requestTranslation(
						sourceText,
						targetLanguageCode)
					callback(context, duplicatedCString(translated), nil)
				} catch {
					callback(
						context,
						nil,
						duplicatedCString(translateErrorCode(error)))
				}
				return
			}
#endif
			callback(context, nil, duplicatedCString("unsupported-platform"))
		}
		return
	}
	callback(context, nil, duplicatedCString("unsupported-platform"))
}
