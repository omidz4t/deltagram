#ifndef _GI_GLIB_SOURCE_EXTRA_DEF_IMPL_HPP_
#define _GI_GLIB_SOURCE_EXTRA_DEF_IMPL_HPP_

namespace gi
{
namespace repository
{
namespace GLib
{
namespace base
{
template<typename CallbackType>
void
SourceExtra::set_callback(CallbackType callback) noexcept
{
  auto callback_wrap_ = unwrap(std::move(callback), gi::scope_notified);
  g_source_set_callback(this->gobj_(),
      (GSourceFunc)(GCallback)&callback_wrap_->wrapper, callback_wrap_,
      &callback_wrap_->destroy);
}

} // namespace base

} // namespace GLib

} // namespace repository

} // namespace gi

#endif
