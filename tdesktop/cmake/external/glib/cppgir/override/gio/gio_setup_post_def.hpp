#pragma once

#define G_SETTINGS_ENABLE_BACKEND 1
#include <gio/gsettingsbackend.h>

// now unconditionally part of <gio/gio.h>
// though (still) missing in some distro's release
#include <gio/gunixconnection.h>
#include <gio/gunixcredentialsmessage.h>
#include <gio/gunixsocketaddress.h>
