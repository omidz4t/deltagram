#ifndef _GI_GIO_SIMPLE_ACTION_EXTRA_DEF_HPP_
#define _GI_GIO_SIMPLE_ACTION_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gio
{
class SimpleAction;

namespace base
{
class SimpleActionExtra : public GI_GIO_SIMPLEACTION_BASE
{
  typedef GI_GIO_SIMPLEACTION_BASE super;

public:
  // clang-format off

  // add some signals that are missing due to missing parameter type annotation
  // (at least in older Gio)

  gi::signal_proxy<void(Gio::SimpleAction, GLib::Variant)> signal_activate()
  { return gi::signal_proxy<void(Gio::SimpleAction, GLib::Variant)>(*this, "activate"); }

  gi::signal_proxy<void(Gio::SimpleAction, GLib::Variant)> signal_change_state()
  { return gi::signal_proxy<void(Gio::SimpleAction, GLib::Variant)>(*this, "change-state"); }
  // clang-format on
}; // class

#undef GI_GIO_SIMPLEACTION_BASE
#define GI_GIO_SIMPLEACTION_BASE base::SimpleActionExtra

} // namespace base

} // namespace Gio

} // namespace repository

} // namespace gi

#endif
