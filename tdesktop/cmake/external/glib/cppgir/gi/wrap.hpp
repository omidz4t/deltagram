#ifndef GI_WRAP_HPP
#define GI_WRAP_HPP

#include "base.hpp"
#include "string.hpp"

GI_MODULE_EXPORT
namespace gi
{
// object/wrapper conversion
template<typename CType, typename TransferType,
    typename CppType = typename traits::cpptype<CType *>::type,
    typename Enable =
        typename std::enable_if<traits::is_wrapper<CppType>::value>::type>
inline typename std::remove_const<CppType>::type
wrap(CType *v, const TransferType &t)
{
  // should be called with a concrete transfer subtype
  static_assert(!std::is_same<TransferType, transfer_t>::value, "");
  // the class wrap only has to deal with non-const class type
  typedef typename std::remove_const<CppType>::type TNC;
  return CppType::template wrap<TNC>(v, t.value);
}

// special case; wrap an owned box with full transfer
template<typename CType,
    typename CppType = typename traits::cpptype<CType *>::type,
    typename Enable =
        typename std::enable_if<traits::is_boxed<CppType>::value>::type,
    typename TNC = typename std::remove_const<CppType>::type>
inline TNC
wrap(CType *v, const transfer_full_t &)
{
  return CppType::template wrap<TNC>(v);
}

// special case; wrap a unowned box (that is, transfer none) to the Ref type
template<typename CType,
    typename CppType = typename traits::cpptype<CType *>::type,
    typename Enable =
        typename std::enable_if<traits::is_boxed<CppType>::value>::type,
    typename RefType = typename traits::reftype<
        typename std::remove_const<CppType>::type>::type>
inline RefType
wrap(CType *v, const transfer_none_t &)
{
  // unowned and no copy in all cases
  return RefType::template wrap<RefType>(v);
}

template<typename T,
    typename std::remove_reference<T>::type::BaseObjectType * = nullptr>
inline typename traits::ctype<T>::type
unwrap(T &&v, const transfer_none_t &)
{
  using DT = typename std::decay<T>::type;
  // test convenience
#ifndef GI_TEST
  static constexpr bool ALLOW_ALL = false;
#else
  static constexpr bool ALLOW_ALL = true;
#endif
  static_assert(ALLOW_ALL || traits::is_wrapper<DT>::value ||
                    traits::is_reftype<DT>::value,
      "transfer none expects refcnt wrapper or reftype (not owning box)");
  return v.gobj_();
}

namespace detail
{
// lvalue
template<typename T,
    typename std::enable_if<!traits::is_boxed<T>::value>::type * = nullptr>
inline typename traits::ctype<T>::type
unwrap(const T &v, const transfer_full_t &, std::true_type)
{
  // no implicit copy for boxed; should end up in other case
  static_assert(!traits::is_boxed<T>::value, "boxed copy");
  return v.gobj_copy_();
}

// rvalue
template<typename T,
    typename std::enable_if<!traits::is_reftype<T>::value>::type * = nullptr>
inline typename traits::ctype<T>::type
unwrap(T &&v, const transfer_full_t &, std::false_type)
{
  // in case of wrapper/object;
  // release only provided on base case with void* return
  return (typename traits::ctype<T>::type)v.release_();
}

} // namespace detail

template<typename T, typename std::decay<T>::type::BaseObjectType * = nullptr>
inline typename traits::ctype<T>::type
unwrap(T &&v, const transfer_full_t &t)
{
  // universal reference dispatch
  return detail::unwrap(std::forward<T>(v), t, std::is_lvalue_reference<T>());
}

// container types
template<typename T, typename Transfer,
    typename std::decay<T>::type::_detail::DataType * = nullptr>
inline typename std::decay<T>::type::_detail::DataType
unwrap(T &&v, const Transfer &t)
{
  // universal reference dispatch
  return std::forward<T>(v)._unwrap(t);
}

// to wrap a container, the target wrapped type needs to be explicitly specified
// (in particular the contained element type)
// (target type should be decay'ed type)

// generic case, let wrap take care of it (usually no target type is needed)
template<typename TargetType, typename CType, typename Transfer,
    decltype(wrap(std::declval<typename std::decay<CType>::type>(),
        Transfer())) * = nullptr>
TargetType
wrap_to(CType v, const Transfer &t)
{
  static_assert(traits::is_decayed<TargetType>::value, "");
  return wrap(v, t);
}

// container case
template<typename TargetType, typename CType, typename Transfer,
    typename TargetType::_detail::DataType * = nullptr>
TargetType
wrap_to(CType v, const Transfer &t)
{
  static_assert(traits::is_decayed<TargetType>::value, "");
  return TargetType::template _wrap<TargetType>(v, t);
}

// container size case
template<typename TargetType, typename CType, typename Transfer,
    typename TargetType::_detail::DataType * = nullptr>
TargetType
wrap_to(CType v, int s, const Transfer &t)
{
  static_assert(traits::is_decayed<TargetType>::value, "");
  return TargetType::template _wrap<TargetType>(v, s, t);
}

#if 0
// string conversion
inline std::string
wrap(const char *v, const transfer_none_t &,
    const direction_t & = direction_dummy)
{
  return detail::make_string(v);
}

// actually should not accept const input (as it makes no sense for full
// transfer) but let's go the runtime way and not mind that too much (code
// generation will warn though)
inline std::string
wrap(const char *v, const transfer_full_t &,
    const direction_t & = direction_dummy)
{
  // a custom type that would allow direct mem transfer might be nice
  // but that might be too nifty and create yet-another-string-type
  std::string s;
  if (v) {
    s = v;
    g_free((char *)v);
  }
  return s;
}
#else
// string conversion
inline gi::cstring_v
wrap(const char *v, const transfer_none_t &)
{
  return cstring_v(v);
}

// actually should not accept const input (as it makes no sense for full
// transfer) but let's go the runtime way and not mind that too much (code
// generation will warn though)
inline gi::cstring
wrap(const char *v, const transfer_full_t &)
{
  // as said, never mind const
  return cstring{(char *)v, transfer_full};
}
#endif

// return const here, as somewhat customary, also
// wrapped function call is force-casted anyway (to const char* parameter)
// FIXME ?? though const is generally rare and it breaks consistency that way
inline const gchar *
unwrap(const std::string &v, const transfer_none_t &)
{
  return v.c_str();
}

inline const gchar *
unwrap(const detail::optional_string &v, const transfer_none_t &)
{
  return v.empty() ? nullptr : v.c_str();
}

template<typename Transfer>
inline const gchar *
unwrap(const detail::cstr<Transfer> &v, const transfer_none_t &)
{
  return v.c_str();
}

// R-value variants of the above, akin to transfer_none from an owning R-value
// bad dangling things would happen
inline const gchar *unwrap(std::string &&v, const transfer_none_t &) = delete;

inline const gchar *unwrap(
    detail::optional_string &&v, const transfer_none_t &) = delete;

template<typename Transfer>
inline const gchar *
unwrap(detail::cstr<Transfer> &&v, const transfer_none_t &)
{
  static_assert(std::is_same<Transfer, transfer_none_t>::value,
      "transfer none expects non-owning type");
  return v.c_str();
}

inline gchar *
unwrap(const std::string &v, const transfer_full_t &)
{
  return g_strdup(v.c_str());
}

inline gchar *
unwrap(const detail::optional_string &v, const transfer_full_t &)
{
  return v.empty() ? nullptr : g_strdup(v.c_str());
}

template<typename Transfer>
inline gchar *
unwrap(const gi::detail::cstr<Transfer> &v, const transfer_full_t &)
{
  return g_strdup(v.c_str());
}

inline gchar *
unwrap(gi::cstring &&v, const transfer_full_t &)
{
  return v.release_();
}

// enum conversion
template<typename T,
    typename std::enable_if<std::is_enum<T>::value>::type * = nullptr>
inline typename traits::cpptype<T>::type
wrap(T v, const transfer_t & = transfer_dummy)
{
  return (typename traits::cpptype<T>::type)v;
}

template<typename T,
    typename std::enable_if<std::is_enum<T>::value>::type * = nullptr>
inline typename traits::ctype<T>::type
unwrap(T v, const transfer_t & = transfer_dummy)
{
  return (typename traits::ctype<T>::type)v;
}

// plain basic pass along
template<typename T,
    typename std::enable_if<traits::is_basic<T>::value>::type * = nullptr>
inline T
wrap(T v, const transfer_t & = transfer_dummy)
{
  return v;
}

template<typename T,
    typename std::enable_if<traits::is_basic<T>::value>::type * = nullptr>
inline T
unwrap(T v, const transfer_t & = transfer_dummy)
{
  return v;
}

// callback conversion
// async or destroy-notify;
// signature forces copy, and std::move is used in unwrap call
template<typename T,
    typename std::remove_reference<T>::type::CallbackWrapperType * = nullptr>
inline typename std::remove_reference<T>::type::CallbackWrapperType
unwrap(T &&v, const transfer_t & = transfer_dummy)
{
  return typename std::remove_reference<T>::type::CallbackWrapperType(
      std::forward<T>(v));
}

// call or destroy-notify scope
template<typename T>
inline typename std::remove_reference<T>::type::template wrapper_type<false> *
unwrap(T &&v, const scope_t &)
{
  return new
      typename std::remove_reference<T>::type::template wrapper_type<false>(
          std::forward<T>(v));
}

// async scope
template<typename T>
inline typename std::remove_reference<T>::type::template wrapper_type<true> *
unwrap(T &&v, const scope_async_t &)
{
  return new
      typename std::remove_reference<T>::type::template wrapper_type<true>(
          std::forward<T>(v));
}

// dynamic GType casting within GObject/interface hierarchy
template<typename T, typename I,
    typename std::enable_if<
        traits::is_object<T>::value &&
        traits::is_object<typename std::decay<I>::type>::value>::type * =
        nullptr>
inline T
object_cast(I &&t)
{
  if (!t || !g_type_is_a(t.gobj_type_(), T::get_type_())) {
    return T();
  } else {
    return wrap((typename T::BaseObjectType *)t.gobj_copy_(), transfer_full);
  }
}

// this utility can be used to arrange for pointer-like const-ness
// that is, a const shared_ptr<T> is still usable like (non-const) T*
// in a way, any wrapper object T acts much like a smart-pointer,
// but as the (code generated) methods are non-const, they are not usable
// if the wrapper object is const (e.g. captured in a lambda)
// this helper object/class can be wrapped around the wrapper (phew)
// to absorb/shield the (outer) `const` (as it behaves as other smart pointers)
template<typename T>
class cs_ptr
{
  T t;

public:
  // rough check; T is expected to be a pointer wrapper
  static_assert(sizeof(T) == sizeof(void *), "");

  // if T not copy-able, argument may need to be move'd
  cs_ptr(T _t) : t(std::move(_t)) {}

  operator T() const & { return t; }
  operator T() && { return std::move(t); }

  T *get() const { return &t; }

  // C++ sacrilege,
  // but the const of pointer in T does not extend to pointee anyway
  T *operator*() const { return const_cast<T *>(&t); }
  T *operator->() const { return const_cast<T *>(&t); }
};

} // namespace gi

#endif // GI_WRAP_HPP
