#ifndef _GI_GST_BIN_EXTRA_DEF_HPP_
#define _GI_GST_BIN_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gst
{
namespace base
{
class BinExtra : public GI_GST_BIN_BASE
{
  typedef GI_GST_BIN_BASE super;

public:
  using super::add;

  // Syntax sugar add with multiple arguments to reflect a non-introspectable gst_bin_add_many
  template <typename ...Args>
  GI_INLINE_DECL void add(Gst::Element arg0, Args ...args) GI_NOEXCEPT_DECL(true) {
    super::add(gi::object_cast<Gst::Element>(arg0));
    // We don't need extra code to stop the recursion because of `using
    // super::add` above. When the number of arguments reaches 1, the base class
    // `add` will be called.
    add(args...);
  }

}; // class

#undef GI_GST_BIN_BASE
#define GI_GST_BIN_BASE base::BinExtra

} // namespace base

} // namespace Gst

} // namespace repository

} // namespace gi


#endif
