/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#include "delta/full_message_box.h"

#include "base/weak_ptr.h"
#include "core/click_handler_types.h"
#include "delta/account_scope.h"
#include "delta/delta_bridge.h"
#include "delta/full_message.h"
#include "delta/full_message_view.h"
#include "history/history.h"
#include "history/history_item.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/layers/generic_box.h"
#include "ui/qt_object_factory.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

#include <algorithm>

#include <QtCore/QUrl>

namespace Delta {
namespace {

void WatchFullMessageBox(
		not_null<Ui::GenericBox*> box,
		const std::shared_ptr<AccountScope> &scope,
		const std::shared_ptr<bool> &consumed) {
	AccountViewResets() | rpl::on_next([=](int) {
		if (scope) {
			scope->invalidate();
		}
		if (consumed) {
			*consumed = true;
		}
		box->closeBox();
	}, box->lifetime());
}

void OpenFullMessageLink(
		not_null<Window::SessionController*> controller,
		const QUrl &url) {
	const auto scheme = url.scheme().toLower();
	if (scheme == u"http"_q || scheme == u"https"_q) {
		auto ctx = ClickHandlerContext();
		ctx.sessionWindow = base::make_weak(controller);
		ctx.show = controller->uiShow();
		ctx.forceExternalUrlConfirmation = true;
		ctx.ignoreIv = true;
		HiddenUrlClickHandler::Open(
			url.toString(),
			QVariant::fromValue(ctx));
		return;
	}
	controller->showToast(tr::lng_full_message_link_denied(tr::now));
}

[[nodiscard]] QString FullMessageErrorText(FullMessageOutcome outcome) {
	switch (outcome) {
	case FullMessageOutcome::Unavailable:
		return tr::lng_full_message_unavailable(tr::now);
	case FullMessageOutcome::Unreadable:
		return tr::lng_full_message_unreadable(tr::now);
	case FullMessageOutcome::Empty:
		return tr::lng_full_message_empty(tr::now);
	case FullMessageOutcome::Malformed:
		return tr::lng_full_message_malformed(tr::now);
	case FullMessageOutcome::Ready:
		return {};
	}
	return tr::lng_full_message_unreadable(tr::now);
}

[[nodiscard]] object_ptr<Ui::GenericBox> MakeFullMessageBox(
		not_null<Window::SessionController*> controller,
		FullMessageResult result) {
	const auto weak = base::make_weak(controller);
	const auto title = result.title;
	return Box([=](not_null<Ui::GenericBox*> box) {
		if (title.isEmpty()) {
			box->setTitle(tr::lng_full_message_title());
		} else {
			box->setTitle(title);
		}
		box->setWidth(st::boxWideWidth);
		box->setMaxHeight(st::boxMaxListHeight);
		WatchFullMessageBox(box, nullptr, nullptr);
		const auto error = FullMessageErrorText(result.outcome);
		if (!error.isEmpty()) {
			const auto label = box->addRow(
				object_ptr<Ui::FlatLabel>(box, error, st::boxLabel));
			label->setSelectable(true);
			box->addButton(tr::lng_close(), [=] { box->closeBox(); });
			return;
		}
		auto wrapPtr = object_ptr<Ui::RpWidget>(box);
		const auto wrap = wrapPtr.data();
		const auto view = Ui::CreateChild<FullMessageView>(wrap);
		view->setFrameShape(QFrame::NoFrame);
		view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		view->setBody(result.html, result.fallback);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			view->usedFallback()
				? tr::lng_full_message_plain()
				: tr::lng_full_message_limited(),
			st::boxLabel));
		box->addRow(std::move(wrapPtr));
		view->setLinkHandler([=](const QUrl &url) {
			if (const auto strong = weak.get()) {
				OpenFullMessageLink(strong, url);
			}
		});
		const auto fit = [=](int width) {
			if (width <= 0) {
				return;
			}
			view->document()->setTextWidth(width);
			const auto height = std::max(
				int(view->document()->size().height()) + 1,
				st::normalFont->height);
			if (wrap->width() != width || wrap->height() != height) {
				wrap->resize(width, height);
			}
			view->setGeometry(0, 0, width, height);
		};
		wrap->widthValue() | rpl::on_next(fit, wrap->lifetime());
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	});
}

} // namespace

void ShowFullMessage(
		not_null<Window::SessionController*> controller,
		not_null<HistoryItem*> item) {
	const auto session = &controller->session();
	if (!OwnsSession(session)
		|| &item->history()->session() != session
		|| !HasFullMessage(item)) {
		controller->showToast(tr::lng_full_message_unavailable(tr::now));
		return;
	}
	const auto current = CaptureAccountGuard();
	if (current && !current()) {
		controller->showToast(tr::lng_full_message_unavailable(tr::now));
		return;
	}
	const auto messageId = int(item->id.bare);
	const auto generation = AccountGeneration();
	const auto scope = std::make_shared<AccountScope>();
	const auto consumed = std::make_shared<bool>(false);
	const auto loading = controller->show(Box([=](
			not_null<Ui::GenericBox*> box) {
		box->boxClosing() | rpl::on_next([=] {
			*consumed = true;
			scope->invalidate();
		}, box->lifetime());
		box->setTitle(tr::lng_full_message_title());
		box->addRow(object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_full_message_loading(),
			st::boxLabel));
		WatchFullMessageBox(box, scope, consumed);
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
	const auto abandoned = [=] {
		return *consumed || !loading;
	};
	LoadFullMessage(
		session,
		messageId,
		*scope,
		crl::guard(controller, [=](FullMessageResult result) {
			if (abandoned()) {
				return;
			}
			if (generation != AccountGeneration()
				|| !OwnsSession(&controller->session())
				|| (current && !current())) {
				*consumed = true;
				if (loading) {
					loading->closeBox();
				}
				return;
			}
			*consumed = true;
			if (loading) {
				loading->closeBox();
			}
			if (generation != AccountGeneration()
				|| !OwnsSession(&controller->session())
				|| (current && !current())) {
				return;
			}
			controller->show(MakeFullMessageBox(
				controller,
				std::move(result)));
		}));
}

} // namespace Delta
