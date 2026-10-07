#ifndef _GI_GTK_TREEVIEWCOLUMN_EXTRA_DEF_IMPL_HPP_
#define _GI_GTK_TREEVIEWCOLUMN_EXTRA_DEF_IMPL_HPP_

namespace gi
{
namespace repository
{
namespace Gtk
{
namespace base
{
Gtk::TreeViewColumn
TreeViewColumnExtra::new_(const std::string title, Gtk::CellRenderer renderer,
    const std::map<std::string, int> attribs) noexcept
{
  auto result = TreeViewColumnBase::new_();
  result.set_title(title);
  if (renderer) {
    result.pack_start(renderer, true);
    for (auto &e : attribs) {
      result.add_attribute(renderer, e.first, e.second);
    }
  }
  return result;
}

} // namespace base

} // namespace Gtk

} // namespace repository

} // namespace gi

#endif
