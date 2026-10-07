#ifndef FORMAT_HPP

#if __cplusplus >= 202002L
// available as of gcc-13
// also requires/checks the above guard on C++20

#include <format>
#include <string_view>

namespace fmt
{
template<typename... Args>
inline std::string
format(std::string_view format, Args &&...args)
{
  return std::vformat(format, std::make_format_args(args...));
}
} // namespace fmt

#else
// use original fmtlib
#include <fmt/format.h>
#endif

#endif // FORMAT_HPP
