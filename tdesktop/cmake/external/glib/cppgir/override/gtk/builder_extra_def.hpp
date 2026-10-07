#ifndef _GI_GTK_BUILDER_EXTRA_DEF_HPP_
#define _GI_GTK_BUILDER_EXTRA_DEF_HPP_

namespace gi
{
namespace repository
{
namespace Gtk
{
class Builder;

namespace base
{
class BuilderExtra : public GI_GTK_BUILDER_BASE
{
  typedef GI_GTK_BUILDER_BASE super;

public:
  using super::get_object;

  // T must be object-side type (not impl type)
  template<typename T>
  T get_object(const gi::cstring_v name)
  {
    auto obj = super::get_object(name);
    return gi::object_cast<T>(obj);
  }

  /*
   * T_Derived should be a custom subclass of some widget/object BaseClass.
   * It should have a constructor of following signature that delegates
   * to use the latter's special constructor (see also gtk example);
   *
   * T_Derived(BaseClass::instance_type instance, Gtk::Builder) :
   *    BaseClass(instance, this) {}
   *
   * Of course, it may also have additional arguments, and specify some
   * additional arguments to super class constructor, if needed.
   * See also comments on the latter.
   *
   * Note that this function *should* be called at some stage for any created
   * C++-type based widget to ensure full and proper "C++ side" setup.
   * See also gtk example (for additional comments).
   */
  template<typename T_Derived, typename... Args>
  gi::ref_ptr<T_Derived> get_object_derived(
      const gi::cstring_v name, Args &&...args)
  {
    // instance type of class that T_Derived is based on
    // (Gtk::Window for Gtk::WindowClass)
    using instance_type = typename T_Derived::instance_type;

    auto obj = super::get_object(name);
    if (!obj)
      return {};
    // should be of expected base type
    auto wobj = gi::object_cast<instance_type>(obj);
    if (!wobj) {
      g_error("wrong type (%s)", name.c_str());
      return {};
    }

    // perhaps wrapper already of suitable type
    auto wrapper = detail::ObjectClass::instance(obj.gobj_());
    auto wrapper_cast = dynamic_cast<T_Derived *>(wrapper);
    if (wrapper && !wrapper_cast) {
      g_error("wrong C++ instance type (%s)", name.c_str());
      return {};
    } else if (wrapper_cast) {
      return ref_ptr<T_Derived>(wrapper_cast, false);
    }

    // obtain builder from this
    // (Builder subclass not yet declared/defined at this stage)
    auto &builder = *(Builder *)(this);
    // make wrapper using suitable constructor signature
    // which does not add ref on provided instance
    // so ref_ptr needs to grab an extra one
    return ref_ptr<T_Derived>(
        new T_Derived(wobj, builder, std::forward<Args>(args)...), false);
  }

}; // class

#undef GI_GTK_BUILDER_BASE
#define GI_GTK_BUILDER_BASE base::BuilderExtra

} // namespace base

} // namespace Gtk

} // namespace repository

} // namespace gi

#endif
