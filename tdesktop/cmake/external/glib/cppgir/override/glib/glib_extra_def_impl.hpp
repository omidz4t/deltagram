#ifndef _GI_GLIB_GLIB_EXTRA_DEF_IMPL_HPP_
#define _GI_GLIB_GLIB_EXTRA_DEF_IMPL_HPP_

namespace gi
{
namespace repository
{
namespace GLib
{
gi::rv<guint>::type
idle_add(GLib::SourceFunc function) GI_NOEXCEPT_DECL(true)
{
  return idle_add(GLib::PRIORITY_DEFAULT_IDLE_, function);
}

gi::rv<guint>::type
idle_add_once(std::function<void()> function) GI_NOEXCEPT_DECL(true)
{
  // clang-format off
  return idle_add([=] { function(); return false; });
  // clang-format on
}

gi::rv<guint>::type
timeout_add_seconds(guint interval, GLib::SourceFunc function)
    GI_NOEXCEPT_DECL(true)
{
  return timeout_add_seconds(GLib::PRIORITY_DEFAULT_, interval, function);
}

gi::rv<guint>::type
timeout_add_seconds_once(guint interval, std::function<void()> function)
    GI_NOEXCEPT_DECL(true)
{
  // clang-format off
  return timeout_add_seconds(interval, [=] { function(); return false; });
  // clang-format on
}

gi::rv<guint>::type
timeout_add(guint interval, GLib::SourceFunc function) GI_NOEXCEPT_DECL(true)
{
  return timeout_add(GLib::PRIORITY_DEFAULT_, interval, function);
}

gi::rv<guint>::type
timeout_add_once(guint interval, std::function<void()> function)
    GI_NOEXCEPT_DECL(true)
{
  // clang-format off
  return timeout_add(interval, [=] { function(); return false; });
  // clang-format on
}

} // namespace GLib

} // namespace repository

} // namespace gi

#endif
