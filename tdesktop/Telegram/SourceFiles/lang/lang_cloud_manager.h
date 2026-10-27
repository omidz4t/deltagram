/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "mtproto/sender.h"
#include "base/weak_ptr.h"
#include "lang/lang_backend.h"
#include "main/account_backend.h"

namespace Main {
class Account;
} // namespace Main

namespace MTP {
class Instance;
} // namespace MTP

namespace Lang {

class Instance;
enum class Pack;
struct Language;

Language ParseLanguage(const MTPLangPackLanguage &data);

class CloudManager : public base::has_weak_ptr {
public:
	explicit CloudManager(Instance &langpack);

	using Languages = std::vector<Language>;

	void requestLanguageList(Main::Account *initiating = nullptr);
	const Languages &languageList() const {
		return _languages;
	}
	[[nodiscard]] rpl::producer<> languageListChanged() const;
	[[nodiscard]] rpl::producer<> firstLanguageSuggestion() const;
	void requestLangPackDifference(const QString &langId);
	void applyLangPackDifference(const MTPLangPackDifference &difference);
	void setCurrentVersions(int version, int baseVersion);

	void resetToDefault();
	void switchWithWarning(
		const QString &id,
		Main::Account *initiating = nullptr);
	void switchToLanguage(
		const QString &id,
		Main::Account *initiating = nullptr);
	void switchToLanguage(
		const Language &data,
		Main::Account *initiating = nullptr);
	void switchToTestLanguage(Main::Account *initiating = nullptr);
	void setSuggestedLanguage(const QString &langCode);
	QString suggestedLanguage() const {
		return _suggestedLanguage;
	}

	void getValueForLang(
		const QString &key,
		const QString &langId,
		Fn<void(const QString &)> callback);

private:
	mtpRequestId &packRequestId(Pack pack);
	mtpRequestId packRequestId(Pack pack) const;
	Pack packTypeFromId(const QString &id) const;
	void requestLangPackDifference(Pack pack);
	bool canApplyWithoutRestart(const QString &id) const;
	void performSwitchToCustom(Main::Account *initiating);
	void performSwitch(const Language &data);
	void performSwitchAndAddToRecent(const Language &data);
	void performSwitchAndRestart(const Language &data);
	void restartAfterSwitch();
	void offerSwitchLangPack();
	bool showOfferSwitchBox();
	Language findOfferedLanguage() const;

	void requestLanguageAndSwitch(
		const QString &id,
		bool warning,
		Main::Account *initiating);
	void applyLangPackData(Pack pack, const MTPDlangPackDifference &data);
	void switchLangPackId(const Language &data);
	void changeIdAndReInitConnection(const Language &data);

	void sendSwitchingToLanguageRequest();
	void resendPendingValueRequests();
	void resendRequests();
	void refreshBackingAccount();
	void bindBackingAccount(Main::Account *account);
	void markInFlightForResend();
	void failPendingValueRequests();
	[[nodiscard]] Main::AccountBackend backingBackend() const;
	[[nodiscard]] bool allowsCloudRequest(
		Main::Account *initiating,
		LanguageRequestOrigin origin) const;

	std::optional<MTP::Sender> _api;
	base::weak_ptr<Main::Account> _backingAccount;
	base::weak_ptr<Main::Account> _activeAccount;
	rpl::lifetime _backingLifetime;
	int _backingGeneration = 0;
	Instance &_langpack;
	Languages _languages;
	mtpRequestId _langPackRequestId = 0;
	mtpRequestId _langPackBaseRequestId = 0;
	mtpRequestId _languagesRequestId = 0;

	QString _offerSwitchToId;
	bool _restartAfterSwitch = false;

	QString _suggestedLanguage;
	bool _languageWasSuggested = false;

	mtpRequestId _switchingToLanguageRequest = 0;
	QString _switchingToLanguageId;
	bool _switchingToLanguageWarning = false;
	base::weak_ptr<Main::Account> _switchingToLanguageInitiating;

	mtpRequestId _getKeysForSwitchRequestId = 0;

	struct ValueRequest {
		mtpRequestId requestId = 0;
		Fn<void(const QString &)> callback;
	};
	base::flat_map<QString, ValueRequest> _getValueForLangRequests;

	rpl::event_stream<> _languageListChanged;
	rpl::event_stream<> _firstLanguageSuggestion;

	rpl::lifetime _lifetime;

};

CloudManager &CurrentCloudManager();

} // namespace Lang
