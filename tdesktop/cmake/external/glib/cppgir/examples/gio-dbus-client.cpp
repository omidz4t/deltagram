#define GI_INLINE 1
#include <gio/gio.hpp>

#include <iostream>

namespace GObject_ = gi::repository::GObject;
namespace GLib = gi::repository::GLib;
namespace Gio = gi::repository::Gio;

static GLib::MainLoop loop;

// many calls here support GError
// so typically will throw here instead
// (unless GError output is explicitly requested in call signature)

// NOTE abundancy of gi::expect not generally needed;
// only needed when using --dl and --expected

static void
on_reply(GObject_::Object ob, Gio::AsyncResult result)
{
  // if not caught here, it will be caught before returning to plain C
#if GI_CONFIG_EXCEPTIONS
  try {
#endif
    auto connection = gi::object_cast<Gio::DBusConnection>(ob);

    auto call_result = gi::expect(connection.call_finish(result));
    // get single array child
    auto names = gi::expect(call_result.get_child_value(0));
    int count = gi::expect(names.n_children());

    std::cout << count << " message bus names: " << std::endl;
    for (int i = 0; i < count; ++i)
      std::cout << gi::expect(
                       gi::expect(names.get_child_value(i)).get_string(nullptr))
                << std::endl;
#if GI_CONFIG_EXCEPTIONS
  } catch (const GLib::Error &error) {
    std::cerr << "error: '" << error.what() << "'." << std::endl;
  }
#endif

  // quit when idle
  GLib::idle_add([]() {
    loop.quit();
    return GLib::SOURCE_REMOVE_;
  });
}

int
main(int argc, char ** /*argv*/)
{
  auto bustype = argc <= 1 ? Gio::BusType::SESSION_ : Gio::BusType::SYSTEM_;
  auto connection = gi::expect(Gio::bus_get_sync(bustype));

  connection.call("org.freedesktop.DBus", "/org/freedesktop/DBus",
      "org.freedesktop.DBus", "ListNames", nullptr, nullptr,
      Gio::DBusCallFlags::NONE_, -1, nullptr, on_reply);

  loop = gi::expect(GLib::MainLoop::new_());
  loop.run();
}
