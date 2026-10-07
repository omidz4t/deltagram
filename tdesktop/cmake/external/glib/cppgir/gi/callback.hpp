#ifndef GI_CALLBACK_HPP
#define GI_CALLBACK_HPP

#include "base.hpp"
#include "exception.hpp"
#include "wrap.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
#if GI_CONFIG_EXCEPTIONS

inline ::GError **
find_gerror(bool &has_gerror)
{
  has_gerror = false;
  return nullptr;
}

inline ::GError **
find_gerror(bool &has_gerror, ::GError **error)
{
  has_gerror = true;
  return error;
}

template<typename CT, typename... CType>
inline ::GError **
find_gerror(bool &has_gerror, CT /*arg*/, CType... args)
{
  return find_gerror(has_gerror, args...);
}

inline ::GError *
exception_error(const repository::GLib::Error &exc)
{
  return g_error_copy(exc.gobj_());
}

inline ::GError *
exception_error(const repository::GLib::Error_Ref &exc)
{
  return g_error_copy(exc.gobj_());
}

template<typename E>
inline ::GError *
exception_error(const E &exc)
{
  static auto quark = g_quark_from_static_string("gi-error-quark");
  return g_error_new(quark, 0, "%s", exception_desc(exc).c_str());
}

template<bool SILENT = FALSE, typename E, typename... CType>
void
report_exception(const E &exc, CType... args)
{
  // see if we can really report error somewhere
  bool has_gerror = false;
  GError **error = find_gerror(has_gerror, args...);
  if (has_gerror) {
    g_return_if_fail(error == NULL || *error == NULL);
    if (error)
      *error = exception_error(exc);
    // if caller does not need/want error, exception disappears here
  } else {
    // simply report the hard and simple way
    // otherwise catch internally if something else/more is desired
    if (!SILENT) {
      auto msg = std::string("handler exception; ") + exception_desc(exc);
      g_critical("%s", msg.c_str());
    }
  }
}
#endif

// (re)float only applies to GObject
template<typename CppType, typename Transfer,
    typename std::enable_if<!traits::is_object<CppType>::value>::type * =
        nullptr>
auto
unwrap_maybe_float(CppType &&v, const Transfer &t)
{
  return unwrap(std::forward<CppType>(v), t);
}

// (re) float only for transfer none/floating
template<typename CppType,
    typename std::enable_if<traits::is_object<CppType>::value>::type * =
        nullptr>
inline typename traits::ctype<CppType>::type
unwrap_maybe_float(CppType &&v, const transfer_full_t &t)
{
  return unwrap(std::forward<CppType>(v), t);
}

template<typename CppType,
    typename std::enable_if<traits::is_object<CppType>::value>::type * =
        nullptr>
inline typename traits::ctype<CppType>::type
unwrap_maybe_float(CppType &&v, const transfer_none_t &)
{
  // expected called with r-value
  static_assert(!std::is_reference<CppType>::value, "");
  // steal/take from wrapper
  auto result = (typename traits::ctype<CppType>::type)v.release_();
  // the following is essentially a bit of a hack as mentioned in
  // https://bugzilla.gnome.org/show_bug.cgi?id=693393)
  // that is, we are about to return an object to C (from binding/callback)
  // and this should be done with none transfer
  // this none may actually mean floating (e.g. a factory-like callback)
  // but no way to know from annotations
  // so if at runtime the wrapper actually holds the only reference,
  // then it is about to be destroyed (when wrapper goes away)
  // *before* the object can make it back to caller
  // so turn that ref into a floating one (as only that makes sense)
  // if it is not the only ref, it is kept alive elsewhere
  // (as typically so for a "getter" callback)
  // so it is really treated as none
  auto obj = (GObject *)result;
  // theoretically not MT safe, but if == 1, only 1 thread should be involved
  if (obj->ref_count == 1) {
    g_object_force_floating(obj);
  } else {
    // otherwise unref as wrapper would have
    g_object_unref(obj);
  }
  return result;
}

template<typename T>
constexpr T
unconst(T t)
{
  return t;
}

inline char *
unconst(const char *t)
{
  return (char *)t;
}

// helper types to provide additional argument info beyond Transfer
template<std::size_t... index>
struct args_index
{
  static constexpr auto value = std::make_tuple(index...);
  using value_type = decltype(value);
};

template<typename Transfer, bool _inout, typename CustomTraits = void,
    typename ArgsIndex = args_index<>>
struct arg_info
{
  using transfer_type = Transfer;
  static constexpr bool inout = _inout;
  // tuple of index; used to selects the C arguments (to assemble C++ argument)
  using args_type = ArgsIndex;
  // additional info as used by corresponding cb_arg_handler
  using custom_type = CustomTraits;
};

// access above info by forwarding types
template<typename T, typename Enable = void>
struct arg_traits
{
  using transfer_type = typename T::transfer_type;
  static constexpr bool inout = T::inout;
  using args_type = typename T::args_type;
  using custom_type = typename T::custom_type;
};

// legacy case; only transfer type
template<typename T>
struct arg_traits<T,
    typename std::enable_if<std::is_base_of<transfer_t, T>::value>::type>
{
  using transfer_type = T;
  static constexpr bool inout = false;
  using args_type = args_index<>;
  using custom_type = void;
};

// IndexTuple is essentially an args_index<...>
template<typename IndexTuple, std::size_t... Index, typename F,
    typename ArgTuple>
decltype(auto)
apply_with_args(std::index_sequence<Index...>, F &&f, ArgTuple &&args)
{
  return f(std::get<std::get<Index>(IndexTuple::value)>(args)...);
}

template<typename IndexTuple, typename F, typename... Args>
decltype(auto)
apply_with_args(F &&f, Args &&...args)
{
  return apply_with_args<IndexTuple>(
      std::make_index_sequence<
          std::tuple_size<typename IndexTuple::value_type>::value>(),
      std::forward<F>(f), std::forward_as_tuple(args...));
}

// a simple callback has no (need for) args_index
template<typename T>
struct is_simple_cb : public std::true_type
{};

template<typename Transfer, typename... Transfers>
struct is_simple_cb<std::tuple<Transfer, Transfers...>>
{
  static constexpr bool value =
      std::tuple_size<
          typename arg_traits<Transfer>::args_type::value_type>::value == 0 ||
      (sizeof...(Transfers) > 0 &&
          is_simple_cb<std::tuple<Transfers...>>::value);
};

template<typename T, typename CT = void>
struct map_cpp_function;

// handles all calls C -> C++ (callbacks, virtual method calls)
// restrictions though on types supported (enforced by code generation)
template<typename T, typename RetTransfer, typename ArgTransfers,
    typename CT = typename map_cpp_function<T>::type,
    bool SIMPLE = is_simple_cb<ArgTransfers>::value>
struct transform_caller;

// helper used below that provides pre-call and post-call steps
// to handle each argument's conversion to and from C++
// in so-called (most) simple cases, there is one-to-one mapping between
// C and C++ arguments and C++ argument type that allows to deduce context
// (in particular, no callbacks or sized array)
// as such, proper steps can be obtained by specialization on Cpp argument type
template<typename CppArg, typename Enable = void>
struct cb_arg_handler;

// in simple cases, C signature can be derived from C++ signature
template<typename T, typename CT>
struct map_cpp_function
{
  using type = CT;
};

template<typename R, typename... Args>
struct map_cpp_function<R(Args...), void>
{
  using type = typename traits::ctype<R>::type(
      typename cb_arg_handler<Args>::c_type...);
};

// signature used in virtual method handling
template<typename R, typename... Args>
struct map_cpp_function<R (*)(Args...), void>
{
  using type = typename traits::ctype<R>::type(
      typename cb_arg_handler<Args>::c_type...);
};

// NOTE function type R(const A) is identical to R(A)
// so no deduced Args below will retain const (if such)
// in simple cases, *Transfer* is simply a transfer type
// but it may also be a more elaborate argument trait

template<typename R, typename... Args, typename RetTransfer,
    typename... Transfers, typename CR, typename... CArgs, bool SIMPLE>
struct transform_caller<R(Args...), RetTransfer, std::tuple<Transfers...>,
    CR(CArgs...), SIMPLE>
{
  static_assert(sizeof...(Args) == sizeof...(Transfers), "");
  static_assert(!SIMPLE || sizeof...(Args) == sizeof...(CArgs), "");

  typedef transform_caller self_type;
  typedef R (*caller_type)(Args &&..., void *d);

private:
  static R do_call(Args &&...args, caller_type func, void *d)
  {
    return func(std::forward<Args>(args)..., d);
  }

  // helper that provides context for pack expansion below
  static void dummy_call(...){};

  template<typename T>
  static auto _tt(const T &)
  {
    return typename arg_traits<T>::transfer_type();
  }

  // non-void return
  template<typename T, std::size_t... Index,
      typename std::enable_if<SIMPLE && !std::is_void<T>::value>::type * =
          nullptr>
  static CR _wrapper(
      CArgs... args, caller_type func, void *d, std::index_sequence<Index...>)
  {
    std::tuple<cb_arg_handler<Args>...> handlers;
    auto ret = do_call(
        std::get<Index>(handlers).arg(args, _tt(Transfers()), Transfers())...,
        func, d);
    dummy_call((std::get<Index>(handlers).post(args, _tt(Transfers())), 0)...);
    return unconst(unwrap_maybe_float(std::move(ret), RetTransfer()));
  }

  // void return
  template<typename T, std::size_t... Index,
      typename std::enable_if<SIMPLE && std::is_void<T>::value>::type * =
          nullptr>
  static CR _wrapper(
      CArgs... args, caller_type func, void *d, std::index_sequence<Index...>)
  {
    std::tuple<cb_arg_handler<Args>...> handlers;
    do_call(
        std::get<Index>(handlers).arg(args, _tt(Transfers()), Transfers())...,
        func, d);
    dummy_call((std::get<Index>(handlers).post(args, _tt(Transfers())), 0)...);
  }

  // complex; non-void return
  template<typename T, std::size_t... Index,
      typename std::enable_if<!SIMPLE && !std::is_void<T>::value>::type * =
          nullptr>
  static CR _wrapper(
      CArgs... args, caller_type func, void *d, std::index_sequence<Index...>)
  {
    std::tuple<cb_arg_handler<Args>...> handlers;
    auto ret = do_call(
        apply_with_args<typename arg_traits<Transfers>::args_type>(
            [&handlers](auto... selargs) mutable -> decltype(auto) {
              return std::get<Index>(handlers).arg(selargs...,
                  typename arg_traits<Transfers>::transfer_type(), Transfers());
            },
            args...)...,
        func, d);
    dummy_call((apply_with_args<typename arg_traits<Transfers>::args_type>(
                    [&handlers](auto... selargs) mutable -> decltype(auto) {
                      return std::get<Index>(handlers).post(selargs...,
                          typename arg_traits<Transfers>::transfer_type());
                    },
                    args...),
        0)...);
    return unconst(unwrap_maybe_float(std::move(ret), RetTransfer()));
  }

  // complex; void return
  template<typename T, std::size_t... Index,
      typename std::enable_if<!SIMPLE && std::is_void<T>::value>::type * =
          nullptr>
  static CR _wrapper(
      CArgs... args, caller_type func, void *d, std::index_sequence<Index...>)
  {
    std::tuple<cb_arg_handler<Args>...> handlers;
    do_call(apply_with_args<typename arg_traits<Transfers>::args_type>(
                [&handlers](auto... selargs) mutable -> decltype(auto) {
                  return std::get<Index>(handlers).arg(selargs...,
                      typename arg_traits<Transfers>::transfer_type(),
                      Transfers());
                },
                args...)...,
        func, d);
    dummy_call((apply_with_args<typename arg_traits<Transfers>::args_type>(
                    [&handlers](auto... selargs) mutable -> decltype(auto) {
                      return std::get<Index>(handlers).post(selargs...,
                          typename arg_traits<Transfers>::transfer_type());
                    },
                    args...),
        0)...);
  }

public:
  static CR wrapper(CArgs... args, caller_type func, void *d)
  {
    // exceptions should not escape into plain C
#if GI_CONFIG_EXCEPTIONS
    try {
#endif
      return self_type::template _wrapper<R>(
          args..., func, d, std::make_index_sequence<sizeof...(Args)>());
#if GI_CONFIG_EXCEPTIONS
    } catch (const repository::GLib::Error &exc) {
      report_exception(exc, args...);
    } catch (const repository::GLib::Error_Ref &exc) {
      report_exception(exc, args...);
    } catch (const std::exception &exc) {
      report_exception(exc, args...);
    } catch (...) {
      report_exception(nullptr, args...);
    }
    return typename traits::ctype<R>::type();
#endif
  }
};

// minor helper traits used below
namespace _traits
{
template<typename T>
struct remove_all_pointers
{
  using type = T;
};

template<typename T>
struct remove_all_pointers<T *>
{
  using type = typename remove_all_pointers<T>::type;
};

template<typename T>
struct remove_all_pointers<T *const>
{
  using type = typename remove_all_pointers<T>::type;
};

template<typename T>
using is_basic_or_void = typename std::conditional<traits::is_basic<T>::value ||
                                                       std::is_void<T>::value,
    std::true_type, std::false_type>::type;

template<typename T>
using is_basic_argument = typename std::conditional<
    is_basic_or_void<typename std::remove_cv<
        typename remove_all_pointers<T>::type>::type>::value,
    std::true_type, std::false_type>::type;

template<typename T>
using is_const_lvalue = typename std::conditional<
    std::is_lvalue_reference<T>::value &&
        std::is_const<typename std::remove_reference<T>::type>::value,
    std::true_type, std::false_type>::type;

} // namespace _traits

// generic fallback case; assume input parameter
// (also covers boxed callerallocates, which pretty much is/becomes input)
template<typename CppArg, typename Enable>
struct cb_arg_handler
{
  using c_type = typename traits::ctype<CppArg>::type;

  // use generic CType to handle const differences wrt c_type
  template<typename CType, typename Transfer, typename ArgTrait>
  typename std::decay<CppArg>::type arg(
      CType arg, const Transfer &t, const ArgTrait &) noexcept
  {
    // wrap to normalized destination target (no const &)
    // the destination type here is really only relevant for collection cases
    // (since that depends on the contained element)
    // otherwise all the info is pretty much in c_type type
    return wrap_to<typename std::decay<CppArg>::type>(arg, t);
  }

  // minor variation; dynamic sized array input
  // also accepts length input
  template<typename CType, typename Transfer, typename ArgTrait>
  typename std::decay<CppArg>::type arg(
      CType arg, int length, const Transfer &t, const ArgTrait &) noexcept
  {
    // destination also really needed here
    return wrap_to<typename std::decay<CppArg>::type>(arg, length, t);
  }

  void post(...){};
};

// passthrough on (pointers to) basic values;
// handles input/output of basic values, as well as arrays of such
template<typename CppArg>
struct cb_arg_handler<CppArg,
    typename std::enable_if<_traits::is_basic_argument<CppArg>::value>::type>
{
  using c_type = CppArg;

  template<typename CType, typename Transfer, typename ArgTrait>
  CType arg(CType arg, const Transfer &, const ArgTrait &) noexcept
  {
    return arg;
  }
  void post(...){};

  // array size case; inout C int* to in Cpp int
  // (only enable if so tagged by non-default custom trait type)
  template<typename CType, typename Transfer, typename ArgTrait>
  typename std::enable_if<
      !std::is_void<typename arg_traits<ArgTrait>::custom_type>::value,
      CType>::type
  arg(CType *arg, const Transfer &, const ArgTrait &) noexcept
  {
    return *arg;
  }
};

// handle "complex" (in)out argument
// (though also handles e.g. int& case, which might be optimized, but anyways)
// these are recognized/assumed to be a pointer or reference to non-basic type
template<typename CppArg>
struct cb_arg_handler<CppArg,
    typename std::enable_if<
        !_traits::is_basic_argument<CppArg>::value &&
        (std::is_pointer<CppArg>::value ||
            (std::is_lvalue_reference<CppArg>::value &&
                !_traits::is_const_lvalue<CppArg>::value))>::type>
{
  using BaseCppType =
      typename std::decay<typename std::remove_pointer<CppArg>::type>::type;
  // no more pointer expected here
  // (other than for void* cases, which do not introspect well, but anyways)
  static_assert(!std::is_pointer<BaseCppType>::value ||
                    std::is_same<BaseCppType, gpointer>::value ||
                    std::is_same<BaseCppType, gconstpointer>::value,
      "");
  using c_type = typename traits::ctype<BaseCppType>::type *;

  // intermediate helper storage
  BaseCppType var_{};

  // pointer case
  CppArg rv(std::true_type) { return &var_; }
  // ref case
  CppArg rv(std::false_type) { return var_; }

  // handle const variations (e.g. const char**)
  template<typename T, typename X>
  static void assign(T *&t, X val)
  {
    t = const_cast<T *>(val);
  }

  template<typename T, typename X>
  static void assign(T &t, X val)
  {
    t = unconst(val);
  }

  template<typename Transfer, typename ArgTrait>
  CppArg arg(c_type arg, const Transfer &t, const ArgTrait &)
  {
    bool inout = arg_traits<ArgTrait>::inout;
    // a plain type has no special needs
    // so we can always take any bogus value as-is
    // (which is then less sensitive to incorrect annotation)
    if (arg && (inout || traits::is_plain<BaseCppType>::value))
      var_ = wrap_to<BaseCppType>(*arg, t);
    return rv(std::is_pointer<CppArg>());
  }

  // overall function call happens following arg call above
  // post invoked after function call
  template<typename Transfer>
  void post(c_type arg, const Transfer &t)
  {
    if (arg)
      assign(*arg, unwrap_maybe_float(std::move(var_), t));
  }

  // sized array variants; arguments (data, size)
  // latter could be input (int) or (in)out (int*)
  template<typename Transfer, typename Int, typename ArgTrait>
  CppArg arg(c_type arg, Int size, const Transfer &t, const ArgTrait &)
  {
    bool inout = arg_traits<ArgTrait>::inout;
    if (arg && inout)
      var_ = wrap_to<BaseCppType>(*arg, size, t);
    return rv(std::is_pointer<CppArg>());
  }

  template<typename Transfer, typename Int, typename ArgTrait>
  CppArg arg(c_type arg, Int *size, const Transfer &t, const ArgTrait &)
  {
    bool inout = arg_traits<ArgTrait>::inout;
    if (arg && size && inout)
      var_ = wrap_to<BaseCppType>(*arg, *size, t);
    return rv(std::is_pointer<CppArg>());
  }

  template<typename Transfer, typename Int>
  void post(c_type arg, Int, const Transfer &t)
  {
    post(arg, nullptr, t);
  }

  template<typename Transfer, typename Int>
  void post(c_type arg, Int *size, const Transfer &t)
  {
    if (arg)
      assign(*arg, unwrap_maybe_float(std::move(var_), t));
    if (size)
      *size = var_.size();
  }
};

// handles callback argument
// (as argument in anther callback, most likely a virtual method)
template<typename CppArg>
struct cb_arg_handler<CppArg,
    typename std::enable_if<
        std::is_pointer<typename CppArg::cfunction_type>::value>::type>
{
  template<typename CType, typename Transfer, typename ArgTrait>
  CppArg arg(CType cb, gpointer userdata, ::GDestroyNotify destroy,
      const Transfer &, const ArgTrait &) noexcept
  {
    using ct = typename arg_traits<ArgTrait>::custom_type;
    // setup shared pointer to userdata with destroy as Deleter
    auto sp = destroy ? std::shared_ptr<void>(userdata, destroy) : nullptr;
    // keep userdata/destroy alive as long as lambda handler
    auto h = [cb, userdata, sp = std::move(sp)](
                 auto &&...args) -> decltype(auto) {
      // original callback type CType should match deduced handler_cb_tpe
      // but let's make sure as usual
      return ct::handler(std::forward<decltype(args)>(args)...,
          typename ct::handler_cb_type(cb), userdata);
    };
    return h;
  }

  template<typename CType, typename Transfer, typename ArgTrait>
  CppArg arg(CType cb, gpointer userdata, const Transfer &t,
      const ArgTrait &tt) noexcept
  {
    return arg(cb, userdata, nullptr, t, tt);
  }

  void post(...){};
};

class connection_status
{
public:
  struct data
  {
    bool connected{false};
  };

  bool connected() const
  {
    auto sp = data_.lock();
    return sp && sp->connected;
  }

protected:
  std::weak_ptr<data> data_;
};

// callback handling
template<typename G>
class connectable;

template<typename G, bool AUTODESTROY = false>
class callback_wrapper;

template<typename R, typename... Args>
class connectable<R(Args...)>
{
  friend class callback_wrapper<R(Args...)>;
  struct data : public connection_status::data
  {
    template<typename T,
        typename Enable = typename detail::disable_if_same_or_derived<data, T>>
    data(T &&t) : callable(std::forward<T>(t))
    {}

    std::function<R(Args... args)> callable;
  };

  struct connection_status_factory : public connection_status
  {
    connection_status_factory(std::shared_ptr<data> p)
    {
      data_ = std::weak_ptr<connection_status::data>(p);
    }
  };

public:
  // avoid copy constructor mishaps
  template<typename T,
      typename Enable =
          typename detail::disable_if_same_or_derived<connectable, T>>
  connectable(T &&t) : data_(std::make_shared<data>(std::forward<T>(t)))
  {}

  connection_status connection() const
  {
    return connection_status_factory(data_);
  }

  R operator()(Args... args) const
  {
    return data_->callable(std::forward<Args>(args)...);
  }

  explicit operator bool() const { return data_ && data_->callable; }

private:
  // state management by wrapper
  void connected(bool conn) { data_->connected = conn; }
  void disconnect() { data_->connected = false; }

private:
  std::shared_ptr<data> data_;
};

template<typename R, typename... Args, bool AUTODESTROY>
class callback_wrapper<R(Args...), AUTODESTROY>
{
  typedef callback_wrapper self_type;

public:
  template<typename T,
      typename Enable =
          typename detail::disable_if_same_or_derived<callback_wrapper, T>>
  callback_wrapper(T &&t) : _callback(std::forward<T>(t))
  {
    // mark connected now that it is wrapped
    _callback.connected(true);
  }

  // (only) used by manual callback workaround
  static R wrapper(Args... args, gpointer user_data)
  {
    auto t = reinterpret_cast<callback_wrapper *>(user_data);
    std::unique_ptr<self_type> wt(AUTODESTROY ? t : nullptr);
    return t->_callback(args...);
  }

  static void destroy(gpointer user_data)
  {
    auto t = reinterpret_cast<callback_wrapper *>(user_data);
    delete t;
  }

  // (async scope) wrapper may have to take ownership of additional data
  // (other callback wrapper)
  template<typename T>
  void take_data(std::shared_ptr<T> d)
  {
    auto cb = std::move(_callback.data_->callable);
    auto newcb = [d, cb](Args &&...args) {
      return cb(std::forward<Args>(args)...);
    };
    _callback.data_->callable = std::move(newcb);
  }

  template<typename T>
  void take_data(T *d)
  {
    take_data(std::shared_ptr<T>(d));
  }

  ~callback_wrapper()
  {
    // other shared ptr to data might be around (unlikely though)
    // but regardless disconnect now as requested (as wrapper is going away)
    _callback.disconnect();
  }

  connectable<R(Args... args)> _callback;
};

template<typename G, typename CG = typename map_cpp_function<G>::type>
struct transform_callback_wrapper;

template<typename R, typename... Args, typename CR, typename... CArgs>
struct transform_callback_wrapper<R(Args...), CR(CArgs...)>
{
  // transfers of arguments
  template<bool AUTODESTROY, typename RetTransfer, typename... Transfers>
  class with_transfer : public callback_wrapper<R(Args...)>
  {
    typedef callback_wrapper<R(Args...)> super_type;
    typedef with_transfer self_type;

  public:
    template<typename T>
    explicit with_transfer(T &&t) : super_type(std::forward<T>(t))
    {}

  private:
    static R caller(Args &&...args, void *d)
    {
      auto this_ = (self_type *)d;
      return this_->_callback(std::forward<Args>(args)...);
    }

  public:
    static CR wrapper(CArgs... args, gpointer user_data)
    {
      auto t = reinterpret_cast<self_type *>(user_data);
      std::unique_ptr<self_type> wt(AUTODESTROY ? t : nullptr);
      return transform_caller<R(Args...), RetTransfer, std::tuple<Transfers...>,
          CR(CArgs...)>::wrapper(args..., caller, t);
    }
  };
};

// used early in declarations, so avoid using unknown types in CSigOrVoid
template<typename CppSig, typename RetTransfer,
    typename ArgTransfers = std::tuple<>, typename CSigOrVoid = void>
class callback;

template<typename CppSig, typename RetTransfer, typename... Transfers,
    typename CSigOrVoid>
class callback<CppSig, RetTransfer, std::tuple<Transfers...>, CSigOrVoid>
    : public connectable<CppSig>
{
  typedef connectable<CppSig> super_type;

public:
  typedef CppSig function_type;
  typedef typename map_cpp_function<CppSig, CSigOrVoid>::type CSig;

  template<bool ASYNC = false>
  using wrapper_type = typename transform_callback_wrapper<function_type,
      CSig>::template with_transfer<ASYNC, RetTransfer, Transfers...>;

  typedef typename std::add_pointer<decltype(wrapper_type<>::wrapper)>::type
      cfunction_type;

  using super_type::super_type;
};

// signal handling;
// transfer none for arguments
// transfer full for return
template<typename G>
struct transform_signal_wrapper;

template<typename T>
struct signal_arg_transfer
{
  typedef transfer_none_t type;
};

template<typename R, typename... Args>
struct transform_signal_wrapper<R(Args...)>
    : public transform_callback_wrapper<R(
          Args...)>::template with_transfer<false, transfer_full_t,
          typename detail::signal_arg_transfer<Args>::type...>
{
private:
  typedef typename transform_callback_wrapper<R(
      Args...)>::template with_transfer<false, transfer_full_t,
      typename detail::signal_arg_transfer<Args>::type...>
      super_;

public:
  template<typename T>
  transform_signal_wrapper(T &&t) : super_(std::forward<T>(t))
  {}
};

// connection helpers
class connection_impl
{
public:
  connection_impl(gulong id, connection_status s) : id_(id), status_(s) {}

  bool connected() const { return status_.connected(); }

  gulong id() const { return id_; }

protected:
  gulong id_;
  connection_status status_;
};

template<typename Connection>
class connection
{
  typedef Connection impl;

public:
  connection() = default;

  template<typename... Args>
  explicit connection(Args... arg)
      : conn_(std::make_shared<impl>(std::forward<Args>(arg)...))
  {
    // this is to be expected at this time
    if (!connected()) {
      g_warning("creating non-connected connection");
    }
  }

  // implicit copy/move

  bool connected() const { return conn_ && conn_->connected(); }
  void disconnect()
  {
    if (connected())
      conn_->disconnect();
  }

protected:
  std::shared_ptr<impl> conn_;
};

template<typename ConnectionBase>
class scoped_connection : public ConnectionBase
{
  typedef ConnectionBase connection;

public:
  scoped_connection() : connection() {}
  ~scoped_connection() { this->disconnect(); }

  void release() { this->conn_.reset(); }

  // ensure default movable
  scoped_connection(scoped_connection &&other) = default;
  scoped_connection &operator=(scoped_connection &&other) = default;

  // not copyable; to avoid inadvertent disconnect
  scoped_connection(const scoped_connection &other) = delete;
  scoped_connection &operator=(const scoped_connection &other) = delete;

  // but convert/assign from base class
  scoped_connection(const connection &other) : connection(other) {}
  scoped_connection &operator=(const connection &other)
  {
    (*(connection *)(this)) = other;
    return *this;
  }
  scoped_connection &operator=(const connection &&other)
  {
    (*(connection *)(this)) = std::move(other);
    return *this;
  }
};

} // namespace detail

// function bind helpers
template<typename R, typename T, typename Tp, typename... Args>
inline std::function<R(Args...)>
mem_fun(R (T::*pm)(Args...), Tp object)
{
  return [object, pm](Args... args) {
    return (object->*pm)(std::forward<Args>(args)...);
  };
}

template<typename R, typename T, typename Tp, typename... Args>
inline std::function<R(Args...)>
mem_fun(R (T::*pm)(Args...) const, Tp object)
{
  return [object, pm](Args... args) {
    return (object->*pm)(std::forward<Args>(args)...);
  };
}

// expose for use in fallback scenarios
using detail::callback_wrapper;

} // namespace gi

#endif // CALLBACK_HPP
