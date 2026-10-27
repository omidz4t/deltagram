#pragma once

#include "ui/rp_widget.h"

class HistoryItem;

namespace Window {
class SessionController;
} // namespace Window

namespace Delta {

void ShowFullMessage(
	not_null<Window::SessionController*> controller,
	not_null<HistoryItem*> item);

} // namespace Delta
