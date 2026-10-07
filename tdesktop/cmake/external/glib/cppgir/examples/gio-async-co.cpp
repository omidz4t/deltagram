#define GI_INLINE 1
#include <gio/gio.hpp>

#include "co-async.hpp"

namespace GLib = gi::repository::GLib;
namespace GObject_ = gi::repository::GObject;
namespace Gio = gi::repository::Gio;

class async_result
{
  using p_type = co_promise<Gio::AsyncResult>;

  // protect against cancellation/destruction of async call stack
  struct state
  {
    p_type p_;
    Gio::Cancellable cancel_;
  };

  std::shared_ptr<state> s_ = std::make_shared<state>();

public:
  ~async_result()
  {
    auto c = cancellable(false);
    if (c)
      c.cancel();
  }

  operator Gio::AsyncReadyCallback()
  {
    // setup for new gio call
    s_->p_.reset();
    return [wp = std::weak_ptr(s_)](GObject_::Object, Gio::AsyncResult result) {
      if (auto sp = wp.lock(); sp) {
        // clear state for subsequent re-use if needed
        if (sp->cancel_ && sp->cancel_.is_cancelled())
          sp->cancel_ = nullptr;
        sp->p_.set_value(result);
        sp->p_.resume();
      }
    };
  }

  Gio::Cancellable cancellable(bool create = true)
  {
    auto &s = *s_;
    if (!s.cancel_ && create)
      s.cancel_ = Gio::Cancellable::new_();
    return s.cancel_;
  }

  static Gio::Cancellable timeout(
      const std::chrono::milliseconds &to, Gio::Cancellable cancel)
  {
    if (to.count() > 0 && cancel) {
      GLib::timeout_add_once(
          to.count(), [cancel]() mutable { cancel.cancel(); });
    }
    return cancel;
  }

  auto operator co_await() { return s_->p_.operator co_await(); }
};

task<void>
sleep_for(std::chrono::milliseconds to)
{
  co_promise<void> w;
  GLib::SourceFunc func = [&w]() {
    w.set_value();
    w.resume();
    return GLib::SOURCE_REMOVE_;
  };
  auto id = GLib::timeout_add(to.count(), func);
  // scope guard on w in case task stack gets dropped
  GLib::SourceScopedConnection guard = gi::make_connection(id, func);
  co_await w;
  co_return;
}

static task<void>
async_client(int port, int id, int &count)
{
  async_result w;

  auto dest = Gio::NetworkAddress::new_loopback(port);

  std::string sid = "client ";
  sid += std::to_string(id);

  // connect a client
  std::cout << sid << ": connect" << std::endl;
  auto client = Gio::SocketClient::new_();
  client.connect_async(dest, w);
  auto conn = gi::expect(client.connect_finish(co_await w));

  // say something
  auto os = conn.get_output_stream();
  std::cout << sid << ": send: " << sid << std::endl;
  os.write_all_async(
      (guint8 *)sid.data(), sid.size(), GLib::PRIORITY_DEFAULT_, w);
  os.write_all_finish(co_await w, (gsize *)nullptr);

  // now hear what the other side has to say
  std::cout << sid << ": receive" << std::endl;
  auto is = conn.get_input_stream();
  while (1) {
    guint8 data[1024];
    is.read_async(data, sizeof(data), GLib::PRIORITY_DEFAULT_, w);
    auto size = gi::expect(is.read_finish(co_await w));
    if (!size)
      break;
    std::string msg(data, data + size);
    std::cout << sid << ": got data: " << msg << std::endl;
  }

  std::cout << sid << ": closing down" << std::endl;
  --count;
}

static task<void>
async_handle_client(Gio::SocketConnection conn)
{
  async_result w;

  // say hello
  std::cout << "server: hello" << std::endl;
  auto os = conn.get_output_stream();
  std::string msg = "hello ";
  os.write_all_async(
      (guint8 *)msg.data(), msg.size(), GLib::PRIORITY_DEFAULT_, w);
  os.write_all_finish(co_await w, (gsize *)nullptr);

  // now echo what the other side has to say
  auto is = conn.get_input_stream();
  while (1) {
    guint8 data[1024];
    // give up if timeout
    GLib::Error error;
    std::cout << "server: reading ..." << std::endl;
    is.read_async(data, sizeof(data), GLib::PRIORITY_DEFAULT_,
        w.timeout(std::chrono::milliseconds(200), w.cancellable()), w);
    auto size = gi::expect(is.read_finish(co_await w, &error));
    if (error) {
      if (error.matches(G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        break;
      } else {
        gi::detail::try_throw(std::move(error));
      }
    }
    std::string msg(data, data + size);
    std::cout << "server: got data: " << msg << std::endl;
    os.write_all_async(data, size, GLib::PRIORITY_DEFAULT_, w);
    os.write_all_finish(co_await w, (gsize *)nullptr);
  }

  std::cout << "server: closing down client" << std::endl;
}

static task<void>
async_server(int clients, int &port)
{
  async_result w;

  auto listener = Gio::SocketListener::new_();
  port = gi::expect(listener.add_any_inet_port());

  int count = 0;
  while (count < clients) {
    // accept clients
    std::cout << "server: accepting" << std::endl;
    listener.accept_async(w);
    auto conn = gi::expect(
        listener.accept_finish(co_await w, (GObject_::Object *)nullptr));

    // spawn client handler
    std::cout << "server: new connection" << std::endl;
    // task will run itself to completion, no need to wait/watch it here
    async_handle_client(conn).detach();
    ++count;
  }

  // wait a bit more and shutdown, because we can
  co_await sleep_for(std::chrono::milliseconds(1000));
  std::cout << "server going down" << std::endl;
}

void
async_demo(GLib::MainLoop loop, int clients)
{
  // run server
  // dispatch at once to obtain port
  int port = 0;
  auto server = async_server(clients, port);

  // make clients
  int count = 0;
  for (int i = 0; i < clients; ++i) {
    ++count;
    // client task runs to completion, no need to wait/watch
    // NOTE this frame will stay alive, so count ref is valid
    async_client(port, i, count).detach();
  }

  // plain-and-simple; poll regularly and quit when all clients done
  co_promise<void> cd;
  auto check = [&]() -> gboolean {
    if (!count) {
      cd.set_value();
      cd.resume();
      return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
  };
  GLib::timeout_add(100, check);

  auto wait = [&]() -> task<void> {
    // wait clients
    co_await cd;
    std::cout << "clients down" << std::endl;
    // server should also have completed
    co_await server;
    std::cout << "server down" << std::endl;
    loop.quit();
  };

  // avoid destruct, alternatively detach()
  auto w = wait();

  std::cout << "running loop" << std::endl;
  loop.run();
  std::cout << "ending loop" << std::endl;
}

int
main(int argc, char **argv)
{
  auto loop = GLib::MainLoop::new_();

  int clients = argc > 1 ? std::stoi(argv[1]) : 0;
  std::cout << clients << " clients" << std::endl;
  if (clients > 0)
    async_demo(loop, clients);
}
