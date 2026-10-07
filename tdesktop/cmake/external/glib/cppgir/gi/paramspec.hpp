#ifndef GI_PARAMSPEC_HPP
#define GI_PARAMSPEC_HPP

#include "objectbase.hpp"
#include "value.hpp"

GI_MODULE_EXPORT
namespace gi
{
// slightly nasty; will be generated
namespace repository
{
namespace GObject
{
enum class ParamFlags : std::underlying_type<::GParamFlags>::type;
}
} // namespace repository

namespace detail
{
struct GParamSpecFuncs
{
  static void *ref(void *data) { return g_param_spec_ref((GParamSpec *)data); }
  static void *sink(void *data)
  {
    return g_param_spec_ref_sink((GParamSpec *)data);
  }
  static void free(void *data) { g_param_spec_unref((GParamSpec *)data); }
  static void *float_(void *data) { return data; }
};

using ParamFlags = repository::GObject::ParamFlags;

// helper paramspec type
template<typename T>
struct param_spec_constructor;

#define GI_DECLARE_PARAM_SPEC(cpptype, suffix) \
  template<> \
  struct param_spec_constructor<cpptype> \
  { \
    typedef std::true_type range_type; \
    static const constexpr decltype(&g_param_spec_##suffix) new_ = \
        g_param_spec_##suffix; \
  };

GI_DECLARE_PARAM_SPEC(char, char)
GI_DECLARE_PARAM_SPEC(unsigned char, uchar)
GI_DECLARE_PARAM_SPEC(int, int)
GI_DECLARE_PARAM_SPEC(unsigned int, uint)
GI_DECLARE_PARAM_SPEC(long, long)
GI_DECLARE_PARAM_SPEC(unsigned long, ulong)
GI_DECLARE_PARAM_SPEC(long long, int64)
GI_DECLARE_PARAM_SPEC(unsigned long long, uint64)
GI_DECLARE_PARAM_SPEC(float, float)
GI_DECLARE_PARAM_SPEC(double, double)

#undef GI_DECLARE_PARAM_SPEC

// specialize appropriately with static new_ member
template<typename T, typename Enable = void>
struct ParamSpecFactory;

template<typename T>
struct ParamSpecFactory<T,
    typename std::enable_if<param_spec_constructor<T>::range_type::value>::type>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb, T min, T max, T _default,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return param_spec_constructor<T>::new_(name.c_str(), nick.c_str(),
        blurb.c_str(), min, max, _default, (GParamFlags)flags);
  }

  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb, T min, T max,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return new_(name, nick, blurb, min, max, T{}, flags);
  }
};

template<>
struct ParamSpecFactory<bool>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb, bool _default,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return g_param_spec_boolean(name.c_str(), nick.c_str(), blurb.c_str(),
        _default, (GParamFlags)flags);
  }
};

template<>
struct ParamSpecFactory<gpointer>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return g_param_spec_pointer(
        name.c_str(), nick.c_str(), blurb.c_str(), (GParamFlags)flags);
  }
};

template<>
struct ParamSpecFactory<std::string>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb, const gi::cstring_v _default,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return g_param_spec_string(name.c_str(), nick.c_str(), blurb.c_str(),
        _default.c_str(), (GParamFlags)flags);
  }
};

template<>
struct ParamSpecFactory<gi::cstring> : public ParamSpecFactory<std::string>
{};

template<typename T>
struct ParamSpecFactory<T,
    typename std::enable_if<traits::is_object<T>::value>::type>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return g_param_spec_object(name.c_str(), nick.c_str(), blurb.c_str(),
        traits::gtype<T>::get_type(), (GParamFlags)flags);
  }
};

template<typename T>
struct ParamSpecFactory<T,
    typename std::enable_if<traits::is_boxed<T>::value>::type>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    return g_param_spec_boxed(name.c_str(), nick.c_str(), blurb.c_str(),
        traits::gtype<T>::get_type(), (GParamFlags)flags);
  }
};

template<typename T>
struct ParamSpecFactory<T,
    typename std::enable_if<traits::is_enum_or_bitfield<T>::value>::type>
{
  static GParamSpec *new_(const gi::cstring_v name, const gi::cstring_v nick,
      const gi::cstring_v blurb, guint _default = 0,
      ParamFlags flags = (ParamFlags)G_PARAM_READWRITE)
  {
    GType t = traits::gtype<T>::get_type();
    // FIXME compile-time determination rather than dynamic ??
    return G_TYPE_IS_FLAGS(t)
               ? g_param_spec_flags(name.c_str(), nick.c_str(), blurb.c_str(),
                     t, _default, (GParamFlags)flags)
               : g_param_spec_enum(name.c_str(), nick.c_str(), blurb.c_str(), t,
                     _default, (GParamFlags)flags);
  }
};

} // namespace detail

namespace repository
{
// slightly nasty
namespace GObject
{
class ParamSpec;
}

template<>
struct declare_cpptype_of<GParamSpec>
{
  typedef GObject::ParamSpec type;
};

namespace GObject
{
class ParamSpec : public detail::WrapperBase<GParamSpec,
                      detail::GParamSpecFuncs, G_TYPE_PARAM>
{
  typedef WrapperBase<GParamSpec, detail::GParamSpecFuncs, G_TYPE_PARAM> super;

public:
  ParamSpec(std::nullptr_t = nullptr) {}

  template<typename T, typename... Args>
  static ParamSpec new_(Args &&...args)
  {
    return static_cast<ParamSpec &&>(
        super(detail::ParamSpecFactory<T>::new_(std::forward<Args>(args)...)));
  }

  // special override case
  static ParamSpec new_(const gi::cstring_v name, ParamSpec overridden)
  {
    return static_cast<ParamSpec &&>(
        super(g_param_spec_override(name.c_str(), overridden.gobj_())));
  }

  gi::cstring_v get_blurb() { return g_param_spec_get_blurb(gobj_()); }

  gi::cstring_v get_nick() { return g_param_spec_get_nick(gobj_()); }

  gi::cstring_v get_name() { return g_param_spec_get_name(gobj_()); }

  GQuark get_name_quark() { return g_param_spec_get_name_quark(gobj_()); }

  repository::GObject::Value get_default_value()
  {
    return gi::wrap(g_param_spec_get_default_value(gobj_()), transfer_none);
  }

  ParamSpec get_redirect_target()
  {
    return gi::wrap(g_param_spec_get_redirect_target(gobj_()), transfer_none);
  }

  // struct fields
  const gchar *name_() const { return gobj_()->name; }

  ParamFlags value_type() const { return (ParamFlags)gobj_()->flags; }

  GType value_type_() const { return gobj_()->value_type; }

  GType owner_type_() const { return gobj_()->owner_type; }
};

} // namespace GObject

} // namespace repository

} // namespace gi

#endif // GI_PARAMSPEC_HPP
