#ifndef _GI_GST_ELEMENT_EXTRA_DEF_HPP_
#define _GI_GST_ELEMENT_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gst
{
namespace base
{
class ElementExtra : public GI_GST_ELEMENT_BASE
{
  typedef GI_GST_ELEMENT_BASE super;

  GI_INLINE_DECL bool link() GI_NOEXCEPT_DECL(true)
  {
    return true;
  }

public:
  // Syntax sugar link with multiple arguments to reflect a non-introspectable gst_element_link_many
  template <typename Arg0, typename ...Args>
  GI_INLINE_DECL bool link(Arg0 arg0, Args ...args) GI_NOEXCEPT_DECL(true)
  {
    if (super::link(gi::object_cast<Gst::Element>(arg0))) {
        return arg0.link(args...);
    } else {
      return false;
    }
  }

}; // class

#undef GI_GST_ELEMENT_BASE
#define GI_GST_ELEMENT_BASE base::ElementExtra

} // namespace base

} // namespace Gst

} // namespace repository

} // namespace gi


#endif
