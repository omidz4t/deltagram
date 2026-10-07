#ifndef _GI_GLIB_MAINLOOP_EXTRA_DEF_HPP_
#define _GI_GLIB_MAINLOOP_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace GLib
{
namespace base
{
class MainLoopExtra : public GI_GLIB_MAINLOOP_BASE
{
  typedef GI_GLIB_MAINLOOP_BASE super;

public:
  // preserve existing
  using super::new_;

  // add another convenience one
  static GI_INLINE_DECL gi::rv<GLib::MainLoop>::type new_()
      GI_NOEXCEPT_DECL(true);

}; // class

#undef GI_GLIB_MAINLOOP_BASE
#define GI_GLIB_MAINLOOP_BASE base::MainLoopExtra

} // namespace base

} // namespace GLib

} // namespace repository

} // namespace gi

#endif
