#ifndef GI_BASE_HPP
#define GI_BASE_HPP

#include "gi_inc.hpp"

#include "boxed.hpp"
#include "objectbase.hpp"

// un-inline some glib parts
// (otherwise they have internal linkage and not usable in non-TU-local context)
#ifdef GI_MODULE_IN_INTERFACE
#ifdef g_strdup
#undef g_strdup
#endif
#endif

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
inline std::string
exception_desc(const std::exception &e)
{
  auto desc = e.what();
  return desc ? desc : typeid(e).name();
}

inline std::string
exception_desc(...)
{
  return "[unknown]";
}

template<typename E>
[[noreturn]] inline void
try_throw(E &&e)
{
#if GI_CONFIG_EXCEPTIONS
  throw std::forward<E>(e);
#else
  g_critical("no throw exception; %s", exception_desc(e).c_str());
  abort();
#endif
}

// constructor does not appreciate NULL, so wrap that here
// map NULL to empty string; not quite the same, but it will do
inline std::string
make_string(const char *s)
{
  return std::string(s ? s : "");
}

// helper string subtype
// used to overload unwrap of optional string argument
// (transfrom empty string to null)
// NOTE std::optional requires C++17
class optional_string : public std::string
{};

class noncopyable
{
public:
  noncopyable() {}
  noncopyable(const noncopyable &) = delete;
  noncopyable &operator=(const noncopyable &) = delete;

  noncopyable(noncopyable &&) = default;
  noncopyable &operator=(noncopyable &&) = default;
};

class scope_guard : public noncopyable
{
private:
  std::function<void()> cleanup_;

public:
  scope_guard(std::function<void()> &&cleanup) : cleanup_(std::move(cleanup)) {}

  ~scope_guard() noexcept(false)
  {
#if GI_CONFIG_EXCEPTIONS
#if __cplusplus >= 201703L
    auto pending = std::uncaught_exceptions();
#else
    auto pending = std::uncaught_exception();
#endif
    try {
#endif
      cleanup_();
#if GI_CONFIG_EXCEPTIONS
    } catch (...) {
      if (!pending)
        throw;
    }
#endif
  }
};

// as in
// http://ericniebler.com/2013/08/07/universal-references-and-the-copy-constructo/
template<typename A, typename B>
using disable_if_same_or_derived = typename std::enable_if<
    !std::is_base_of<A, typename std::remove_reference<B>::type>::value>::type;
} // namespace detail

namespace repository
{
// class types declare c type within class
// others can do so using this (e.g. enum)
template<typename CppType>
struct declare_ctype_of
{};

// and for all cases the reverse cpp type
template<typename CType>
struct declare_cpptype_of
{};

// generate code must specialize appropriately
template<typename T>
struct is_enumeration : public std::false_type
{};

template<typename T>
struct is_bitfield : public std::false_type
{};

} // namespace repository

struct transfer_full_t;
struct transfer_none_t;

namespace traits
{
template<typename T, typename U = void>
struct if_valid_type
{
  typedef U type;
};

template<typename, typename = void>
struct is_type_complete : public std::false_type
{};

template<typename T>
struct is_type_complete<T, typename if_valid_type<decltype(sizeof(
                               typename std::decay<T>::type))>::type>
    : public std::true_type
{};

template<typename T>
using is_decayed = std::is_same<typename std::decay<T>::type, T>;

template<typename T>
using is_cboxed =
    typename std::conditional<std::is_base_of<detail::CBoxed, T>::value,
        std::true_type, std::false_type>::type;

template<typename T>
using is_gboxed =
    typename std::conditional<std::is_base_of<detail::GBoxed, T>::value,
        std::true_type, std::false_type>::type;

template<typename T>
using is_boxed =
    typename std::conditional<std::is_base_of<detail::Boxed, T>::value,
        std::true_type, std::false_type>::type;

// avoid derived cases
template<typename T>
using is_object =
    typename std::conditional<std::is_base_of<detail::ObjectBase, T>::value &&
                                  sizeof(T) == sizeof(gpointer),
        std::true_type, std::false_type>::type;

template<typename T>
using is_wrapper =
    typename std::conditional<std::is_base_of<detail::wrapper_tag, T>::value &&
                                  sizeof(T) == sizeof(gpointer),
        std::true_type, std::false_type>::type;

// bring in to this namespace
using repository::is_bitfield;
using repository::is_enumeration;

// aka passthrough
template<typename T>
using is_basic =
    typename std::conditional<std::is_same<T, gpointer>::value ||
                                  std::is_same<T, gconstpointer>::value ||
                                  std::is_arithmetic<T>::value,
        std::true_type, std::false_type>::type;

// almost passthrough (on lower level at least)
template<typename T>
using is_plain = typename std::conditional<traits::is_basic<T>::value ||
                                               std::is_enum<T>::value,
    std::true_type, std::false_type>::type;

template<typename T, typename E = void>
struct is_reftype : public std::false_type
{};

template<typename T>
struct is_reftype<T,
    typename if_valid_type<typename std::decay<T>::type::BoxType>::type>
    : public std::true_type
{};

template<typename T, typename Enable = void>
struct has_ctype_member : public std::false_type
{};

template<typename T>
struct has_ctype_member<T,
    typename if_valid_type<typename T::BaseObjectType>::type>
    : public std::true_type
{};

// return corresponding c type (if any)
// (string and basic type not considered)
// preserve const
template<typename T, typename Enable = void>
struct ctype
{};

// class case
template<typename T>
struct ctype<T,
    typename if_valid_type<typename std::decay<T>::type::BaseObjectType>::type>
{
  typedef typename std::remove_reference<T>::type CppType;
  // make sure; avoid subclassed cases
  static_assert(is_wrapper<CppType>::value || is_boxed<CppType>::value,
      "must be object or boxed wrapper");
  typedef typename CppType::BaseObjectType CType;
  typedef typename std::conditional<std::is_const<CppType>::value, const CType,
      CType>::type *type;
};

// remaining cases
template<typename T>
struct ctype<T, typename if_valid_type<
                    typename repository::declare_ctype_of<T>::type>::type>
{
  typedef typename repository::declare_ctype_of<T>::type CType;
  typedef typename std::conditional<std::is_const<T>::value, const CType,
      CType>::type type;
};

// basic cases passthrough
template<typename T>
struct ctype<T,
    typename std::enable_if<(std::is_fundamental<T>::value &&
                                !std::is_same<T, bool>::value) ||
                            std::is_same<T, gpointer>::value ||
                            std::is_same<T, gconstpointer>::value>::type>
{
  typedef T type;
};

// ... exception though for bool
template<>
struct ctype<bool, void>
{
  typedef gboolean type;
};

// as used in callback signatures
// or in list (un)wrapping
template<>
struct ctype<const std::string &, void>
{
  typedef const char *type;
};
template<>
struct ctype<std::string, void>
{
  typedef char *type;
};

template<typename T1, typename T2>
struct ctype<std::pair<T1, T2>>
{
  typedef std::pair<typename ctype<T1>::type, typename ctype<T2>::type> type;
};

// conversely
// return corresponding cpp type (if known)
// (string and basic type not considered)
// preserve const
template<typename T, typename Transfer = transfer_full_t,
    typename Enable = void>
struct cpptype
{};

// generic
template<typename T>
struct cpptype<T *, transfer_full_t,
    typename if_valid_type<typename repository::declare_cpptype_of<
        typename std::remove_const<T>::type>::type>::type>
{
  typedef typename repository::declare_cpptype_of<
      typename std::remove_const<T>::type>::type CppType;
  typedef typename std::conditional<std::is_const<T>::value, const CppType,
      CppType>::type type;
};

template<typename T>
struct cpptype<T, transfer_full_t,
    typename if_valid_type<typename repository::declare_cpptype_of<
        typename std::remove_const<T>::type>::type>::type>
{
  typedef typename repository::declare_cpptype_of<
      typename std::remove_const<T>::type>::type CppType;
  typedef typename std::conditional<std::is_const<T>::value, const CppType,
      CppType>::type type;
};

// basic cases passthrough
template<typename T>
struct cpptype<T, transfer_full_t,
    typename std::enable_if<std::is_fundamental<T>::value ||
                            std::is_same<T, gpointer>::value ||
                            std::is_same<T, gconstpointer>::value>::type>
{
  typedef T type;
};

#if 0
template<>
struct cpptype<char *, transfer_full_t>
{
  using type = std::string;
};
#endif

// handle none transfer case
template<typename T>
struct cpptype<T, transfer_none_t>
{
  using CppType = typename cpptype<T, transfer_full_t>::type;
  template<typename TT, typename Enable = void>
  struct map_type
  {
    using type = TT;
  };
  template<typename TT>
  struct map_type<TT, typename if_valid_type<typename TT::ReferenceType>::type>
  {
    using type = typename TT::ReferenceType;
  };
  using type = typename map_type<CppType>::type;
};

// map owning box type to corresponding reference box type
template<typename T>
struct reftype
{
  typedef typename T::ReferenceType type;
};

} // namespace traits

// specify transfer type when (un)wrapping
// this approach is safer than some booleans and allows overload combinations
struct transfer_t
{
  const int value;
  constexpr explicit transfer_t(int v = 0) : value(v) {}
};
struct transfer_none_t : public transfer_t
{
  constexpr transfer_none_t() : transfer_t(0) {}
};
struct transfer_full_t : public transfer_t
{
  constexpr transfer_full_t() : transfer_t(1) {}
};
struct transfer_container_t : public transfer_t
{
  constexpr transfer_container_t() : transfer_t(2) {}
};

GI_MODULE_INLINE const constexpr transfer_t transfer_dummy = transfer_t();
GI_MODULE_INLINE const constexpr transfer_none_t transfer_none =
    transfer_none_t();
GI_MODULE_INLINE const constexpr transfer_full_t transfer_full =
    transfer_full_t();
GI_MODULE_INLINE const constexpr transfer_container_t transfer_container =
    transfer_container_t();

template<typename Transfer>
struct element_transfer
{};
template<>
struct element_transfer<transfer_none_t>
{
  typedef transfer_none_t type;
};
template<>
struct element_transfer<transfer_full_t>
{
  typedef transfer_full_t type;
};
template<>
struct element_transfer<transfer_container_t>
{
  typedef transfer_none_t type;
};

// unwrapping a callback
// specify call scope type
struct scope_t
{
  const int value;
  constexpr explicit scope_t(int v = 0) : value(v) {}
};
struct scope_call_t : public scope_t
{
  constexpr scope_call_t() : scope_t(0) {}
};
struct scope_async_t : public scope_t
{
  constexpr scope_async_t() : scope_t(1) {}
};
struct scope_notified_t : public scope_t
{
  constexpr scope_notified_t() : scope_t(2) {}
};

GI_MODULE_INLINE const constexpr scope_t scope_dummy = scope_t();
GI_MODULE_INLINE const constexpr scope_call_t scope_call = scope_call_t();
GI_MODULE_INLINE const constexpr scope_async_t scope_async = scope_async_t();
GI_MODULE_INLINE const constexpr scope_notified_t scope_notified =
    scope_notified_t();

// (dummy) helper tag to aid in overload resolution
template<typename Interface>
struct interface_tag
{
  typedef Interface type;
};

// CallArgs minimal helper type
// only provide what is needed
// not intended for general use, even though not in internal namespace
template<typename T>
class required
{
  T data_;

public:
  constexpr required(T v) : data_(std::move(v)) {}
  constexpr operator T &() &noexcept { return data_; }
  constexpr operator T &&() &&noexcept { return std::move(data_); }
  constexpr T &value() &noexcept { return data_; }
  constexpr T &&value() &&noexcept { return std::move(data_); }
};

// helper tag type to aid in overload resolution of CallArgs
// this is used as the first argument of a (non-plain) signature variant
// (since it may otherwise have only 1 struct argument, like the plain variant)
template<typename... CA_TAG>
using ca = std::tuple<CA_TAG...>;

struct ca_tag
{};
struct ca_in_tag : public ca_tag
{};
struct ca_bc_tag : public ca_tag
{};
// convenience abbreviation
using ca_in = ca<ca_in_tag>;

#if GI_DL
namespace detail
{
// dynamic load of symbol
inline void *
load_symbol(const std::vector<const char *> libs, const char *symbol)
{
  void *s = nullptr;
  for (const auto &l : libs) {
    auto h = dlopen(l, RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE);
    if (h) {
      s = dlsym(h, symbol);
      dlclose(h);
      if (s)
        break;
    }
  }
  return s;
}

} // namespace detail
#endif // GI_DL

} // namespace gi

#endif // GI_BASE_HPP
