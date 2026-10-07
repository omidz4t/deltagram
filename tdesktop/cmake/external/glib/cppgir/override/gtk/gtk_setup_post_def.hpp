#pragma once

// unfortunately gtk includes gtkx includes gdkx includes X11/X*.h
// where the latter brings in a whole slew of (evidently non-namespaced) define
// so try to undefine some of the more nasty ones that might likely conflict
#ifdef DestroyNotify
#undef DestroyNotify
#endif
#ifdef Status
#undef Status
#endif

// still part of GIR, but now not part of standard include (or pkg .pc)
#if GTK_CHECK_VERSION(4, 0, 0)
#ifdef G_OS_UNIX
// needs include path specified in gtk4-unix-print.pc
#include <gtk/gtkunixprint.h>
#endif
#endif
