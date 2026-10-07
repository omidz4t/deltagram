#define GI_INLINE 1
#include <gio/gio.hpp>

#include <iostream>

namespace GLib = gi::repository::GLib;
namespace Gio = gi::repository::Gio;

const std::string localhost("127.0.0.1");
static GLib::MainLoop loop;

// many calls here support GError
// so typically will throw here instead
// (unless GError output is explicitly requested in call signature)

// NOTE abundancy of gi::expect not generally needed;
// only needed when using --dl and --expected

static bool
receive(Gio::Socket s, GLib::IOCondition /*cond*/)
{
  guint8 buffer[1024] = {
      0,
  };
  Gio::SocketAddress a;
  int count = gi::expect(s.receive_from(&a, buffer, sizeof(buffer)));
  if (count > 0) {
    // let's see where it came from
    std::string origin("someone");
    auto ia = gi::object_cast<Gio::InetSocketAddress>(a);
    if (ia) {
      origin = gi::expect(gi::expect(ia.get_address()).to_string());
      origin += ":";
      origin += std::to_string(gi::expect(ia.get_port()));
    }
    std::cout << origin << " said " << (char *)buffer << std::endl;
    // quit when idle
    GLib::idle_add([]() {
      loop.quit();
      return GLib::SOURCE_REMOVE_;
    });
  }
  return true;
}

Gio::Socket
open(bool listen)
{
  auto socket = gi::expect(Gio::Socket::new_(Gio::SocketFamily::IPV4_,
      Gio::SocketType::DATAGRAM_, Gio::SocketProtocol::DEFAULT_));

  auto address =
      gi::expect(Gio::InetSocketAddress::new_from_string(localhost, 0));
  socket.bind(address, false);
  socket.set_blocking(false);

  if (listen) {
    // runtime introspection has a hard time here,
    // but with a bit of extra information, we can keep going
#if defined(GI_CALL_ARGS) && CALL_ARGS <= 1
    // so we should have this signature for a function with 1 non-required
    GLib::Source source =
        gi::expect(socket.create_source({.condition = GLib::IOCondition::IN_}));
#else
    GLib::Source source =
        gi::expect(socket.create_source(GLib::IOCondition::IN_, nullptr));
#endif
    source.set_callback<Gio::SocketSourceFunc>(receive);
    source.attach();
  }

  return socket;
}

static void
die(const std::string &why)
{
  std::cerr << why << std::endl;
  exit(2);
}

int
main(int argc, char **argv)
{
  if (argc < 2)
    die("missing argument");

  std::string msg = argv[1];
  std::cout << "will send message " << msg << std::endl;

  auto recv = open(true);
  auto local = gi::expect(recv.get_local_address());
  auto send = open(false);
  send.send_to(local, (guint8 *)msg.data(), msg.size(), nullptr);

  loop = gi::expect(GLib::MainLoop::new_());
  loop.run();
}
