#pragma once

#include <gtk/gtk.h>
#if !GTK_CHECK_VERSION(3, 99, 0)
#include <gtk/gtk-a11y.h>
#endif

// GtkSnapshot is alias for GdkSnapshot and its GIR does not specify c:type
// use a fairly opaque dummy type here to make things work out
// (since one ctype can not map to 2 cpp types (Gdk::Snapshot and Gtk::Snapshot,
// and since one pointer is much like another)
struct GI_PATCH_GtkSnapshot;
