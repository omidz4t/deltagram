#ifndef _GI_GTK_GTK_EXTRA_DEF_HPP_
#define _GI_GTK_GTK_EXTRA_DEF_HPP_

#include <map>
#include <string>

namespace gi
{
namespace repository
{
namespace Gtk
{
namespace base
{
#ifdef _GI_GTK_LIST_STORE_EXTRA_DEF_HPP_
// deprecated (as of about 4.9.1)
// so only define implementation here if declaration has been included
template<typename... Args>
Gtk::ListStore
ListStoreExtra::new_type_() noexcept
{
  GType columns[] = {traits::gtype<Args>::get_type()...};
  return ListStoreBase::new_(G_N_ELEMENTS(columns), columns);
};
#endif
} // namespace base

// class generated parts are needed
#if !GTK_CHECK_VERSION(4, 0, 0) || GI_CLASS_IMPL
namespace impl
{
// gtk widget template helper
class WidgetTemplateHelper
{
public:
  enum class ConnectObject { NONE, TAIL, HEAD };

protected:
  using ConnectData =
      std::map<GType, std::map<std::string, gi::repository::GObject::Closure>>;

  // inner namespace
  class Internal
  {
    template<typename TUPLE>
    struct tuple_strip_last;

    template<typename... T>
    struct tuple_strip_last<std::tuple<T...>>
    {
    protected:
      using Tuple = std::tuple<T...>;
      template<typename W>
      struct inner;

      template<std::size_t... Index>
      struct inner<std::index_sequence<Index...>>
      {
        using type = std::tuple<
            typename std::tuple_element<Index, std::tuple<T...>>::type...>;
      };

    public:
      using type =
          typename inner<std::make_index_sequence<sizeof...(T) - 1>>::type;
    };

  public:
    struct ProxyClosure
    {
      GClosure base;
      // actual target closure
      GObject::Closure actual;
      // duplicated from actual, holds ownership
      GCallback cb;
      gpointer user_data;
      // perhaps through a thunk call helper
      GObject::Closure thunk;
      // no ref, guarded by watch
      ::GObject *object;
      gboolean swapped;
    };

    // FUNCTOR represents C helper that transforms to Cpp call
    // so it has C-types of FUNCTOR follows by functor user_data
    template<ConnectObject CONNECT, typename FUNCTOR>
    struct make_thunk : public std::false_type
    {
      struct call_type
      {
        // call should accept (signal emit object, signal args, proxy user_data)
        // and transform this to a call in FUNCTOR form
        // (suitably adding extra tail object and optionally swapping)
        static void call() {}
      };
    };

    // essentially SWAPPED case
    template<typename R, typename FirstArg, typename... Args>
    struct make_thunk<ConnectObject::HEAD, R (*)(FirstArg, Args...)>
    {
      // ... so FirstArg is extra object
      static_assert(std::is_pointer<FirstArg>::value, "");
      // remove user data
      using ArgsType = typename tuple_strip_last<std::tuple<Args...>>::type;
      // signal emit object
      using ObjectArgType =
          typename std::tuple_element<sizeof...(Args) - 2, ArgsType>::type;
      static_assert(std::is_pointer<ObjectArgType>::value, "");
      using SignalArgsType = typename tuple_strip_last<ArgsType>::type;

      template<typename T>
      struct thunk;

      template<typename... TArgs>
      struct thunk<std::tuple<TArgs...>>
      {
        // transform to call to
        //   (extra object, signal args, signal emit object, functor user_data)
        static R call(ObjectArgType arg, TArgs... args, gpointer user_data)
        {
          auto tdata = (ProxyClosure *)(user_data);
          typedef R (*func_type)(
              ::GObject *, TArgs..., ObjectArgType, gpointer);
          return ((func_type)(tdata->cb))(
              tdata->object, args..., arg, tdata->user_data);
        }
      };

      using call_type = thunk<SignalArgsType>;
    };

    // essentially non-SWAPPED case
    template<typename R, typename... Args>
    struct make_thunk<ConnectObject::TAIL, R (*)(Args...)>
    {
      // remove user data
      using AllArgsType = typename tuple_strip_last<std::tuple<Args...>>::type;
      // extra object
      using ObjectArgType =
          typename std::tuple_element<sizeof...(Args) - 2, AllArgsType>::type;
      static_assert(std::is_pointer<ObjectArgType>::value, "");

      template<typename T>
      struct thunk;

      template<typename... TArgs>
      struct thunk<std::tuple<TArgs...>>
      {
        // transform to call to
        //   (signal emit object, signal args, extra object, functor user_data)
        static R call(TArgs... args, gpointer user_data)
        {
          auto tdata = (ProxyClosure *)(user_data);
          typedef R (*func_type)(TArgs..., ::GObject *, gpointer);
          return ((func_type)(tdata->cb))(
              args..., tdata->object, tdata->user_data);
        }
      };

      using SignalArgsType = typename tuple_strip_last<AllArgsType>::type;
      using call_type = thunk<SignalArgsType>;
    };

    static void proxy_marshal(GClosure *closure, GValue *return_value,
        guint n_param_values, const GValue *param_values,
        gpointer invocation_hint, gpointer marshal_data)
    {
      // should only be used internally in gclosure for class closure cases
      g_return_if_fail(marshal_data == NULL);

      // call actual target if one has been provided
      auto pclosure = (ProxyClosure *)(closure);
      if (pclosure->actual) {
        // might have to go through thunk if extra object
        g_assert(!!pclosure->thunk == !!pclosure->object);
        auto tclosure = pclosure->thunk ? pclosure->thunk.gobj_()
                                        : pclosure->actual.gobj_();
        g_closure_invoke(tclosure, return_value, n_param_values, param_values,
            invocation_hint);
      }
    }

    static void proxy_finalize(gpointer, GClosure *closure)
    {
      auto pclosure = (ProxyClosure *)(closure);
      pclosure->actual = nullptr;
      pclosure->thunk = nullptr;
    }

    static GObject::Closure make_proxy_closure(
        ::GObject *object = nullptr, bool swapped = false)
    {
      auto closure = g_closure_new_simple(sizeof(ProxyClosure), nullptr);
      g_closure_set_marshal(closure, proxy_marshal);
      g_closure_sink(g_closure_ref(closure));
      g_closure_add_finalize_notifier(closure, nullptr, proxy_finalize);
      // extra tracking
      auto pclosure = (ProxyClosure *)closure;
      if (object) {
        g_object_watch_closure(object, closure);
        pclosure->object = object;
        // only relevant if object
        pclosure->swapped = swapped;
      }
      return gi::wrap(closure, gi::transfer_full);
    }

    // work-around missing inline support
    static ConnectData *&get_connect_data()
    {
      thread_local ConnectData *connect_data;
      return connect_data;
    }

    static GQuark get_template_quark()
    {
      static const char *KEY = "GIOBJECT_TEMPLATE";
      static GQuark q = g_quark_from_static_string(KEY);
      return q;
    }

    static void builder_connect_function(GtkBuilder *builder, ::GObject *object,
        const gchar *signal_name, const gchar *handler_name,
        ::GObject *connect_object, GConnectFlags flags, gpointer user_data)
    {
      (void)builder;
      (void)user_data;

      auto closure = Internal::make_proxy_closure(
          connect_object, flags & G_CONNECT_SWAPPED);
      g_signal_connect_closure(
          object, signal_name, closure.gobj_(), flags & G_CONNECT_AFTER);

      auto connect_data = get_connect_data();
      g_assert(connect_data);
      if (connect_data) {
        (*connect_data)[G_OBJECT_TYPE(object)][handler_name] = closure;
      }
    }
  };

#if GTK_CHECK_VERSION(4, 0, 0)
  class BuilderScope : public Gtk::impl::BuilderCScopeImpl
  {
  public:
    BuilderScope() : Gtk::impl::BuilderCScopeImpl(this) {}

    GObject::Closure create_closure_(Gtk::Builder builder,
        const gi::cstring_v function_name, Gtk::BuilderClosureFlags flags,
        GObject::Object object, GLib::Error *_error) override
    {
      (void)_error;

      // current object is the template'd one being init'ed
      // a direct call is used as a wrapped call might fiddle with refs
      // while the object is still being init'ed, which does not end well
      auto current = gtk_builder_get_current_object(builder.gobj_());
      auto connect_data = current ? (ConnectData *)g_object_get_qdata(
                                        current, Internal::get_template_quark())
                                  : nullptr;
      g_assert(connect_data);
      if (connect_data) {
        auto wrapped = Internal::make_proxy_closure(
            object.gobj_(), (flags & Gtk::BuilderClosureFlags::SWAPPED_) ==
                                Gtk::BuilderClosureFlags::SWAPPED_);
        (*connect_data)[G_OBJECT_TYPE(current)][function_name] = wrapped;
        return wrapped;
      }

      return nullptr;
    }
  };
#endif

  // owned by qdata
  ConnectData *connect_data_;

public:
  WidgetTemplateHelper(gi::repository::GObject::Object object)
  {
    if (object) {
      connect_data_ = (ConnectData *)g_object_get_qdata(
          object.gobj_(), Internal::get_template_quark());
      g_assert(connect_data_);
    }
  }

  template<GType (*typefunc)() = nullptr>
  static void custom_class_init(gpointer klass, gpointer)
  {
    G_OBJECT_CLASS(klass)->dispose = custom_dispose<typefunc>;
#if GTK_CHECK_VERSION(4, 0, 0)
    auto builder = gi::make_ref<BuilderScope>();
    gtk_widget_class_set_template_scope(
        GTK_WIDGET_CLASS(klass), GTK_BUILDER_SCOPE(builder->gobj_()));
#else
    gtk_widget_class_set_connect_func(GTK_WIDGET_CLASS(klass),
        Internal::builder_connect_function, nullptr, nullptr);
#endif
  }

  static void custom_init(GtkWidget *instance, gpointer)
  {
    // avoid namespace interference
    using GObject = ::GObject;
    auto tq = Internal::get_template_quark();
    // prepare storage
    // in case of multi-level subclassing, it is used for all
    ConnectData *cdp;
    if (!(cdp = (ConnectData *)g_object_get_qdata(G_OBJECT(instance), tq))) {
      auto deleter = [](gpointer d) { delete (ConnectData *)d; };
      auto cd = std::make_unique<ConnectData>();
      cdp = cd.get();
      g_object_set_qdata_full(G_OBJECT(instance), tq, cd.release(), deleter);
    }
#if GTK_CHECK_VERSION(4, 0, 0)
    (void)cdp;
    gtk_widget_init_template(GTK_WIDGET(instance));
#else
    // unfortunate API (which does not provide access to instance)
    // make ConnectData instance active on this thread
    Internal::get_connect_data() = cdp;
    gtk_widget_init_template(GTK_WIDGET(instance));
    // so we got all that during above _init call
    Internal::get_connect_data() = nullptr;
#endif
  }

  template<GType (*typefunc)()>
  static void custom_dispose(::GObject *gobject)
  {
    // this intermediate step avoids a -Waddress warning on recent gcc
    auto tf = typefunc;
    auto gtype = tf ? typefunc() : G_OBJECT_TYPE(gobject);

#if GTK_CHECK_VERSION(4, 8, 0)
    gtk_widget_dispose_template(GTK_WIDGET(gobject), gtype);
#endif
    // need to know our place in the type chain
    // (in C, this is usually handled by the parent_class variable)
    // fallback to the leaf type if all else fails, but that too may fail
    G_OBJECT_CLASS(g_type_class_peek(g_type_parent(gtype)))->dispose(gobject);
  }

  template<typename F, ConnectObject c = ConnectObject::NONE, typename Functor>
  bool set_handler(gi::cstring_v handler_name, Functor &&f, GType gtype = 0)
  {
    auto _cd = (ConnectData *)connect_data_;
    if (!_cd) {
      g_assert_not_reached();
      return false;
    }
    auto cd = *_cd;
    // find name in collected data
    Internal::ProxyClosure *proxy{};
    for (auto &e : cd) {
      if (gtype != 0 && gtype != e.first)
        continue;
      auto it = e.second.find(handler_name);
      if (it != e.second.end()) {
        // perhaps found one already
        if (proxy) {
          g_warning("ambiguous template entry for %s", handler_name.c_str());
          return false;
        }
        proxy = (Internal::ProxyClosure *)(it->second.gobj_());
        // continue search to check for conflicts in case of wildcard type
        if (gtype != 0)
          break;
      }
    }
    if (!proxy)
      return false;
    // sanity checks; provided parameters match UI data
    g_return_val_if_fail(!!proxy->object == (c != ConnectObject::NONE), FALSE);
    g_return_val_if_fail(
        !proxy->object || proxy->swapped == (c == ConnectObject::HEAD), FALSE);
    // go go
    // standard closure, also owns functor data
    auto w =
        new gi::detail::transform_signal_wrapper<F>(std::forward<Functor>(f));
    auto closure = g_cclosure_new(
        (GCallback)&w->wrapper, w, (GClosureNotify)(GCallback)&w->destroy);
    g_closure_sink(g_closure_ref(closure));
    g_closure_set_marshal(closure, g_cclosure_marshal_generic);
    proxy->cb = (GCallback)&w->wrapper;
    proxy->user_data = w;
    proxy->actual = gi::wrap(closure, gi::transfer_full);
    proxy->thunk = nullptr;
    // optional thunk closure to invoke helper thunk to handle object
    if (proxy->object) {
      auto cb_thunk = (GCallback)&Internal::make_thunk<c,
          decltype(&w->wrapper)>::call_type::call;
      auto thunk = g_cclosure_new(cb_thunk, proxy, nullptr);
      g_closure_set_marshal(thunk, g_cclosure_marshal_generic);
      g_closure_sink(g_closure_ref(thunk));
      proxy->thunk = gi::wrap(thunk, gi::transfer_full);
    }
    return true;
  }
};
} // namespace impl
#endif

} // namespace Gtk

} // namespace repository

} // namespace gi

#endif
