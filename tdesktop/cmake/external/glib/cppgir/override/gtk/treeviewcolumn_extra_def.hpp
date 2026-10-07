#ifndef _GI_GTK_TREEVIEWCOLUMN_EXTRA_DEF_HPP_
#define _GI_GTK_TREEVIEWCOLUMN_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gtk
{
namespace base
{
class TreeViewColumnExtra : public GI_GTK_TREEVIEWCOLUMN_BASE
{
  typedef GI_GTK_TREEVIEWCOLUMN_BASE super;

public:
  static GI_INLINE_DECL Gtk::TreeViewColumn new_(const std::string title = "",
      Gtk::CellRenderer = nullptr,
      const std::map<std::string, int> = {}) noexcept;

}; // class

#undef GI_GTK_TREEVIEWCOLUMN_BASE
#define GI_GTK_TREEVIEWCOLUMN_BASE base::TreeViewColumnExtra

} // namespace base

} // namespace Gtk

} // namespace repository

} // namespace gi

#endif
