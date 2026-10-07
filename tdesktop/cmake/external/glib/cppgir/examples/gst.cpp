#include <functional>
#include <iostream>
#include <memory>
#include <ostream>
#include <sstream>
#include <vector>

#include "assert.h"

#ifndef USE_GI_MODULE
// no inline as the implementation is provided by other TUs
// #define GI_INLINE 1
#include <gst/gst.hpp>
#else
// optional, but recommended if gi macros are used
#include <gi/gi_inc.hpp>
// we also use some gst macros and api
#include <gst/gst.h>
// import used modules
import gi.repo.gobject;
import gi.repo.gst;
#endif

namespace GLib = gi::repository::GLib;
namespace Gst = gi::repository::Gst;
namespace GObject_ = gi::repository::GObject;

void
say(const std::string &msg)
{
  std::cout << msg << std::endl;
}

void
die(const std::string &why)
{
  std::cerr << why << std::endl;
  exit(2);
}

class Player
{
  typedef Player self;

  GLib::MainLoop loop_;
  std::string url_;

  Gst::Element playbin_;
  bool live_ = false;
  Gst::Element vfilter_;
  Gst::Element afilter_;
  std::map<std::string, Gst::Element> filter_;
  GLib::SourceScopedConnection monitor_;
  GObject_::SignalConnection busconn_;

public:
  Player(GLib::MainLoop loop, const std::string &url) : loop_(loop), url_(url)
  {
    playbin_ = Gst::ElementFactory::make("playbin");
    assert(playbin_);
    // get a property
    // this one is known at introspection time
    // (so type is known and suitable checked)
    auto name = playbin_.property_name().get();
    std::cout << "created playbin " << name << std::endl;
  }

  void start()
  {
    // set some property
    playbin_.set_property("uri", url_);
    // ... or several ones
    playbin_.set_properties("volume", 0.5, "mute", false);
    // ... non-primitive also possible
    vfilter_ = Gst::ElementFactory::make("identity", "vfilter");
    afilter_ = Gst::ElementFactory::make("identity", "afilter");
    playbin_.set_properties("video-filter", vfilter_, "audio-filter", afilter_);

    // connect some; find out what the source element is
    // signal not known to introspection, so have to provide signature here
    playbin_.connect<void(Gst::Element, Gst::Element)>(
        "source-setup", gi::mem_fun(&self::on_source_setup, this));
    // for a known signal, it is a bit easier to connect
    auto bus = playbin_.get_bus();
    bus.add_signal_watch();
    // signal connect; using introspected (hence checked) signal definition
    // could do without the slot step here and directly pass the lambda,
    // but wrapping it this way allows combining this with the returned
    // (plain) id into a scoped connection guard
    auto slot = bus.signal_message().slot(gi::mem_fun(&self::on_message, this));
    busconn_ =
        gi::make_connection(bus.signal_message().connect(slot), slot, bus);
    // again, if the guard is not desired; the following simply suffices
    if (false)
      bus.signal_message().connect(gi::mem_fun(&self::on_message, this));
    // if the signal signature is somehow not supported
    // then the following is a fallback manual method
    // which still allows to use lambda (and such)
    // and also provides some ownership management
    if (false) {
      auto h = [](GstBus *, GstMessage *) {
        // dummy no-op
      };
      bus.connect_unchecked<void(GstBus *, GstMessage *)>("message", h);
    }
    // likewise, if a callback is to be used whose arguments are not supported
    // or in a function that is not supported, then the following is a fallback
    // which still allows to use lambda (and such)
    // and also provides some ownership management
    if (false) {
      auto h = []() { return false; };
      auto cb = new gi::callback_wrapper<gboolean(), false>(h);
      // now the above pointer is managed as user data
      // and will be suitable destroyed when needed
      g_idle_add_full(0, &cb->wrapper, cb, &cb->destroy);
      // in case of a typical single-use async callback (with no GDestroyNotify)
      // use true as AUTODESTROY template parameter
      // (then it will auto-clean up after invoking callback)
    }
    // alternatively, maybe there is some API that accepts a Closure
    // the following are some convenient ways to get a closure
    if (false) {
      GLib::LogFunc h = [](gi::cstring_v log_domain,
                            GLib::LogLevelFlags log_level,
                            gi::cstring_v message) {
        (void)log_domain;
        (void)log_level;
        (void)message;
      };
      GObject_::Closure::from_callback(h);
    }
    // likewise, but with a bit more manual (C++) signature specification
    if (false) {
      auto h = [](GObject_::Object, int) {};
      GObject_::Closure::from_functor<void(GObject_::Object, int)>(h);
    }

    say("Setting pipeline to PAUSED ...");
    auto ret = playbin_.set_state(Gst::State::PAUSED_);
    if (ret == Gst::StateChangeReturn::FAILURE_) {
      die("Pipeline does not want to pause");
    } else if (ret == Gst::StateChangeReturn::NO_PREROLL_) {
      say("Pipeline is live and does not need PREROLL ...");
      live_ = true;
    } else if (ret == Gst::StateChangeReturn::ASYNC_) {
      say("Pipeline is PREROLLING ...");
    }

    // inspect after a while
    GLib::timeout_add_seconds(2, [this]() {
      inspect();
      return GLib::SOURCE_REMOVE_;
    });

    // some regular progress reporting ...
    GLib::SourceFunc func = [this]() {
      progress();
      return GLib::SOURCE_CONTINUE_;
    };
    // the introspected function returns a plain id,
    // which can be used in the usual way to disconnect
    // e.g. at destructor time of owning object
    // alternatively, a helper scoped object can take care of that
    monitor_ = gi::make_connection(GLib::timeout_add_seconds(1, func), func);
    // other such make_connection variations exist;
    // e.g. for a signal connection, a probe callback
    // (and can easily be custom added)

    // add a pad probe; use a casual lambda
    // but not too casual, mind (dangling) references/pointers though
    filter_["video"] = vfilter_;
    filter_["audio"] = afilter_;
    for (auto &&p : filter_) {
      if (p.second) {
        Gst::Pad pad = p.second.get_static_pad("sink");
        auto name = p.first;
        auto handler = [name](Gst::Pad p, Gst::PadProbeInfo_Ref info) {
          auto s = p.get_path_string();
          s += "received " + name + " buffer";
          auto buffer = info.get_buffer();
          if (buffer) {
            s += " of size ";
            s += std::to_string(buffer.get_size());
          }
          return Gst::PadProbeReturn::REMOVE_;
        };
        pad.add_probe(Gst::PadProbeType::BUFFER_, handler);
      }
    }

    // shamelessly demo some helpers that aid in caps/value handling
    auto caps = Gst::Caps::new_empty_simple("video/x-raw");
    caps.set_value("width", GObject_::Value(Gst::IntRange(240, 320)));
    caps.set_value("pixel-aspect-ratio", GObject_::Value(Gst::Fraction(4, 3)));
    caps.set_value(
        "framerate", GObject_::Value(Gst::FractionRange({25, 1}, 30)));
    // retrieving pretty much the same way
    auto s = caps.get_structure(0);
    auto par = s.get_value("pixel-aspect-ratio").get_value<Gst::Fraction>();
    // helpers also stream to string properly
    std::ostringstream oss;
    oss << par;
    oss << (Gst::FlagSet(1, 1) == Gst::FlagSet(2, 1));
    // silly test code to exercise some operators
    // Rank override allows for succinct numeric conversion
    if (+Gst::Rank::PRIMARY_ + 0) {
      // flags support various typical operations
      (void)(Gst::PadProbeType::BUFFER_ | Gst::PadProbeType::BUFFER_LIST_);
    }
  }

  void stop()
  {
    if (playbin_)
      playbin_.set_state(Gst::State::NULL_);
    loop_.quit();
  }

  void on_message(Gst::Bus /*bus*/, Gst::Message_Ref msg)
  {
    auto &&src = msg.src_();
    switch (msg.type_()) {
      case Gst::MessageType::EOS_:
        say("Got EOS from " + src.get_path_string());
        stop();
        break;
      case Gst::MessageType::ERROR_: {
        GLib::Error err;
        gi::cstring debug;
        msg.parse_error(&err, &debug);
        say("Got error from " + src.get_path_string());
        if (debug.size())
          say("debug info:\n" + debug);
        break;
      }
      case Gst::MessageType::STATE_CHANGED_:
        /* only handle top-level case */
        if (src != playbin_)
          break;
        Gst::State old, new_, pending;
        msg.parse_state_changed(&old, &new_, &pending);
        if (new_ == Gst::State::PAUSED_ && old == Gst::State::READY_)
          playbin_.set_state(Gst::State::PLAYING_);
        break;
      default:
        // never mind
        break;
    }
  }

  void on_source_setup(Gst::Element /*pb*/, Gst::Element src)
  {
    say("source is " + src.get_path_string());
  }

  void inspect()
  {
    std::ostringstream oss;
    // this should work
    auto bin = gi::object_cast<Gst::Bin>(playbin_);
    assert(bin);
    // a dynamically loaded element may implement a number of interfaces
    // which is not known at compile-time
    // the cast above can also cast to interface, which can be obtained as
    // follows if known at introspection/compile time
    auto cp = bin.interface_(gi::interface_tag<Gst::ChildProxy>());
    oss << "player bin has " << cp.get_children_count() << " children"
        << std::endl;
    // get some properties (dynamically, i.e. not known at introspection
    // time) type will have to match (i.e. transformable) at runtime
    auto n_v = playbin_.get_property<int>("n-video");
    auto n_a = playbin_.get_property<int>("n-audio");
    // minimal stuff
    oss << "sample streams:  video=" << n_v << ", audio=" << n_a << std::endl;
    // show some tag info
    for (auto &&p :
        std::map<std::string, int>{{"video", n_v}, {"audio", n_a}}) {
      for (int i = 0; i < p.second; ++i) {
        // the argument's type should match the signal definition
        // (cast if needed to make it so)
        auto action = std::string("get-");
        action += p.first + "-tags";
        auto taglist = playbin_.emit<Gst::TagList>(action, i);
        if (!taglist)
          continue;
        auto ntags = taglist.n_tags();
        oss << p.first << " stream " << i << std::endl;
        for (int j = 0; j < ntags; ++j) {
          auto tname = taglist.nth_tag_name(j);
          auto value = taglist.get_value_index(tname, 0);
#if GI_CONFIG_EXCEPTIONS
          try {
#endif
            auto sval = value.transform_value<std::string>();
            oss << "  " << tname << ": " << sval << std::endl;
#if GI_CONFIG_EXCEPTIONS
          } catch (...) {
            // could be object or otherwise, never mind
          }
#endif
        }
      }
    }

    // should also have caps here by now
    // there are other ways to obtain this, but let's go this way here
    for (auto &&p : filter_) {
      if (p.second) {
        Gst::Pad pad = p.second.get_static_pad("sink");
        auto caps = pad.get_current_caps();
        oss << p.first << " caps: " << caps.to_string() << std::endl;
      }
    }

    say(oss.str());

    say("Playbin elements:");
    // Gst::Iterator could be used with native interface
    // but a helper wrapper has been provided that supports ease-of-use as
    // in ...
    for (auto &&el : Gst::IteratorAdapter<Gst::Element>(bin.iterate_recurse()))
      say(el.get_path_string());
    say("");
  }

  static std::string time_to_str(Gst::ClockTime time)
  {
    // could be done otherwise
    // but we have native C access at hand, so let's use that
    auto s = g_strdup_printf("%" GST_TIME_FORMAT, GST_TIME_ARGS(time));
    std::string ret(s);
    g_free(s);
    return ret;
  }

  void progress()
  {
    bool ok = true;
    gint64 duration = -1, position = -1;
    ok &= playbin_.query_duration(Gst::Format::TIME_, &duration);
    ok &= playbin_.query_position(Gst::Format::TIME_, &position);
    std::ostringstream oss;
    if (ok) {
      oss << "Duration: " << time_to_str(duration)
          << ", Position: " << time_to_str(position);
    } else {
      oss << "No progress info available";
    }
    say(oss.str());
  }
};

#ifdef GI_CLASS_IMPL
// not used in the above, but serves as a subclass example
class ChattyBin : public Gst::impl::BinImpl
{
public:
#if 0
  // this part is only needed if there is some conflict
  // (among members of class and/or interfaces)
  // otherwise it should be auto-detected
  struct DefinitionData
  {
    GI_DEFINES_MEMBER(BinClassDef, add_element, true)
  };
#endif

  ChattyBin() : Gst::impl::BinImpl(this) {}

  bool add_element_(Gst::Element element) noexcept override
  {
    say("adding element " + element.name_() + '\n');
    return Gst::impl::BinImpl::add_element_(element);
  }
};
#endif

int
main(int argc, char **argv)
{
  if (argc < 2)
    die("missing argument");

  // C signature fits C main best anyway
  gst_init(&argc, &argv);

  std::string url = argv[1];
  // make it URL if not so
  if (!Gst::Uri::is_valid(url)) {
#if GI_CONFIG_EXCEPTIONS
    try {
#endif
      url = gi::expect(Gst::filename_to_uri(url));
#if GI_CONFIG_EXCEPTIONS
    } catch (const GLib::Error &ex) {
      die(ex.what());
    }
#endif
  }

  say("Playing " + url);

  // simply local var will do here
  auto loop = GLib::MainLoop::new_();
  Player player(loop, url);

  // schedule start
  GLib::idle_add([&] {
    player.start();
    return GLib::SOURCE_REMOVE_;
  });
  // ... and auto end after a while
  GLib::timeout_add_seconds(10, [&] {
    player.stop();
    return GLib::SOURCE_REMOVE_;
  });

  loop.run();
}
