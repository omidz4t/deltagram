#ifndef GI_OBJECT_HPP
#define GI_OBJECT_HPP

#include "callback.hpp"
#include "container.hpp"
#include "exception.hpp"
#include "objectbase.hpp"
#include "paramspec.hpp"
#include "value.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
// helper

// signal argument connect/emit helper;
// turn (C++) argument into a GType or GValue
// most arguments are inputs (with specific GType),
// but some arguments are used as output with G_TYPE_POINTER (e.g. int*)
// which are mapped to (e.g.) int* or int& in C++ signature
template<typename Arg, bool DECAY>
struct signal_arg
{
  static GType get_type() { return traits::gtype<Arg>::get_type(); }
  static detail::Value make(Arg arg)
  {
    return detail::Value(std::forward<Arg>(arg));
  }
};

// re-route e.g. const std::string& cases
template<typename Arg>
struct signal_arg<const Arg &, false> : public signal_arg<const Arg &, true>
{};

template<typename Arg>
struct signal_arg<Arg &, false>
{
  static GType get_type() { return G_TYPE_POINTER; }
  static detail::Value make(Arg &arg)
  {
    return signal_arg<Arg *, false>::make(&arg);
  }
};

template<typename Arg>
struct signal_arg<Arg *, false>
{
  static GType get_type() { return G_TYPE_POINTER; }
  static detail::Value make(Arg *arg)
  {
    // (size of) wrapper argument should match wrappee
    static_assert(sizeof(typename traits::ctype<Arg>::type) == sizeof(Arg), "");
    // the above should suffice for proper handling
    // however, in these output cases, transfer should also be considered,
    // which is not (yet) available here
    // (but could be passed along similar to callback argument info)
    // so, restrict to plain cases for now
    static_assert(traits::is_plain<Arg>::value, "");
    return detail::Value(gpointer(arg));
  }
};

// returns -size if signed numeric, +size if unsigned numeric, otherwise 0
inline int
get_number_size_signed(GType type)
{
  // note; these are generally lower (absolute) bounds
  // at least it works in the context where it is used below
#define GI_HANDLE_TYPE_SWITCH(cpptype, g_type, factor) \
  case g_type: \
    return factor * int(sizeof(cpptype));
  switch (type) {
    GI_HANDLE_TYPE_SWITCH(gchar, G_TYPE_CHAR, -1)
    GI_HANDLE_TYPE_SWITCH(guchar, G_TYPE_UCHAR, 1)
    GI_HANDLE_TYPE_SWITCH(gint, G_TYPE_INT, -1)
    GI_HANDLE_TYPE_SWITCH(guint, G_TYPE_UINT, 1)
    GI_HANDLE_TYPE_SWITCH(glong, G_TYPE_LONG, -1)
    GI_HANDLE_TYPE_SWITCH(gulong, G_TYPE_ULONG, 1)
    GI_HANDLE_TYPE_SWITCH(gint64, G_TYPE_INT64, -1)
    GI_HANDLE_TYPE_SWITCH(guint64, G_TYPE_UINT64, 1)
  }
#undef GI_HANDLE_TYPE_SWITCH
  return 0;
}

// glib type systems treats G_TYPE_INT64 as distinct from the other types
// in practice, however, quite likely C gint64 == long
inline bool
compatible_type(GType expected, GType actual)
{
  if (expected == G_TYPE_BOOLEAN)
    return std::abs(get_number_size_signed(actual)) == sizeof(gboolean);
  auto ssa_e = get_number_size_signed(expected);
  auto ssa_a = get_number_size_signed(actual);
  return ssa_e == ssa_a;
}

inline void
check_signal_type(GType tp, const gi::cstring_v name, GType return_type,
    GType *param_types, guint n_params)
{
  const char *errmsg("expected ");
  auto check_types = [tp, &name, &errmsg](const std::string &desc,
                         GType expected, GType actual) {
    // normalize
    expected &= ~G_SIGNAL_TYPE_STATIC_SCOPE;
    actual &= ~G_SIGNAL_TYPE_STATIC_SCOPE;
    if (expected == actual || compatible_type(expected, actual) ||
        g_type_is_a(expected, actual))
      return;
    std::string msg = errmsg;
    msg += desc + " type ";
    msg += detail::make_string(g_type_name(expected)) + " != ";
    msg += detail::make_string(g_type_name(actual));
    detail::try_throw(invalid_signal_callback_error(tp, name, msg));
  };

  // determine signal (detail)
  guint id;
  GQuark detail;
  if (!g_signal_parse_name(name.c_str(), tp, &id, &detail, false) || (id == 0))
    detail::try_throw(unknown_signal_error(tp, name));
  // get signal info
  GSignalQuery query;
  g_signal_query(id, &query);
  // check
  if (n_params != query.n_params + 1) {
    auto msg = std::string(errmsg) + "argument count ";
    msg += std::to_string(query.n_params);
    msg += " != " + std::to_string(n_params);
    detail::try_throw(invalid_signal_callback_error(tp, name, msg));
  }
  check_types("return", query.return_type, return_type);
  check_types("instance", query.itype, param_types[0]);
  const std::string arg("argument ");
  for (guint i = 0; i < query.n_params; ++i)
    check_types(
        arg + std::to_string(i + 1), query.param_types[i], param_types[i + 1]);
}

template<typename G>
struct signal_type;

template<typename R, typename... Args>
struct signal_type<R(Args...)>
{
  static void check(GType tp, const gi::cstring_v name)
  {
    // capture type info and delegate
    const int argcount = sizeof...(Args);
    GType ti[] = {signal_arg<Args, false>::get_type()...};
    check_signal_type(tp, name, traits::gtype<R>::get_type(), ti, argcount);
  }
};

// like GParameter, but with extra Value trimming
struct Parameter
{
  const char *name;
  detail::Value value;
};

#ifdef GI_OBJECT_NEWV
GI_DISABLE_DEPRECATED_WARN_BEGIN
static_assert(sizeof(Parameter) == sizeof(GParameter), "");
GI_DISABLE_DEPRECATED_WARN_END
#endif

inline void
fill_parameters(Parameter *)
{
  // no-op
}

template<typename Arg, typename... Args>
inline void
fill_parameters(Parameter *param, const char *name, Arg &&arg, Args &&...args)
{
  param->name = name;
  param->value.init<typename std::remove_reference<Arg>::type>();
  set_value(&param->value, std::forward<Arg>(arg));
  fill_parameters(param + 1, std::forward<Args>(args)...);
}

} // namespace detail

#if GLIB_CHECK_VERSION(2, 54, 0)
#define GI_GOBJECT_PROPERTY_VALUE 1
#endif

namespace repository
{
/* if you have arrived here due to an ambiguous GObject reference
 * (both the C typedef GObject and this namespace)
 * then that can be worked-around by:
 *  + using _GObject (struct name instead)
 *  + adjust 'using namespace' style imports e.g. alias
 *    namespace GObject_ = gi::GObject;
 * or simply do not mention GObject at all and simply use the wrappers ;-)
 */
namespace GObject
{
typedef std::vector<detail::Parameter> construct_params;

template<typename... Args>
construct_params
make_construct_params(Args &&...args)
{
  const int nparams = sizeof...(Args) / 2;
  construct_params parameters;
  parameters.resize(nparams);
  detail::fill_parameters(parameters.data(), std::forward<Args>(args)...);
  return parameters;
}

class Object : public detail::ObjectBase
{
  typedef Object self;
  typedef detail::ObjectBase super_type;

public:
  typedef ::GObject BaseObjectType;

  Object(std::nullptr_t = nullptr) : super_type() {}

  BaseObjectType *gobj_() { return (BaseObjectType *)super_type::gobj_(); }
  const BaseObjectType *gobj_() const
  {
    return (const BaseObjectType *)super_type::gobj_();
  }
  BaseObjectType *gobj_copy_() const
  {
    return (BaseObjectType *)super_type::gobj_copy_();
  }

  // class type
  static GType get_type_() { return G_TYPE_OBJECT; }
  // instance type
  GType gobj_type_() const { return G_OBJECT_TYPE(gobj_()); }

  // type-erased generic object creation
  // transfer full return
  static gpointer new_(GType gtype, const construct_params &params)
  {
#ifdef GI_OBJECT_NEWV
    GI_DISABLE_DEPRECATED_WARN_BEGIN
    auto result =
        g_object_newv(gtype, params.size(), (GParameter *)params.data());
    GI_DISABLE_DEPRECATED_WARN_END
#else
    std::vector<const char *> names;
    std::vector<GValue> values;
    names.reserve(params.size());
    values.reserve(params.size());
    // ownership remains in params
    for (auto &&p : params) {
      names.push_back(p.name);
      values.emplace_back(p.value);
    }
    auto result = g_object_new_with_properties(
        gtype, params.size(), names.data(), values.data());
#endif
    // GIR says transfer full, but let's be careful and really make it so
    // if likely still floating, then we assume ownership
    // but if it is no longer, then it has already been stolen (e.g.
    // GtkWindow), and we need to add one here
    if (g_type_is_a(gtype, G_TYPE_INITIALLY_UNOWNED))
      g_object_ref_sink(result);
    return result;
  }

  // type-based generic object creation
  template<typename CTYPE, typename... Args>
  static auto new_(GType gtype, Args &&...args)
  {
    auto parameters = make_construct_params(std::forward<Args>(args)...);
    auto *result = CTYPE(new_(gtype, parameters));
    return gi::wrap(result, transfer_full);
  }

  // type-based generic object creation
  // Args are a sequence of name, value
  template<typename TYPE, typename... Args>
  static TYPE new_(Args &&...args)
  {
    return new_<typename TYPE::BaseObjectType *>(
        TYPE::get_type_(), std::forward<Args>(args)...);
  }

  // property stuff
  // generic type unsafe
  template<typename V>
  self &set_property(ParamSpec _pspec, V &&val)
  {
    // additional checks
    // allows for basic conversion between arithmetic types
    // without worrying about those details
    auto pspec = _pspec.gobj_();
    detail::Value v(std::forward<V>(val));
    detail::Value dest;
    GValue *p = &v;
    if (G_VALUE_TYPE(&v) != pspec->value_type) {
      g_value_init(&dest, pspec->value_type);
      if (!g_value_transform(&v, &dest))
        detail::try_throw(
            detail::transform_error(pspec->value_type, pspec->name));
      p = &dest;
    }
    g_object_set_property(gobj_(), pspec->name, p);
    return *this;
  }

  template<typename V>
  self &set_property(const gi::cstring_v propname, V &&val)
  {
    return set_property<V>(find_property(propname, true), std::forward<V>(val));
  }

  template<typename V>
  self &set_properties(const gi::cstring_v propname, V &&val)
  {
    return set_property<V>(propname, std::forward<V>(val));
  }

  // set a number of props
  template<typename V, typename... Args>
  self &set_properties(const gi::cstring_v propname, V &&val, Args... args)
  {
    g_object_freeze_notify(gobj_());
#if GI_CONFIG_EXCEPTIONS
    try {
#endif
      set_property(propname, std::forward<V>(val));
      set_properties(std::forward<Args>(args)...);
#if GI_CONFIG_EXCEPTIONS
    } catch (...) {
      g_object_thaw_notify(gobj_());
      throw;
    }
#endif
    g_object_thaw_notify(gobj_());
    return *this;
  }

#ifdef GI_GOBJECT_PROPERTY_VALUE
  self &set_property(const gi::cstring_v propname, Value val)
  {
    g_object_set_property(gobj_(), propname.c_str(), val.gobj_());
    return *this;
  }
#endif

  template<typename V>
  V get_property(const char *propname) const
  {
    // this would return a ref to what is owned by stack-local v below
    static_assert(!traits::is_reftype<V>::value, "dangling ref");
    detail::Value v;
    v.init<V>();
    // the _get_ already tries to transform
    // also close enough to const
    g_object_get_property(const_cast<::GObject *>(gobj_()), propname, &v);
    return detail::get_value<V>(&v);
  }

  template<typename V>
  V get_property(const gi::cstring_v propname) const
  {
    return get_property<V>(propname.c_str());
  }

#ifdef GI_GOBJECT_PROPERTY_VALUE
  Value get_property(const gi::cstring_v propname) const
  {
    Value result;
    const gchar *name = propname.c_str();
    GValue *val = result.gobj_();
    g_object_getv(const_cast<::GObject *>(gobj_()), 1, &name, val);
    return result;
  }
#endif

  static ParamSpec find_property(
      GType gtype, const gi::cstring_v propname, bool _throw = false)
  {
    GParamSpec *spec;
    if (g_type_is_a(gtype, G_TYPE_INTERFACE)) {
      // interface should be loaded if we have an instance here
      auto vtable = g_type_default_interface_peek(gtype);
      spec = g_object_interface_find_property(vtable, propname.c_str());
    } else {
      spec = g_object_class_find_property(
          (GObjectClass *)g_type_class_peek(gtype), propname.c_str());
    }
    if (_throw && !spec)
      detail::try_throw(
          detail::unknown_property_error(gtype, propname.c_str()));
    return gi::wrap(spec, transfer_none);
  }

  ParamSpec find_property(
      const gi::cstring_v propname, bool _throw = false) const
  {
    return find_property(gobj_type_(), propname, _throw);
  }

  gi::Collection<gi::DSpan, GParamSpec *, gi::transfer_container_t>
  list_properties() const
  {
    GParamSpec **specs;
    guint nspecs = 0;
    if (g_type_is_a(gobj_type_(), G_TYPE_INTERFACE)) {
      // interface should be loaded if we have an instance here
      auto vtable = g_type_default_interface_peek(gobj_type_());
      specs = g_object_interface_list_properties(vtable, &nspecs);
    } else {
      specs =
          g_object_class_list_properties(G_OBJECT_GET_CLASS(gobj_()), &nspecs);
    }
    return wrap_to<
        gi::Collection<gi::DSpan, GParamSpec *, gi::transfer_container_t>>(
        specs, nspecs, transfer_container);
  }

  // signal stuff
private:
  template<typename F, typename Functor>
  gulong connect_data(
      const gi::cstring_v signal, Functor &&f, GConnectFlags flags)
  {
    // runtime signature check
    detail::signal_type<F>::check(gobj_type_(), signal);
    auto w = new detail::transform_signal_wrapper<F>(std::forward<Functor>(f));
    // mind gcc's -Wcast-function-type
    return g_signal_connect_data(gobj_(), signal.c_str(),
        (GCallback)&w->wrapper, w, (GClosureNotify)(GCallback)&w->destroy,
        flags);
  }

public:
  template<typename F, typename Functor>
  gulong connect(const gi::cstring_v signal, Functor &&f)
  {
    return connect_data<F, Functor>(
        signal, std::forward<Functor>(f), (GConnectFlags)0);
  }

  template<typename F, typename Functor>
  gulong connect_after(const gi::cstring_v signal, Functor &&f)
  {
    return connect_data<F, Functor>(
        signal, std::forward<Functor>(f), G_CONNECT_AFTER);
  }

  // TODO the object variants ??

  // in case of unsupported signal signature
  // connect using a plain C signature without check/transform (wrap/unwrap)
  template<typename F, typename Functor>
  gulong connect_unchecked(
      const gi::cstring_v signal, Functor &&f, GConnectFlags flags = {})
  {
    auto w = new detail::callback_wrapper<F>(std::forward<Functor>(f));
    // mind gcc's -Wcast-function-type
    return g_signal_connect_data(gobj_(), signal.c_str(),
        (GCallback)&w->wrapper, w, (GClosureNotify)(GCallback)&w->destroy,
        flags);
  }

  void disconnect(gulong id) { g_signal_handler_disconnect(gobj_(), id); }

  // Args... may be explicitly specified or deduced
  // if deduced; arrange to decay/strip reference below
  // if not deduced; may need to considere specified type as-is
  template<typename R, bool DECAY = true, typename... Args>
  R emit(const gi::cstring_v signal, Args &&...args)
  {
    // static constexpr bool DECAY = true;
    guint id;
    GQuark detail;
    if (!g_signal_parse_name(signal.c_str(), gobj_type_(), &id, &detail, true))
      detail::try_throw(std::out_of_range(std::string("unknown signal name: ") +
                                          detail::make_string(signal.c_str())));

    detail::Value values[] = {detail::Value(*this),
        detail::signal_arg<Args, DECAY>::make(std::forward<Args>(args))...};
    detail::Value retv;
    retv.init<R>();
    g_signal_emitv(values, id, detail, &retv);
    return detail::get_value<R>(&retv);
  }

  void handler_block(gulong handler_id)
  {
    g_signal_handler_block(gobj_(), handler_id);
  }

  void handler_unblock(gulong handler_id)
  {
    g_signal_handler_unblock(gobj_(), handler_id);
  }

  bool handler_is_connected(gulong handler_id)
  {
    return g_signal_handler_is_connected(gobj_(), handler_id);
  }

  void stop_emission(guint id, GQuark detail)
  {
    g_signal_stop_emission(gobj_(), id, detail);
  }

  void stop_emission_by_name(const gi::cstring_v signal)
  {
    g_signal_stop_emission_by_name(gobj_(), signal.c_str());
  }
};

} // namespace GObject

template<>
struct declare_cpptype_of<::GObject>
{
  typedef repository::GObject::Object type;
};

namespace GLib
{
// predefined
typedef detail::callback<void(), gi::transfer_none_t> DestroyNotify;
} // namespace GLib

} // namespace repository

// type safe signal connection
template<typename T, typename Base = repository::GObject::Object>
class signal_proxy;

template<typename R, typename Instance, typename... Args, typename Base>
class signal_proxy<R(Instance, Args...), Base>
{
protected:
  typedef R(CppSig)(Instance, Args...);
  Base object_;
  gi::cstring name_;

public:
  typedef CppSig function_type;
  typedef detail::connectable<function_type> slot_type;

  signal_proxy(Base owner, gi::cstring name)
      : object_(owner), name_(std::move(name))
  {}

  template<typename Functor>
  gulong connect(Functor &&f)
  {
    return object_.template connect<CppSig>(name_, std::forward<Functor>(f));
  }

  template<typename Functor>
  gulong connect_after(Functor &&f)
  {
    return object_.template connect_after<CppSig>(
        name_, std::forward<Functor>(f));
  }

  R emit(Args... args)
  {
    return object_.template emit<R, false, Args...>(
        name_, std::forward<Args>(args)...);
  }

  template<typename Functor>
  slot_type slot(Functor &&f)
  {
    return slot_type(std::forward<Functor>(f));
  }
};

// type safe property setting
template<typename T, typename Base = repository::GObject::Object>
class property_proxy
{
  typedef property_proxy self;
  typedef repository::GObject::ParamSpec ParamSpec;

protected:
  Base object_;
  ParamSpec pspec_;

public:
  property_proxy(Base owner, ParamSpec pspec) : object_(owner), pspec_(pspec) {}

  property_proxy(Base owner, const gi::cstring_v name)
      : property_proxy(owner, owner.find_property(name, true))
  {}

  void set(T v) { object_.set_property(pspec_, std::move(v)); }

  self &operator=(T v)
  {
    set(v);
    return *this;
  }

  T get() const
  {
    return object_.template get_property<T>(pspec_.gobj_()->name);
  }

  ParamSpec param_spec() const { return pspec_; }

  signal_proxy<void(Base, ParamSpec)> signal_notify() const
  {
    return signal_proxy<void(Base, ParamSpec)>(
        object_, gi::cstring_v("notify::") + gi::cstring_v(pspec_.name_()));
  }
};

template<typename T, typename Base = repository::GObject::Object>
class property_proxy_read : private property_proxy<T, Base>
{
  typedef property_proxy<T, Base> super;

public:
  using super::get;
  using super::property_proxy;
};

template<typename T, typename Base = repository::GObject::Object>
class property_proxy_write : private property_proxy<T, Base>
{
  typedef property_proxy<T, Base> super;

public:
  using super::property_proxy;
  using super::set;
  using super::operator=;
};

// interface (ptr) is wrapped the same way,
// as it is essentially a ptr to implementing object
// TODO use other intermediate base ??
using InterfaceBase = repository::GObject::Object;

namespace repository
{
namespace GObject
{
// connection helpers
namespace internal
{
class SignalConnection : public detail::connection_impl
{
public:
  SignalConnection(gulong id, detail::connection_status s, Object object)
      : connection_impl(id, s), object_(object)
  {}

  void disconnect() { object_.disconnect(id_); }

private:
  Object object_;
};

} // namespace internal

using SignalConnection = detail::connection<internal::SignalConnection>;
using SignalScopedConnection = detail::scoped_connection<SignalConnection>;

} // namespace GObject

} // namespace repository

// connection callback type
template<typename G>
using slot = detail::connectable<G>;

template<typename G>
inline repository::GObject::SignalConnection
make_connection(
    gulong id, const gi::slot<G> &s, repository::GObject::Object object)
{
  using repository::GObject::SignalConnection;
  return SignalConnection(id, s.connection(), object);
}

} // namespace gi

#endif // GI_OBJECT_HPP
