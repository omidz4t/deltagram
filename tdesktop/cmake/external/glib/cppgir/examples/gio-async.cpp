#define GI_INLINE 1
#include <gio/gio.hpp>

#include <iostream>

#if GI_CONFIG_EXCEPTIONS
#include <boost/fiber/all.hpp>

namespace GLib = gi::repository::GLib;
namespace GObject_ = gi::repository::GObject;
namespace Gio = gi::repository::Gio;

class context_scheduler : public boost::fibers::algo::round_robin
{
  typedef context_scheduler self;
  typedef boost::fibers::algo::round_robin super;

  struct src : GSource
  {
    self *scheduler;
  };

  GSourceFuncs funcs{};
  GLib::MainContext ctx_;
  src *source_;
  boost::fibers::condition_variable cond_;
  boost::fibers::mutex mtx_;
  using clock_type = std::chrono::steady_clock;
  bool dispatching_ = false;

  static gboolean src_dispatch(
      GSource *source, GSourceFunc /*callback*/, gpointer /*user_data*/)
  {
    auto s = (src *)(source);
    auto sched = s->scheduler;
    // wait here to give (other) fibers a chance
    sched->dispatching_ = true;
    std::unique_lock<boost::fibers::mutex> lk(sched->mtx_);
    sched->cond_.wait(lk);
    sched->dispatching_ = false;
    // all available work has been done while we were waiting above
    // no need to dispatch again until new work
    // which we accept as of now (due to mainloop activity)
    return G_SOURCE_CONTINUE;
  }

public:
  context_scheduler(GLib::MainContext ctx) : ctx_(ctx)
  {
    // this is a bit too much for bindings, so handle the raw C way
    funcs.dispatch = src_dispatch;
    auto s = g_source_new(&funcs, sizeof(src));
    source_ = (src *)(s);
    source_->scheduler = this;
    g_source_attach(s, ctx.gobj_());
  }

  ~context_scheduler()
  {
    g_source_destroy(source_);
    g_source_unref(source_);
  }

  void awakened(boost::fibers::context *t) noexcept override
  {
    // delegate first
    super::awakened(t);
    // arrange for dispatch of work
    // discard awake of source dispatch
    if (!dispatching_)
      g_source_set_ready_time(source_, 0);
  }

  void suspend_until(
      std::chrono::steady_clock::time_point const &abs_time) noexcept override
  {
    // release dispatch
    // should only end up here while dispatching in source
    // (rather than inadvertently trying to block main loop,
    // which would then lead to busy loop)
    if (dispatching_) {
      // derive time of subsequent dispatch
      if (clock_type::time_point::max() != abs_time) {
        auto to = abs_time - std::chrono::steady_clock::now();
        int ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(to).count();
        ms = std::max(ms, 0);
        g_source_set_ready_time(source_, g_get_monotonic_time() + ms);
      } else {
        g_source_set_ready_time(source_, -1);
      }
      // release source dispatching
      cond_.notify_one();
    } else {
      // suspend is requested, so there is nothing to do for a while
      // so in particular the main fiber is then also blocked (e.g. some sleep)
      // such main loop blocking is also/still not allowed
      // (if such is active)
      g_assert(g_main_depth() == 0);
      // no running loop (so also no dispatch)
      // so delegate to the usual scheduling
      // (which will really block the hard way, rather than poll)
      super::suspend_until(abs_time);
    }
  }

  // might be called from a different thread
  void notify() noexcept override
  {
    // discard our own notify above to resume source dispatch
    if (dispatching_)
      return;
    ctx_.wakeup();
  }
};

class async_future
{
  boost::fibers::promise<Gio::AsyncResult> p_;
  boost::fibers::future<Gio::AsyncResult> f_;
  Gio::Cancellable cancel_;
  GLib::Source timeout_;

public:
  ~async_future()
  {
    if (timeout_)
      timeout_.destroy();
  }

  operator Gio::AsyncReadyCallback()
  {
    // prepare a new promise
    p_ = decltype(p_)();
    f_ = p_.get_future();
    return [&](GObject_::Object, Gio::AsyncResult result) {
      // clear state for subsequent re-use if needed
      if (cancel_ && cancel_.is_cancelled())
        cancel_ = nullptr;
      p_.set_value(result);
    };
  }

  Gio::AsyncResult get() { return f_.get(); }

  Gio::Cancellable cancellable()
  {
    if (!cancel_)
      cancel_ = Gio::Cancellable::new_();
    return cancel_;
  }

  Gio::Cancellable timeout(const std::chrono::milliseconds &to)
  {
    auto cancel = cancellable();
    if (to.count() > 0) {
      timeout_ = GLib::timeout_source_new(to.count());
      auto do_timeout = [cancel]() mutable {
        cancel.cancel();
        return GLib::SOURCE_REMOVE_;
      };
      timeout_.set_callback<GLib::SourceFunc>(do_timeout);
      timeout_.attach(GLib::MainContext::get_thread_default());
    }
    return cancel_;
  }
};

static void
async_client(int port, int id, int &count)
{
  async_future w;

  auto dest = Gio::NetworkAddress::new_loopback(port);

  std::string sid = "client ";
  sid += std::to_string(id);

  // connect a client
  std::cout << sid << ": connect" << std::endl;
  auto client = Gio::SocketClient::new_();
  client.connect_async(dest, w);
  auto conn = gi::expect(client.connect_finish(w.get()));

  // say something
  auto os = conn.get_output_stream();
  std::cout << sid << ": send: " << sid << std::endl;
  os.write_all_async(
      (guint8 *)sid.data(), sid.size(), GLib::PRIORITY_DEFAULT_, w);
  os.write_all_finish(w.get(), (gsize *)nullptr);

  // now hear what the other side has to say
  std::cout << sid << ": receive" << std::endl;
  auto is = conn.get_input_stream();
  while (1) {
    guint8 data[1024];
    is.read_async(data, sizeof(data), GLib::PRIORITY_DEFAULT_, w);
    auto size = gi::expect(is.read_finish(w.get()));
    if (!size)
      break;
    std::string msg(data, data + size);
    std::cout << sid << ": got data: " << msg << std::endl;
  }

  std::cout << sid << ": closing down" << std::endl;
  --count;
}

static void
async_handle_client(Gio::SocketConnection conn)
{
  async_future w;

  // say hello
  auto os = conn.get_output_stream();
  std::string msg = "hello ";
  os.write_all_async(
      (guint8 *)msg.data(), msg.size(), GLib::PRIORITY_DEFAULT_, w);
  os.write_all_finish(w.get(), (gsize *)nullptr);

  // now echo what the other side has to say
  auto is = conn.get_input_stream();
  while (1) {
    guint8 data[1024];
    // give up if timeout
    GLib::Error error;
    is.read_async(data, sizeof(data), GLib::PRIORITY_DEFAULT_,
        w.timeout(std::chrono::milliseconds(200)), w);
    auto size = gi::expect(is.read_finish(w.get(), &error));
    if (error) {
      if (error.matches(G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
        break;
      } else {
        throw error;
      }
    }
    std::string msg(data, data + size);
    std::cout << "server: got data: " << msg << std::endl;
    os.write_all_async(data, size, GLib::PRIORITY_DEFAULT_, w);
    os.write_all_finish(w.get(), (gsize *)nullptr);
  }

  std::cout << "server: closing down client" << std::endl;
}

static void
async_server(int clients, int &port)
{
  async_future w;

  auto listener = Gio::SocketListener::new_();
  port = gi::expect(listener.add_any_inet_port());

  int count = 0;
  while (count < clients) {
    // accept clients
    std::cout << "server: accepting" << std::endl;
    listener.accept_async(w);
    auto conn = gi::expect(
        listener.accept_finish(w.get(), (GObject_::Object *)nullptr));

    // spawn client handler
    std::cout << "server: new connection" << std::endl;
    boost::fibers::fiber c(async_handle_client, conn);
    c.detach();
    ++count;
  }

  // wait a bit and shutdown
  // wait long enough to test the out-of-loop join below
  boost::this_fiber::sleep_for(std::chrono::milliseconds(1000));
  std::cout << "server: shutdown" << std::endl;
}

static void
async_demo(GLib::MainLoop loop, int clients)
{
  // run server
  // dispatch at once to obtain port
  int port = 0;
  boost::fibers::fiber server(
      boost::fibers::launch::dispatch, async_server, clients, std::ref(port));

  // make clients
  int count = 0;
  for (int i = 0; i < clients; ++i) {
    ++count;
    auto c = boost::fibers::fiber(async_client, port, i, std::ref(count));
    c.detach();
  }

  // plain-and-simple; poll regularly and quit when all clients done
  auto check = [&]() {
    if (!count)
      loop.quit();
    return G_SOURCE_CONTINUE;
  };
  GLib::timeout_add(100, check);

  std::cout << "running loop" << std::endl;
  loop.run();
  std::cout << "ending loop" << std::endl;

  server.join();
}

int
main(int argc, char **argv)
{
  GLib::MainLoop loop = GLib::MainLoop::new_();
  auto ctx = GLib::MainContext::default_();
  boost::fibers::use_scheduling_algorithm<context_scheduler>(ctx);

  { // basic fiber demo
    int count = 0;
    auto work = [&](const std::string &msg) {
      std::cout << msg << std::endl;
      ++count;
    };
    auto quit = [&](int limit, const std::chrono::milliseconds &d) {
      while (count < limit)
        boost::this_fiber::sleep_for(d);
      loop.quit();
    };
    boost::fibers::fiber f1(work, "fiber 1");
    boost::fibers::fiber f2(work, "fiber 2");
    boost::fibers::fiber q(quit, 2, std::chrono::milliseconds(100));

    loop.run();

    f1.join();
    f2.join();
    q.join();
  }

  // now an optional async GIO demo
  int clients = argc > 1 ? std::stoi(argv[1]) : 0;
  std::cout << clients << " clients" << std::endl;
  if (clients > 0)
    async_demo(loop, clients);
}

#else
int
main(int argc, char **argv)
{
  (void)argc;
  (void)argv;

  std::cerr << "boost::fibers needs exceptions";
}

#endif
