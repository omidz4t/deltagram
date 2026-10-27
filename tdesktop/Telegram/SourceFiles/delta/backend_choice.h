/*
This file is part of Delta Tel, a Telegram Desktop based Delta Chat client.
*/
#pragma once

#include "base/object_ptr.h"
#include "main/account_backend.h"
#include "main/backend_startup.h"
#include "ui/rp_widget.h"

namespace Ui {
class FlatLabel;
class RoundButton;
} // namespace Ui

namespace Delta {

class BackendChoice final : public Ui::RpWidget {
public:
	explicit BackendChoice(QWidget *parent);
	~BackendChoice();

	void setInnerFocus();
	void showError(const QString &text);
	[[nodiscard]] rpl::producer<Main::AccountBackend> chosen() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

private:
	void updateControlsGeometry();

	object_ptr<Ui::FlatLabel> _title;
	object_ptr<Ui::FlatLabel> _description;
	object_ptr<Ui::RoundButton> _core;
	object_ptr<Ui::RoundButton> _telegram;
	object_ptr<Ui::FlatLabel> _error;
	rpl::event_stream<Main::AccountBackend> _chosen;

};

[[nodiscard]] QString BackendChoiceErrorText(
	Main::AccountBackendBindResult result);
[[nodiscard]] QString CoreSessionStartErrorText(
	Main::CoreSessionStartResult result);

} // namespace Delta
