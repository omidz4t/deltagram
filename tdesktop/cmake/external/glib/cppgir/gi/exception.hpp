#ifndef GI_EXCEPTION_HPP
#define GI_EXCEPTION_HPP

#include "base.hpp"
#include "wrap.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
inline std::logic_error
transform_error(GType tp, const char *name = nullptr)
{
  auto n = g_type_name(tp);
  auto msg = std::string("could not transform value to type ") +
             detail::make_string(n);
  if (name)
    msg += std::string(" of property \'") + name + "\'";
  return std::invalid_argument(msg);
}

inline std::logic_error
unknown_property_error(GType tp, const gchar *property)
{
  auto n = g_type_name(tp);
  auto msg = std::string("object of type ") + detail::make_string(n) +
             " does not have property \'" + detail::make_string(property) +
             "\'";
  return std::invalid_argument(msg);
}

inline std::logic_error
unknown_signal_error(GType tp, const std::string &name)
{
  auto n = g_type_name(tp);
  auto msg = std::string("object of type ") + detail::make_string(n) +
             " does not have signal \'" + name + "\'";
  return std::invalid_argument(msg);
}

inline std::logic_error
invalid_signal_callback_error(
    GType tp, const std::string &name, const std::string &_msg)
{
  auto n = g_type_name(tp);
  auto msg = std::string("invalid callback for signal ") + n + "::" + name +
             "; " + _msg;
  return std::invalid_argument(msg);
}

// partially generated GError wrapper
class Error : public gi::detail::GBoxedWrapperBase<Error, GError>
{
  typedef gi::detail::GBoxedWrapperBase<Error, GError> super_type;

public:
  Error(GError *obj = nullptr) : super_type(obj) {}

  static GType get_type_() G_GNUC_CONST { return g_error_get_type(); }

  // use with care; dangling reference caution applies here
  gint &code_() { return gobj_()->code; }
  const gint &code_() const { return gobj_()->code; }

  // use with care; dangling reference caution applies here
  gi::cstring_v message_() const { return gobj_()->message; }

  // gboolean g_error_matches (const GError* error, GQuark domain, gint code);
  inline bool matches(GQuark domain, gint code) const
  {
    return g_error_matches(gobj_(), domain, code);
  }

}; // class

} // namespace detail

namespace repository
{
namespace GLib
{
class Error_Ref;

class Error
    : public std::runtime_error,
      public detail::GBoxedWrapper<Error, ::GError, detail::Error, Error_Ref>
{
  typedef std::runtime_error super;

  static inline std::string make_message(GError *error)
  {
    return error ? detail::make_string(g_quark_to_string(error->domain)) +
                       ": " + detail::make_string(error->message) + "(" +
                       std::to_string(error->code) + ")"
                 : "";
  }

public:
  explicit Error(GError *obj = nullptr) : super(make_message(obj))
  {
    data_ = obj;
  }

  // GError* g_error_new_literal (GQuark domain, gint code, const gchar*
  // message);
  static inline Error new_literal(
      GQuark domain, gint code, const std::string &message)
  {
    return Error(g_error_new_literal(
        domain, code, gi::unwrap(message, gi::transfer_none)));
  }

  // GError* g_error_copy (const GError* error);
  inline Error copy() const { return Error(g_error_copy(gobj_())); }

  // override wrap since we are no longer in a simple single-base case
  template<typename Cpp, typename Enable = typename std::enable_if<
                             std::is_base_of<Error, Cpp>::value>::type>
  static Cpp wrap(const typename Cpp::BaseObjectType *obj)
  {
    static_assert(sizeof(Cpp) == sizeof(Error), "type wrap not supported");
    Error w(const_cast<GError *>(obj));
    return std::move(*static_cast<Cpp *>(&w));
  }
};

class Error_Ref
    : public gi::detail::GBoxedRefWrapper<GLib::Error, ::GError, detail::Error>
{
  typedef gi::detail::GBoxedRefWrapper<GLib::Error, ::GError, detail::Error>
      super_type;
  using super_type::super_type;

  // GError* g_error_copy (const GError* error);
  inline Error copy() const { return Error(g_error_copy(gobj_())); }
};

} // namespace GLib

template<>
struct declare_cpptype_of<GError>
{
  typedef GLib::Error type;
};

} // namespace repository

inline void
check_error(GError *error)
{
  if (error)
    detail::try_throw(repository::GLib::Error(error));
}

namespace detail
{
inline repository::GLib::Error
missing_symbol_error(const std::string &symbol)
{
  ::GQuark domain = g_quark_from_static_string("gi-error-quark");
  auto error =
      g_error_new(domain, 0, "could not find symbol %s", symbol.c_str());
  return repository::GLib::Error(error);
}
} // namespace detail

} // namespace gi

#endif // GI_EXCEPTION_HPP
