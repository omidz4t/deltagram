#ifndef GI_VALUE_HPP
#define GI_VALUE_HPP

#include "exception.hpp"
#include "wrap.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace repository
{
// specialize to declare gtype info
// if not within class get_type()
// gvalue info can also be included this way
// to inject support into Value wrapper
template<typename T>
struct declare_gtype_of : public std::false_type
{};

} // namespace repository

namespace traits
{
namespace detail
{
template<typename T, class Enable = void>
struct gtype : public std::false_type
{
  static GType get_type()
  {
    // dummy test to trigger (almost) always
    static_assert(std::is_void<T>::value, "type is not a registered GType");
    return 0;
  }
};

// gboxed/gobject (or otherwise class) cases
template<typename T>
struct gtype<T, typename if_valid_type<decltype(T::get_type_())>::type>
    : public std::true_type
{
  typedef typename std::remove_reference<T>::type CppType;
  static GType get_type() { return CppType::get_type_(); }
};

// otherwise externally declared
template<typename T>
struct gtype<T, typename if_valid_type<decltype(repository::declare_gtype_of<
                    T>::get_type())>::type> : public std::true_type
{
  static constexpr GType (
      *get_type)() = repository::declare_gtype_of<T>::get_type;
};

// gvalue helper info
template<typename T, class Enable = void>
struct gvalue : public std::false_type
{};

// as declared (both of set_value and get_value or neither)
template<typename T>
struct gvalue<T,
    typename if_valid_type<decltype(repository::declare_gtype_of<T>::get_value(
        nullptr))>::type> : public std::true_type
{
  static T get(const GValue *val)
  {
    return repository::declare_gtype_of<T>::get_value(val);
  }
  static void set(GValue *val, T t)
  {
    repository::declare_gtype_of<T>::set_value(val, t);
  }
};

template<typename T>
using is_enum_or_bitfield =
    typename std::conditional<std::is_enum<T>::value && gtype<T>::value,
        std::true_type, std::false_type>::type;

// handle enum/flags cases, rather than many declares
template<typename T>
struct gvalue<T, typename std::enable_if<is_enum_or_bitfield<T>::value>::type>
    : public std::true_type
{
  static T get(const GValue *val)
  {
    GType t = gtype<T>::get_type();
    if (G_TYPE_IS_FLAGS(t))
      return static_cast<T>(g_value_get_flags(val));
    // assume enum, let glib complain otherwise
    return static_cast<T>(g_value_get_enum(val));
  }

  static void set(GValue *val, T v)
  {
    GType t = gtype<T>::get_type();
    if (G_TYPE_IS_FLAGS(t)) {
      g_value_set_flags(val, (guint)v);
    } else {
      // assume enum, let glib complain otherwise
      g_value_set_enum(val, (gint)v);
    }
  }
};

} // namespace detail

template<typename T>
using gtype = detail::gtype<
    typename std::decay<typename std::remove_reference<T>::type>::type>;

template<typename T>
using gvalue = detail::gvalue<
    typename std::decay<typename std::remove_reference<T>::type>::type>;

template<typename T>
using is_enum_or_bitfield = detail::is_enum_or_bitfield<
    typename std::decay<typename std::remove_reference<T>::type>::type>;

#if 0
template<typename T, typename Enable = void>
struct is_flag : public std::false_type {};

// TODO extend fundamental type stuff ??
template<typename T>
struct is_flag<T,
    typename std::enable_if<std::is_enum<T>::value &&
        repository::declare_gtype_of<T>::get_fundamental_type() == G_TYPE_FLAGS>::type> :
  public std::true_type {};
#endif

} // namespace traits

// C++ types are used below (e.g. int) instead of e.g. gint
// since gint64 might map to same as glong (or not)
// so some of the int types are "best approximation" from C++ type to GType
// instead; use the following types to guide to the right overload
typedef char vchar;
typedef long vlong;
typedef int vint;
typedef long long vint64;
typedef bool vboolean;

typedef unsigned char vuchar;
typedef unsigned long vulong;
typedef unsigned int vuint;
typedef unsigned long long vuint64;

typedef float vfloat;
typedef double vdouble;

// NOTE: (plain) char might be signed or unsigned depending on platform
// but gchar == char always anyway
static_assert(std::is_same<gchar, char>::value, "now what");

namespace repository
{
template<>
struct declare_gtype_of<void>
{
  static GType get_type() { return G_TYPE_NONE; }
};

#define GI_DECLARE_GTYPE(cpptype, g_type) \
  template<> \
  struct declare_gtype_of<cpptype> \
  { \
    static constexpr GType get_type() { return g_type; } \
  };

#define GI_DECLARE_GTYPE_VALUE(cpptype, g_type, value_suffix) \
  template<> \
  struct declare_gtype_of<cpptype> \
  { \
    static constexpr GType get_type() { return g_type; } \
    static void set_value(GValue *val, cpptype v) \
    { \
      g_value_set_##value_suffix(val, v); \
    } \
    static cpptype get_value(const GValue *val) \
    { \
      return g_value_get_##value_suffix(val); \
    } \
  };

// declare non-cv qualified type
GI_DECLARE_GTYPE_VALUE(gpointer, G_TYPE_POINTER, pointer)
GI_DECLARE_GTYPE_VALUE(bool, G_TYPE_BOOLEAN, boolean)
GI_DECLARE_GTYPE_VALUE(char, G_TYPE_CHAR, schar)
GI_DECLARE_GTYPE_VALUE(unsigned char, G_TYPE_UCHAR, uchar)
GI_DECLARE_GTYPE_VALUE(int, G_TYPE_INT, int)
GI_DECLARE_GTYPE_VALUE(unsigned int, G_TYPE_UINT, uint)
GI_DECLARE_GTYPE_VALUE(long, G_TYPE_LONG, long)
GI_DECLARE_GTYPE_VALUE(unsigned long, G_TYPE_ULONG, ulong)
GI_DECLARE_GTYPE_VALUE(long long, G_TYPE_INT64, int64)
GI_DECLARE_GTYPE_VALUE(unsigned long long, G_TYPE_UINT64, uint64)
GI_DECLARE_GTYPE_VALUE(float, G_TYPE_FLOAT, float)
GI_DECLARE_GTYPE_VALUE(double, G_TYPE_DOUBLE, double)
// some custom set/get for these
// remember; the pointer is non-const
GI_DECLARE_GTYPE(const char *, G_TYPE_STRING)
GI_DECLARE_GTYPE(char *, G_TYPE_STRING)
GI_DECLARE_GTYPE(std::string, G_TYPE_STRING)
GI_DECLARE_GTYPE(gi::cstring, G_TYPE_STRING)
GI_DECLARE_GTYPE(gi::cstring_v, G_TYPE_STRING)

#undef GI_DECLARE_GTYPE_VALUE
#undef GI_DECLARE_GTYPE

} // namespace repository

namespace detail
{
// GValue helpers

// set_value
template<typename T,
    typename std::enable_if<traits::gvalue<T>::value>::type * = nullptr>
inline void
set_value(GValue *val, T v)
{
  traits::gvalue<T>::set(val, v);
}

inline void
set_value(GValue *val, const std::string &s)
{
  g_value_set_string(val, s.c_str());
}

inline void
set_value(GValue *val, gi::cstring_v s)
{
  g_value_set_string(val, s.c_str());
}

inline void
set_value(GValue *val, const char *s)
{
  g_value_set_string(val, s);
}

template<typename T,
    typename std::enable_if<traits::is_object<T>::value>::type * = nullptr>
inline void
set_value(GValue *val, T v)
{
  // set might not handle NULL case
  g_value_take_object(val, v.gobj_copy_());
}

template<typename T,
    typename std::enable_if<traits::is_gboxed<T>::value>::type * = nullptr>
inline void
set_value(GValue *val, T v)
{
  // set might not handle NULL case
  g_value_take_boxed(val, v.gobj_copy_());
}

// container case
template<typename T, typename T::_detail::DataType * = nullptr>
inline void
set_value(GValue *val, T v)
{
  v._set_value(val);
}

// get_value
template<typename T,
    typename std::enable_if<traits::is_object<T>::value>::type * = nullptr>
inline T
get_value(const GValue *val)
{
  // ensure sanity
  static_assert(std::is_class<T>::value && !std::is_const<T>::value,
      "non cv-qualified class type required");
  auto cv =
      static_cast<typename traits::ctype<T>::type>(g_value_dup_object(val));
  if (cv && !g_type_is_a(G_OBJECT_TYPE(cv), traits::gtype<T>::get_type()))
    detail::try_throw(transform_error(G_OBJECT_TYPE(cv)));
  return gi::wrap(cv, transfer_full);
}

template<typename T,
    typename std::enable_if<traits::is_gboxed<T>::value &&
                            !traits::is_reftype<T>::value>::type * = nullptr>
inline T
get_value(const GValue *val)
{
  // no way to know whether boxed type is correct
  // ensure sanity
  static_assert(std::is_class<T>::value && !std::is_const<T>::value,
      "non cv-qualified class type required");
  auto cv =
      static_cast<typename traits::ctype<T>::type>(g_value_dup_boxed(val));
  return gi::wrap(cv, transfer_full);
}

template<typename T,
    typename std::enable_if<traits::is_gboxed<T>::value &&
                            traits::is_reftype<T>::value>::type * = nullptr>
inline T
get_value(const GValue *val)
{
  // no way to know whether boxed type is correct
  // ensure sanity
  static_assert(std::is_class<T>::value && !std::is_const<T>::value,
      "non cv-qualified class type required");
  auto cv =
      static_cast<typename traits::ctype<T>::type>(g_value_get_boxed(val));
  return gi::wrap(cv, transfer_none);
}

template<typename T,
    typename std::enable_if<traits::gvalue<T>::value>::type * = nullptr>
inline T
get_value(const GValue *val)
{
  return traits::gvalue<T>::get(val);
}

// sigh ...
template<typename T,
    typename std::enable_if<std::is_same<T, std::string>::value ||
                            std::is_base_of<detail::String, T>::value>::type * =
        nullptr>
inline T
get_value(const GValue *val)
{
  return gi::wrap(g_value_get_string(val), transfer_none);
}

// container case
template<typename T, typename T::_detail::DataType * = nullptr>
inline T
get_value(const GValue *val)
{
  static_assert(traits::is_decayed<T>::value, "");
  return T::template _get_value<T>(val);
}

// convenience helper ...
template<typename T,
    typename std::enable_if<std::is_same<T, void>::value>::type * = nullptr>
inline T
get_value(const GValue * /*val*/)
{}

// simple (RAII) Value wrapper for (internal) use
struct Value : public GValue, noncopyable
{
  void clear() { memset((void *)this, 0, sizeof(*this)); }

  Value() { clear(); }

  template<typename T, typename Enable = disable_if_same_or_derived<T, Value>>
  explicit Value(T &&v)
  {
    clear();
    g_value_init(this, traits::gtype<T>::get_type());
    set_value(this, std::forward<T>(v));
  }

  template<typename T>
  void init()
  {
    // handle no-op void (return value) corner case
    const GType tp = traits::gtype<T>::get_type();
    if (tp != G_TYPE_NONE)
      g_value_init(this, tp);
  }

  // let's not copy, but ok to move around
  Value(Value &&other)
  {
    memcpy((void *)this, &other, sizeof(*this));
    other.clear();
  }

  Value &operator=(Value &&other)
  {
    if (this != &other) {
      memcpy((void *)this, &other, sizeof(*this));
      other.clear();
    }
    return *this;
  }

  ~Value()
  {
    if (G_VALUE_TYPE(this))
      g_value_unset(this);
  }
};

// we really rely upon this as part of the ABI
// justifies operations above and some explicit casts above
// (to avoid -Wclass-memaccess)
static_assert(sizeof(Value) == sizeof(GValue), "unsupported compiler");

template<typename R>
inline R
transform_value(const GValue *val)
{
  detail::Value dest;
  dest.init<R>();
  if (!g_value_transform(val, &dest))
    detail::try_throw(detail::transform_error(G_VALUE_TYPE(&dest)));
  return detail::get_value<R>(&dest);
}

// hand-crafted Value wrapper with an interface as it would be generated
// and used by generated code, along with additional convenience
class ValueBase : public gi::detail::GBoxedWrapperBase<ValueBase, GValue>
{
  using self_type = ValueBase;

public:
  static GType get_type_() G_GNUC_CONST { return G_TYPE_VALUE; }

  void copy(self_type dest) const { g_value_copy(gobj_(), dest.gobj_()); }

  void reset() { g_value_reset(gobj_()); }

  void unset() { g_value_unset(gobj_()); }

  bool transform(self_type dest) const
  {
    return g_value_transform(gobj_(), dest.gobj_());
  }

  static bool type_compatible(GType src_type, GType dest_type)
  {
    return g_value_type_compatible(src_type, dest_type);
  }

  static bool type_transformable(GType src_type, GType dest_type)
  {
    return g_value_type_transformable(src_type, dest_type);
  }

  template<typename T>
  self_type &set_value(T &&v)
  {
    detail::set_value(gobj_(), std::forward<T>(v));
    return *this;
  }

  template<typename T>
  T get_value() const
  {
    return detail::get_value<T>(gobj_());
  }

  template<typename R>
  R transform_value()
  {
    return detail::transform_value<R>(gobj_());
  }
};

} // namespace detail

namespace repository
{
namespace GObject
{
// build on above base with additional convenience (in owning case)
class Value_Ref;
class Value : public gi::detail::GBoxedWrapper<Value, GValue, detail::ValueBase,
                  Value_Ref>
{
  typedef gi::detail::GBoxedWrapper<Value, GValue, detail::ValueBase, Value_Ref>
      super_type;
  typedef Value self_type;

public:
  using detail::ValueBase::copy;
  using super_type::copy;

  // hybrid GBoxed/CBoxed
  void allocate_()
  {
    if (this->data_)
      return;
    // make sure we match GValue boxed allocation with boxed free
    // (though last kown implementation uses g_new0/g_free)
    detail::Value tmp;
    this->data_ = (::GValue *)g_boxed_copy(G_TYPE_VALUE, &tmp);
  }

  // convenience
  Value() { allocate_(); }

  // allow non-explicit use for convenient calling
  // but avoid copy/move construct use
  template<typename T,
      typename std::enable_if<!std::is_base_of<Value,
          typename std::remove_reference<T>::type>::value>::type * = nullptr>
  Value(T &&t)
  {
    allocate_();
    init<T>(std::forward<T>(t));
  }

  Value &init(GType tp)
  {
    // no-op void corner case
    if (tp != G_TYPE_NONE)
      g_value_init(gobj_(), tp);
    return *this;
  }

  template<typename T>
  Value &init(T &&v)
  {
    g_value_init(gobj_(), traits::gtype<T>::get_type());
    set_value(std::forward<T>(v));
    return *this;
  }
};

class Value_Ref
    : public gi::detail::GBoxedRefWrapper<Value, ::GValue, detail::ValueBase>
{
  typedef gi::detail::GBoxedRefWrapper<Value, ::GValue, detail::ValueBase>
      super_type;
  using super_type::super_type;
};

} // namespace GObject

template<>
struct declare_cpptype_of<GValue>
{
  typedef GObject::Value type;
};

} // namespace repository

} // namespace gi

#endif // GI_VALUE_HPP
