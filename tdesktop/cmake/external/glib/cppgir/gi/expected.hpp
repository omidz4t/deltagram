#ifndef GI_EXPECTED_HPP
#define GI_EXPECTED_HPP

#include "base.hpp"
#include "exception.hpp"

GI_MODULE_EXPORT
namespace gi
{
// alias so we might route to a std type some day ...
template<typename T, typename E>
#ifdef expected_lite_VERSION
using expected = nonstd::expected<T, E>;
#else
using expected = std::expected<T, E>;
#endif

template<typename E>
#ifdef expected_lite_VERSION
using unexpected = nonstd::unexpected_type<E>;
#else
using unexpected = std::unexpected<E>;
#endif

// standardize on glib error
template<typename T>
using result = expected<T, repository::GLib::Error>;

namespace detail
{
// only use nonstd if it does not delegate to std (in incomplete way)
#if defined(expected_lite_VERSION) && !nsel_USES_STD_EXPECTED
inline unexpected<repository::GLib::Error>
make_unexpected(repository::GLib::Error error)
{
  assert(error);
  return nonstd::make_unexpected(std::move(error));
}
#else
inline unexpected<repository::GLib::Error>
make_unexpected(repository::GLib::Error error)
{
  assert(error);
  return std::unexpected<repository::GLib::Error>(std::move(error));
}
#endif

inline unexpected<repository::GLib::Error>
make_unexpected(GError *error)
{
  assert(error);
  return make_unexpected(repository::GLib::Error(error));
}
} // namespace detail

// no forwarding reference; T must be non-reference type
template<typename T>
result<T>
make_result(T t, GError *error)
{
  if (error)
    return detail::make_unexpected(error);
  return t;
}

// rough helpers to unwrap result/expected
// unwrap by move
template<typename T>
T
expect(gi::result<T> &&t)
{
  if (!t) {
    g_critical("error result %s", t.error().what());
    detail::try_throw(std::move(t.error()));
  }
  return std::move(*t);
}

namespace detail
{
template<typename T>
void test_result(const gi::result<T> &);
template<typename T>
int test_result(const T &);
template<typename T>
using is_result = std::is_same<void,
    decltype(test_result(std::forward<T>(std::declval<T>())))>;
} // namespace detail

// should only be used for a non-result
// (e.g. avoid l-value result ending up here)
template<typename T, typename Enable = typename std::enable_if<
                         !detail::is_result<T>::value>::type>
T
expect(T &&t)
{
  return std::forward<T>(t);
}

template<typename T>
struct rv
{
#if GI_DL && GI_EXPECTED
  using type = gi::result<T>;
#else
  using type = T;
#endif
};

} // namespace gi

#endif // GI_EXPECTED_HPP
