/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "lang/lang_cloud_manager.h"

#include "lang/lang_backend.h"
#include "lang/lang_instance.h"
#include "lang/lang_file_parser.h"
#include "lang/lang_text_entity.h"
#include "mtproto/mtp_instance.h"
#include "storage/localstorage.h"
#include "core/application.h"
#include "delta/account_backend.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/widgets/labels.h"
#include "ui/text/text_utilities.h"
#include "core/file_utilities.h"
#include "core/click_handler_types.h"
#include "boxes/abstract_box.h" // Ui::hideLayer().
#include "styles/style_layers.h"

namespace Lang {
namespace {

class ConfirmSwitchBox : public Ui::BoxContent {
public:
	ConfirmSwitchBox(
		QWidget*,
		const MTPDlangPackLanguage &data,
		Fn<void()> apply);

protected:
	void prepare() override;

private:
	QString _name;
	int _percent = 0;
	bool _official = false;
	QString _editLink;
	Fn<void()> _apply;

};

class NotReadyBox : public Ui::BoxContent {
public:
	NotReadyBox(
		QWidget*,
		const MTPDlangPackLanguage &data);

protected:
	void prepare() override;

private:
	QString _name;
	QString _editLink;

};

ConfirmSwitchBox::ConfirmSwitchBox(
	QWidget*,
	const MTPDlangPackLanguage &data,
	Fn<void()> apply)
: _name(qs(data.vnative_name()))
, _percent(data.vtranslated_count().v * 100 / data.vstrings_count().v)
, _official(data.is_official())
, _editLink(qs(data.vtranslations_url()))
, _apply(std::move(apply)) {
}

void ConfirmSwitchBox::prepare() {
	setTitle(tr::lng_language_switch_title());

	auto text = (_official
		? tr::lng_language_switch_about_official
		: tr::lng_language_switch_about_unofficial)(
			lt_lang_name,
			rpl::single(tr::bold(_name)),
			lt_percent,
			rpl::single(tr::bold(QString::number(_percent))),
			lt_link,
			tr::lng_language_switch_link(tr::url(_editLink)),
			tr::marked);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_language_switch_apply(), [=] {
		const auto apply = _apply;
		closeBox();
		apply();
	});
	addButton(tr::lng_cancel(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWideWidth);
	content->heightValue(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWideWidth, height);
	}, lifetime());
}

NotReadyBox::NotReadyBox(
	QWidget*,
	const MTPDlangPackLanguage &data)
: _name(qs(data.vnative_name()))
, _editLink(qs(data.vtranslations_url())) {
}

void NotReadyBox::prepare() {
	setTitle(tr::lng_language_not_ready_title());

	auto text = tr::lng_language_not_ready_about(
		lt_lang_name,
		rpl::single(tr::marked(_name)),
		lt_link,
		tr::lng_language_not_ready_link(tr::url(_editLink)),
		tr::marked);
	const auto content = Ui::CreateChild<Ui::PaddingWrap<Ui::FlatLabel>>(
		this,
		object_ptr<Ui::FlatLabel>(
			this,
			std::move(text),
			st::boxLabel),
		QMargins{ st::boxPadding.left(), 0, st::boxPadding.right(), 0 });
	content->entity()->setLinksTrusted();

	addButton(tr::lng_box_ok(), [=] { closeBox(); });

	content->resizeToWidth(st::boxWidth);
	content->heightValue(
	) | rpl::on_next([=](int height) {
		setDimensions(st::boxWidth, height);
	}, lifetime());
}

} // namespace

Language ParseLanguage(const MTPLangPackLanguage &data) {
	return data.match([](const MTPDlangPackLanguage &data) {
		return Language{
			qs(data.vlang_code()),
			qs(data.vplural_code()),
			qs(data.vbase_lang_code().value_or_empty()),
			qs(data.vname()),
			qs(data.vnative_name())
		};
	});
}

namespace {

[[nodiscard]] Main::AccountBackend AccountBackendOf(Main::Account *account) {
	return account
		? account->backend()
		: Main::AccountBackend::Unspecified;
}

} // namespace

CloudManager::CloudManager(Instance &langpack)
: _langpack(langpack) {
	Core::App().domain().activeValue(
	) | rpl::on_next([=](Main::Account *account) {
		_activeAccount = base::make_weak(account);
		refreshBackingAccount();
	}, _lifetime);
	Core::App().domain().accountsChanges(
	) | rpl::on_next([=] {
		refreshBackingAccount();
	}, _lifetime);
	refreshBackingAccount();
}

Main::AccountBackend CloudManager::backingBackend() const {
	return AccountBackendOf(_backingAccount.get());
}

bool CloudManager::allowsCloudRequest(
		Main::Account *initiating,
		LanguageRequestOrigin origin) const {
	return PlanLanguageCloudRequest({
		.origin = origin,
		.initiating = AccountBackendOf(initiating),
		.backing = backingBackend(),
	}) == LanguageCloudRequestPolicy::Allow;
}

void CloudManager::failPendingValueRequests() {
	auto pending = base::take(_getValueForLangRequests);
	for (auto &[requestKey, request] : pending) {
		if (const auto onstack = request.callback) {
			onstack(QString());
		}
	}
}

void CloudManager::markInFlightForResend() {
	const auto pend = [](mtpRequestId &id) {
		if (id > 0) {
			id = -1;
		}
	};
	pend(_langPackRequestId);
	pend(_langPackBaseRequestId);
	pend(_languagesRequestId);
	pend(_switchingToLanguageRequest);
	pend(_getKeysForSwitchRequestId);
	for (auto &[requestKey, request] : _getValueForLangRequests) {
		if (request.requestId > 0) {
			request.requestId = -1;
		}
	}
}

void CloudManager::bindBackingAccount(Main::Account *account) {
	const auto current = _backingAccount.get();
	if (current && current == account) {
		return;
	}
	if (!current && !account && !_api) {
		_backingAccount = nullptr;
		return;
	}
	++_backingGeneration;
	_backingLifetime.destroy();
	_api.reset();
	markInFlightForResend();
	_backingAccount = base::make_weak(account);
	if (!account) {
		failPendingValueRequests();
		return;
	}
	account->mtpMainSessionValue(
	) | rpl::on_next([=](not_null<MTP::Instance*> instance) {
		if (_backingAccount.get() != account) {
			return;
		}
		++_backingGeneration;
		markInFlightForResend();
		_api.emplace(instance);
		resendRequests();
	}, _backingLifetime);
}

void CloudManager::refreshBackingAccount() {
	const auto active = _activeAccount.get();
	auto existing = _backingAccount.get();
	if (!Delta::UsesTelegramBackend(existing)) {
		existing = nullptr;
	}
	Main::Account *firstTelegram = nullptr;
	auto existingLive = false;
	for (const auto &entry : Core::App().domain().accounts()) {
		const auto account = entry.account.get();
		if (!Delta::UsesTelegramBackend(account)) {
			continue;
		}
		if (!firstTelegram) {
			firstTelegram = account;
		}
		if (existing == account) {
			existingLive = true;
		}
	}
	if (!existingLive) {
		existing = nullptr;
	}
	Main::Account *chosen = nullptr;
	switch (PlanLanguageBacking({
		.active = AccountBackendOf(active),
		.existingBackingIsLiveTelegram = (existing != nullptr),
		.telegramAccountAvailable = (firstTelegram != nullptr),
	})) {
	case LanguageBackingPick::ActiveTelegram:
		chosen = Delta::UsesTelegramBackend(active) ? active : firstTelegram;
		break;
	case LanguageBackingPick::KeepExistingTelegram:
		chosen = existing;
		break;
	case LanguageBackingPick::FirstTelegram:
		chosen = firstTelegram;
		break;
	case LanguageBackingPick::None:
		chosen = nullptr;
		break;
	}
	bindBackingAccount(chosen);
}

Pack CloudManager::packTypeFromId(const QString &id) const {
	if (id == LanguageIdOrDefault(_langpack.id())) {
		return Pack::Current;
	} else if (id == _langpack.baseId()) {
		return Pack::Base;
	}
	return Pack::None;
}

rpl::producer<> CloudManager::languageListChanged() const {
	return _languageListChanged.events();
}

rpl::producer<> CloudManager::firstLanguageSuggestion() const {
	return _firstLanguageSuggestion.events();
}

void CloudManager::requestLangPackDifference(const QString &langId) {
	Expects(!langId.isEmpty());

	if (langId == LanguageIdOrDefault(_langpack.id())) {
		requestLangPackDifference(Pack::Current);
	} else {
		requestLangPackDifference(Pack::Base);
	}
}

mtpRequestId &CloudManager::packRequestId(Pack pack) {
	return (pack != Pack::Base)
		? _langPackRequestId
		: _langPackBaseRequestId;
}

mtpRequestId CloudManager::packRequestId(Pack pack) const {
	return (pack != Pack::Base)
		? _langPackRequestId
		: _langPackBaseRequestId;
}

void CloudManager::requestLangPackDifference(Pack pack) {
	if (!allowsCloudRequest(nullptr, LanguageRequestOrigin::System)
		|| !_api) {
		return;
	}
	_api->request(base::take(packRequestId(pack))).cancel();
	if (_langpack.isCustom()) {
		return;
	}

	const auto version = _langpack.version(pack);
	const auto code = _langpack.cloudLangCode(pack);
	if (code.isEmpty()) {
		return;
	}
	const auto generation = _backingGeneration;
	if (version > 0) {
		packRequestId(pack) = _api->request(MTPlangpack_GetDifference(
			MTP_string(CloudLangPackName()),
			MTP_string(code),
			MTP_int(version)
		)).done([=](const MTPLangPackDifference &result) {
			if (generation != _backingGeneration) {
				return;
			}
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=] {
			if (generation != _backingGeneration) {
				return;
			}
			packRequestId(pack) = 0;
		}).send();
	} else {
		packRequestId(pack) = _api->request(MTPlangpack_GetLangPack(
			MTP_string(CloudLangPackName()),
			MTP_string(code)
		)).done([=](const MTPLangPackDifference &result) {
			if (generation != _backingGeneration) {
				return;
			}
			packRequestId(pack) = 0;
			applyLangPackDifference(result);
		}).fail([=] {
			if (generation != _backingGeneration) {
				return;
			}
			packRequestId(pack) = 0;
		}).send();
	}
}

void CloudManager::setSuggestedLanguage(const QString &langCode) {
	if (Lang::LanguageIdOrDefault(langCode) != Lang::DefaultLanguageId()) {
		_suggestedLanguage = langCode;
	} else {
		_suggestedLanguage = QString();
	}

	if (!_languageWasSuggested) {
		_languageWasSuggested = true;
		_firstLanguageSuggestion.fire({});

		if (Core::App().offerLegacyLangPackSwitch()
			&& _langpack.id().isEmpty()
			&& !_suggestedLanguage.isEmpty()) {
			_offerSwitchToId = _suggestedLanguage;
			offerSwitchLangPack();
		}
	}
}

void CloudManager::setCurrentVersions(int version, int baseVersion) {
	const auto check = [&](Pack pack, int version) {
		if (version > _langpack.version(pack) && !packRequestId(pack)) {
			requestLangPackDifference(pack);
		}
	};
	check(Pack::Current, version);
	check(Pack::Base, baseVersion);
}

void CloudManager::applyLangPackDifference(
		const MTPLangPackDifference &difference) {
	Expects(difference.type() == mtpc_langPackDifference);

	if (_langpack.isCustom()) {
		return;
	}

	const auto &langpack = difference.c_langPackDifference();
	const auto langpackId = qs(langpack.vlang_code());
	const auto pack = packTypeFromId(langpackId);
	if (pack != Pack::None) {
		applyLangPackData(pack, langpack);
		if (_restartAfterSwitch) {
			restartAfterSwitch();
		}
	} else {
		LOG(("Lang Warning: "
			"Ignoring update for '%1' because our language is '%2'").arg(
			langpackId,
			_langpack.id()));
	}
}

void CloudManager::requestLanguageList(Main::Account *initiating) {
	if (_languages.empty()) {
		_languages.push_back(DefaultLanguage());
		_languageListChanged.fire({});
	}
	const auto origin = initiating
		? LanguageRequestOrigin::ExplicitAccount
		: LanguageRequestOrigin::System;
	if (!allowsCloudRequest(initiating, origin)) {
		if (AccountBackendOf(initiating) == Main::AccountBackend::Telegram) {
			_languagesRequestId = -1;
		}
		return;
	}
	if (!_api) {
		_languagesRequestId = -1;
		return;
	}
	const auto generation = _backingGeneration;
	_api->request(base::take(_languagesRequestId)).cancel();
	_languagesRequestId = _api->request(MTPlangpack_GetLanguages(
		MTP_string(CloudLangPackName())
	)).done([=](const MTPVector<MTPLangPackLanguage> &result) {
		if (generation != _backingGeneration) {
			return;
		}
		auto languages = Languages();
		for (const auto &language : result.v) {
			languages.push_back(ParseLanguage(language));
		}
		if (_languages != languages) {
			_languages = languages;
			_languageListChanged.fire({});
		}
		_languagesRequestId = 0;
	}).fail([=] {
		if (generation != _backingGeneration) {
			return;
		}
		_languagesRequestId = 0;
		if (_languages.empty()) {
			_languages.push_back(DefaultLanguage());
			_languageListChanged.fire({});
		}
	}).send();
}

void CloudManager::offerSwitchLangPack() {
	Expects(!_offerSwitchToId.isEmpty());
	Expects(_offerSwitchToId != DefaultLanguageId());

	if (!showOfferSwitchBox()) {
		languageListChanged(
		) | rpl::on_next([=] {
			showOfferSwitchBox();
		}, _lifetime);
		requestLanguageList();
	}
}

Language CloudManager::findOfferedLanguage() const {
	for (const auto &language : _languages) {
		if (language.id == _offerSwitchToId) {
			return language;
		}
	}
	return {};
}

bool CloudManager::showOfferSwitchBox() {
	const auto language = findOfferedLanguage();
	if (language.id.isEmpty()) {
		return false;
	}

	const auto confirm = [=] {
		Ui::hideLayer();
		if (_offerSwitchToId.isEmpty()) {
			return;
		}
		performSwitchAndRestart(language);
	};
	const auto cancel = [=] {
		Ui::hideLayer();
		changeIdAndReInitConnection(DefaultLanguage());
		Local::writeLangPack();
	};
	Ui::show(
		Ui::MakeConfirmBox({
			.text = QString("Do you want to switch your language to ")
			+ language.nativeName
			+ QString("? You can always change your language in Settings."),
			.confirmed = confirm,
			.cancelled = cancel,
			.confirmText = QString("Change"),
		}),
		Ui::LayerOption::KeepOther);
	return true;
}

void CloudManager::applyLangPackData(
		Pack pack,
		const MTPDlangPackDifference &data) {
	if (_langpack.version(pack) < data.vfrom_version().v) {
		requestLangPackDifference(pack);
	} else if (!data.vstrings().v.isEmpty()) {
		_langpack.applyDifference(pack, data);
		Local::writeLangPack();
	} else if (_restartAfterSwitch) {
		Local::writeLangPack();
	} else {
		LOG(("Lang Info: Up to date."));
	}
}

bool CloudManager::canApplyWithoutRestart(const QString &id) const {
	if (id == u"#TEST_X"_q || id == u"#TEST_0"_q) {
		return true;
	}
	return Core::App().canApplyLangPackWithoutRestart();
}

void CloudManager::resetToDefault() {
	performSwitch(DefaultLanguage());
}

void CloudManager::switchToLanguage(
		const QString &id,
		Main::Account *initiating) {
	requestLanguageAndSwitch(id, false, initiating);
}

void CloudManager::switchWithWarning(
		const QString &id,
		Main::Account *initiating) {
	requestLanguageAndSwitch(id, true, initiating);
}

void CloudManager::requestLanguageAndSwitch(
		const QString &id,
		bool warning,
		Main::Account *initiating) {
	Expects(!id.isEmpty());

	if (LanguageIdOrDefault(_langpack.id()) == id) {
		Ui::show(Ui::MakeInformBox(tr::lng_language_already()));
		return;
	} else if (id == u"#custom"_q) {
		performSwitchToCustom(initiating);
		return;
	}
	if (initiating
		&& PlanLanguagePicker(initiating->backend()).localCatalog) {
		if (LanguageIdOrDefault(id) == DefaultLanguageId()) {
			switchToLanguage(DefaultLanguage(), initiating);
			return;
		}
		Ui::show(Ui::MakeInformBox(
			u"Cloud language packs are unavailable. Keep the current language, switch to English, or import a custom .strings file."_q));
		return;
	}

	_switchingToLanguageId = id;
	_switchingToLanguageWarning = warning;
	_switchingToLanguageInitiating = initiating;
	sendSwitchingToLanguageRequest();
}

void CloudManager::sendSwitchingToLanguageRequest() {
	if (!allowsCloudRequest(
			_switchingToLanguageInitiating.get(),
			LanguageRequestOrigin::ExplicitAccount)) {
		_switchingToLanguageRequest = 0;
		return;
	}
	if (!_api) {
		_switchingToLanguageRequest = -1;
		return;
	}
	const auto generation = _backingGeneration;
	_api->request(_switchingToLanguageRequest).cancel();
	_switchingToLanguageRequest = _api->request(MTPlangpack_GetLanguage(
		MTP_string(Lang::CloudLangPackName()),
		MTP_string(_switchingToLanguageId)
	)).done([=](const MTPLangPackLanguage &result) {
		if (generation != _backingGeneration) {
			return;
		}
		_switchingToLanguageRequest = 0;
		const auto language = Lang::ParseLanguage(result);
		const auto finalize = [=] {
			if (canApplyWithoutRestart(language.id)) {
				performSwitchAndAddToRecent(language);
			} else {
				performSwitchAndRestart(language);
			}
		};
		if (!_switchingToLanguageWarning) {
			finalize();
			return;
		}
		result.match([=](const MTPDlangPackLanguage &data) {
			if (data.vstrings_count().v > 0) {
				Ui::show(Box<ConfirmSwitchBox>(data, finalize));
			} else {
				Ui::show(Box<NotReadyBox>(data));
			}
		});
	}).fail([=](const MTP::Error &error) {
		if (generation != _backingGeneration) {
			return;
		}
		_switchingToLanguageRequest = 0;
		if (error.type() == "LANG_CODE_NOT_SUPPORTED") {
			Ui::show(Ui::MakeInformBox(tr::lng_language_not_found()));
		}
	}).send();
}

void CloudManager::switchToLanguage(
		const Language &data,
		Main::Account *initiating) {
	if (_langpack.id() == data.id && data.id != u"#custom"_q) {
		return;
	}
	if (data.id == u"#custom"_q) {
		performSwitchToCustom(initiating);
		return;
	}
	if (initiating
		&& PlanLanguagePicker(initiating->backend()).localCatalog) {
		if (LanguageIdOrDefault(data.id) == DefaultLanguageId()) {
			Local::pushRecentLanguage(DefaultLanguage());
			_restartAfterSwitch = false;
			_langpack.switchToId(DefaultLanguage());
			Local::writeLangPack();
			if (!canApplyWithoutRestart(DefaultLanguage().id)) {
				Core::Restart();
			}
			return;
		}
		Ui::show(Ui::MakeInformBox(
			u"Cloud language packs are unavailable. Keep the current language, switch to English, or import a custom .strings file."_q));
		return;
	}
	if (!_api) {
		return;
	}

	_api->request(base::take(_getKeysForSwitchRequestId)).cancel();
	if (canApplyWithoutRestart(data.id)) {
		performSwitchAndAddToRecent(data);
	} else {
		const auto generation = _backingGeneration;
		QVector<MTPstring> keys;
		keys.reserve(3);
		keys.push_back(MTP_string("lng_sure_save_language"));
		_getKeysForSwitchRequestId = _api->request(MTPlangpack_GetStrings(
			MTP_string(Lang::CloudLangPackName()),
			MTP_string(data.id),
			MTP_vector<MTPstring>(std::move(keys))
		)).done([=](const MTPVector<MTPLangPackString> &result) {
			if (generation != _backingGeneration) {
				return;
			}
			_getKeysForSwitchRequestId = 0;
			const auto values = Instance::ParseStrings(result);
			const auto getValue = [&](ushort key) {
				auto it = values.find(key);
				return (it == values.cend())
					? GetOriginalValue(key)
					: it->second;
			};
			const auto text = tr::lng_sure_save_language(tr::now)
				+ "\n\n"
				+ getValue(tr::lng_sure_save_language.base);
			Ui::show(
				Ui::MakeConfirmBox({
					.text = text,
					.confirmed = [=] { performSwitchAndRestart(data); },
					.confirmText = tr::lng_box_ok(),
				}),
				Ui::LayerOption::KeepOther);
		}).fail([=] {
			if (generation != _backingGeneration) {
				return;
			}
			_getKeysForSwitchRequestId = 0;
		}).send();
	}
}

void CloudManager::performSwitchToCustom(Main::Account *initiating) {
	auto filter = u"Language files (*.strings)"_q;
	auto title = u"Choose language .strings file"_q;
	const auto confirm = PlanLanguageCustomConfirm(
		AccountBackendOf(initiating));
	FileDialog::GetOpenPath(Core::App().getFileDialogParent(), title, filter, [=, weak = base::make_weak(this)](const FileDialog::OpenResult &result) {
		if (!weak || result.paths.isEmpty()) {
			return;
		}

		const auto filePath = result.paths.front();
		auto loader = Lang::FileParser(
			filePath,
			{
				tr::lng_sure_save_language.base,
				tr::lng_delta_sure_save_language.base,
			});
		if (loader.errors().isEmpty()) {
			if (_api) {
				_api->request(
					base::take(_switchingToLanguageRequest)
				).cancel();
			}
			if (canApplyWithoutRestart(u"#custom"_q)) {
				_langpack.switchToCustomFile(filePath);
			} else {
				const auto values = loader.found();
				const auto getValue = [&](ushort key) {
					const auto it = values.find(key);
					return (it == values.cend())
						? GetOriginalValue(key)
						: it.value();
				};
				const auto current = (confirm
						== LanguageCustomConfirmCopy::CoreRestart)
					? tr::lng_delta_sure_save_language(tr::now)
					: tr::lng_sure_save_language(tr::now);
				const auto target = getValue(
					(confirm == LanguageCustomConfirmCopy::CoreRestart)
						? tr::lng_delta_sure_save_language.base
						: tr::lng_sure_save_language.base);
				const auto text = CombineLanguageRestartCopy(
					confirm,
					current,
					target);
				const auto change = [=] {
					_langpack.switchToCustomFile(filePath);
					Core::Restart();
				};
				Ui::show(
					Ui::MakeConfirmBox({
						.text = text,
						.confirmed = change,
						.confirmText = tr::lng_box_ok(),
					}),
					Ui::LayerOption::KeepOther);
			}
		} else {
			Ui::show(
				Ui::MakeInformBox(
					"Custom lang failed :(\n\nError: " + loader.errors()),
				Ui::LayerOption::KeepOther);
		}
	});
}

void CloudManager::switchToTestLanguage(Main::Account *initiating) {
	const auto testLanguageId = (_langpack.id() == u"#TEST_X"_q)
		? u"#TEST_0"_q
		: u"#TEST_X"_q;
	if (!initiating
		|| PlanLanguagePicker(initiating->backend()).localCatalog) {
		_restartAfterSwitch = false;
		switchLangPackId({ testLanguageId });
		return;
	}
	performSwitch({ testLanguageId });
}

void CloudManager::performSwitch(const Language &data) {
	_restartAfterSwitch = false;
	switchLangPackId(data);
	requestLangPackDifference(Pack::Current);
	requestLangPackDifference(Pack::Base);
}

void CloudManager::performSwitchAndAddToRecent(const Language &data) {
	Local::pushRecentLanguage(data);
	performSwitch(data);
}

void CloudManager::performSwitchAndRestart(const Language &data) {
	performSwitchAndAddToRecent(data);
	restartAfterSwitch();
}

void CloudManager::restartAfterSwitch() {
	if (_langPackRequestId || _langPackBaseRequestId) {
		_restartAfterSwitch = true;
	} else {
		Core::Restart();
	}
}

void CloudManager::switchLangPackId(const Language &data) {
	const auto currentId = _langpack.id();
	const auto currentBaseId = _langpack.baseId();
	const auto notChanged = (currentId == data.id
		&& currentBaseId == data.baseId)
		|| (currentId.isEmpty()
			&& currentBaseId.isEmpty()
			&& data.id == DefaultLanguageId());
	if (!notChanged) {
		changeIdAndReInitConnection(data);
	}
}

void CloudManager::changeIdAndReInitConnection(const Language &data) {
	_langpack.switchToId(data);
	if (_api) {
		const auto mtproto = &_api->instance();
		mtproto->reInitConnection(mtproto->mainDcId());
	}
}

void CloudManager::getValueForLang(
		const QString &key,
		const QString &langId,
		Fn<void(const QString &)> callback) {
	if (!allowsCloudRequest(nullptr, LanguageRequestOrigin::System)) {
		callback(QString());
		return;
	}
	const auto requestKey = langId + ':' + key;
	auto &request = _getValueForLangRequests[requestKey];
	request.callback = std::move(callback);
	if (!_api) {
		request.requestId = -1;
		return;
	}
	const auto generation = _backingGeneration;
	_api->request(base::take(request.requestId)).cancel();
	request.requestId = _api->request(
		MTPlangpack_GetStrings(
			MTP_string(Lang::CloudLangPackName()),
			MTP_string(langId),
			MTP_vector<MTPstring>(1, MTP_string(key))
	)).done([=](const MTPVector<MTPLangPackString> &result) {
		if (generation != _backingGeneration) {
			return;
		}
		const auto it = _getValueForLangRequests.find(requestKey);
		if (it != _getValueForLangRequests.end()) {
			const auto onstack = it->second.callback;
			_getValueForLangRequests.erase(it);
			const auto values = Instance::ParseStrings(result);
			for (const auto &[k, v] : values) {
				onstack(v);
				return;
			}
			onstack(QString());
		}
	}).fail([=] {
		if (generation != _backingGeneration) {
			return;
		}
		const auto it = _getValueForLangRequests.find(requestKey);
		if (it != _getValueForLangRequests.end()) {
			const auto onstack = it->second.callback;
			_getValueForLangRequests.erase(it);
			onstack(QString());
		}
	}).send();
}

void CloudManager::resendPendingValueRequests() {
	if (!_api) {
		return;
	}
	for (const auto &[requestKey, request] : _getValueForLangRequests) {
		if (request.requestId == -1) {
			const auto colonPos = requestKey.indexOf(':');
			if (colonPos > 0) {
				getValueForLang(
					requestKey.mid(colonPos + 1),
					requestKey.left(colonPos),
					request.callback);
			}
		}
	}
}

void CloudManager::resendRequests() {
	if (packRequestId(Pack::Base)) {
		requestLangPackDifference(Pack::Base);
	}
	if (packRequestId(Pack::Current)) {
		requestLangPackDifference(Pack::Current);
	}
	if (_languagesRequestId) {
		requestLanguageList();
	}
	if (_switchingToLanguageRequest) {
		sendSwitchingToLanguageRequest();
	}
	resendPendingValueRequests();
}

CloudManager &CurrentCloudManager() {
	auto result = Core::App().langCloudManager();
	Assert(result != nullptr);
	return *result;
}

} // namespace Lang
