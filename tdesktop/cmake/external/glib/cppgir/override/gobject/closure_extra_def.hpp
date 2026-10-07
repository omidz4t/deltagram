#ifndef _GI_GOBJECT_CLOSURE_EXTRA_DEF_HPP_
#define _GI_GOBJECT_CLOSURE_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace GObject
{
class Closure;

namespace base
{
class ClosureExtra : public GI_GOBJECT_CLOSURE_BASE
{
  typedef GI_GOBJECT_CLOSURE_BASE super;

public:
  template<typename Wrapper>
  static GClosure *from_wrapper(Wrapper *w)
  {
    auto closure = g_cclosure_new(
        (GCallback)&w->wrapper, w, (GClosureNotify)(GCallback)&w->destroy);
    g_closure_sink(g_closure_ref(closure));
    g_closure_set_marshal(closure, g_cclosure_marshal_generic);
    return closure;
  }

  // add some convenience helper to create a Closure
  // Closure is incomplete type at this stage, so use Ret to avoid complaints

  // Functor is a callable F = C++ signature
  // where all arguments are implicitly assumed transfer_none
  template<typename F, typename Functor, typename Ret = Closure>
  static Ret from_functor(Functor &&f)
  {
    auto w =
        new gi::detail::transform_signal_wrapper<F>(std::forward<Functor>(f));
    return gi::wrap(from_wrapper(w), gi::transfer_full);
  }

  // Callback is a generated callback type
  template<typename Callback, typename Ret = Closure>
  static Ret from_callback(Callback &&cb)
  {
    auto w = gi::unwrap(cb, gi::scope_notified);
    return gi::wrap(from_wrapper(w), gi::transfer_full);
  }
}; // class

#undef GI_GOBJECT_CLOSURE_BASE
#define GI_GOBJECT_CLOSURE_BASE base::ClosureExtra

} // namespace base

} // namespace GObject

} // namespace repository

} // namespace gi

#endif
