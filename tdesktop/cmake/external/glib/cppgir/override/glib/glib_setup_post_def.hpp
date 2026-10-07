#ifndef _GI_GLIB_SETUP_DEF_HPP_
#define _GI_GLIB_SETUP_DEF_HPP_

// missing in GIR
// recent version re'define functions to system functions,
// but may not include suitable headers, so force fallback to wrapping
// in recent version, it may also be included by glib-unix below
// so do so before including that one
#ifdef G_OS_UNIX
#define G_STDIO_WRAP_ON_UNIX 1
#endif
#include <glib/gstdio.h>

#ifdef G_OS_UNIX
// missing in GIR
#include <glib-unix.h>
#endif

#endif // _GI_GLIB_SETUP_DEF_HPP_
