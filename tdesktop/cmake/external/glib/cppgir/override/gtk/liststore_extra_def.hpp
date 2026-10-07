#ifndef _GI_GTK_LIST_STORE_EXTRA_DEF_HPP_
#define _GI_GTK_LIST_STORE_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gtk
{
namespace base
{
class ListStoreExtra : public GI_GTK_LISTSTORE_BASE
{
  typedef GI_GTK_LISTSTORE_BASE super;

public:
  template<typename... Args>
  static GI_INLINE_DECL Gtk::ListStore new_type_() noexcept;

}; // class

#undef GI_GTK_LISTSTORE_BASE
#define GI_GTK_LISTSTORE_BASE base::ListStoreExtra

} // namespace base

} // namespace Gtk

} // namespace repository

} // namespace gi

#endif
