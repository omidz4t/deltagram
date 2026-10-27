/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/backend_choice.h"

#include "lang/lang_keys.h"
#include "rpl/combine.h"
#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "styles/style_intro.h"
#include "styles/style_window.h"

#include <algorithm>

namespace Delta {

BackendChoice::BackendChoice(QWidget *parent)
: RpWidget(parent)
, _title(
	this,
	tr::lng_delta_account_choice_title(),
	st::deltaAccountChoiceTitle)
, _description(
	this,
	tr::lng_delta_account_choice_about(),
	st::deltaAccountChoiceDescription)
, _core(
	this,
	tr::lng_delta_account_choice_core(),
	st::introNextButton)
, _telegram(
	this,
	tr::lng_delta_account_choice_telegram(),
	st::introNextButton)
, _error(this, QString(), st::introErrorCentered) {
	_error->hide();
	rpl::combine(
		_title->heightValue(),
		_description->heightValue(),
		_error->heightValue()
	) | rpl::on_next([=](int, int, int) {
		updateControlsGeometry();
	}, lifetime());
	_core->setClickedCallback([=] {
		_chosen.fire_copy(Main::AccountBackend::Core);
	});
	_telegram->setClickedCallback([=] {
		_chosen.fire_copy(Main::AccountBackend::Telegram);
	});
}

BackendChoice::~BackendChoice() = default;

void BackendChoice::setInnerFocus() {
	_core->setFocus(Qt::OtherFocusReason);
}

void BackendChoice::showError(const QString &text) {
	_error->setText(text);
	_error->setVisible(!text.isEmpty());
	updateControlsGeometry();
}

rpl::producer<Main::AccountBackend> BackendChoice::chosen() const {
	return _chosen.events();
}

void BackendChoice::paintEvent(QPaintEvent *e) {
	QPainter p(this);
	p.fillRect(e->rect(), st::windowBg);
}

void BackendChoice::resizeEvent(QResizeEvent *e) {
	updateControlsGeometry();
}

void BackendChoice::updateControlsGeometry() {
	const auto columnWidth = st::introNextButton.width;
	_title->resizeToWidth(columnWidth);
	_description->resizeToWidth(columnWidth);
	_error->resizeToWidth(width());
	const auto descriptionTop = std::max(
		int(st::introCoverDescriptionTop),
		st::introCoverTitleTop + _title->height() + st::introPhoneTop);
	const auto coreTop = std::max(
		int(st::introNextTop),
		descriptionTop + _description->height() + st::introLinkTop);
	const auto telegramTop = coreTop + _core->height() + st::introLinkTop;
	const auto errorTop = telegramTop + _telegram->height() + st::introLinkTop;
	const auto contentBottom = _error->isHidden()
		? telegramTop + _telegram->height()
		: errorTop + _error->height();
	const auto preferredTop = std::max(
		(height() - st::introHeight) / 2,
		int(st::introStepTopMin));
	// The donor's minimum top offset assumes one button. Fit both choices
	// and any error inside compact windows, including screen-reader mode.
	const auto top = std::min(
		preferredTop,
		height() - contentBottom - st::introLinkTop);
	const auto left = (width() - columnWidth) / 2;
	_title->moveToLeft(left, top + st::introCoverTitleTop);
	_description->moveToLeft(left, top + descriptionTop);
	_core->moveToLeft(left, top + coreTop);
	_telegram->moveToLeft(left, top + telegramTop);
	_error->moveToLeft(0, top + errorTop);
}

QString BackendChoiceErrorText(Main::AccountBackendBindResult result) {
	switch (result) {
	case Main::AccountBackendBindResult::RejectedPersistFailed:
		return tr::lng_delta_account_persist_failed(tr::now);
	case Main::AccountBackendBindResult::RejectedCoreWithKeys:
		return tr::lng_delta_account_core_keys_denied(tr::now);
	case Main::AccountBackendBindResult::RejectedChange:
	case Main::AccountBackendBindResult::RejectedClear:
		return tr::lng_delta_account_change_denied(tr::now);
	case Main::AccountBackendBindResult::RejectedLiveSession:
		return tr::lng_delta_account_session_denied(tr::now);
	default:
		return tr::lng_delta_account_start_failed(tr::now);
	}
}

QString CoreSessionStartErrorText(Main::CoreSessionStartResult result) {
	switch (result) {
	case Main::CoreSessionStartResult::RejectedNotCore:
		return tr::lng_delta_account_core_required(tr::now);
	case Main::CoreSessionStartResult::RejectedOtherBridge:
		return tr::lng_delta_account_core_busy(tr::now);
	default:
		return tr::lng_delta_account_start_failed(tr::now);
	}
}

} // namespace Delta
