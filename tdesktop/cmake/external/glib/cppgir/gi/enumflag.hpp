#ifndef GI_ENUMFLAG_HPP
#define GI_ENUMFLAG_HPP

#include "base.hpp"
#include "exception.hpp"
#include "value.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
struct EnumValueTraits
{
  typedef GEnumClass class_type;
  typedef GEnumValue value_type;

  static class_type *get_class(GType gtype)
  {
    auto c = g_type_class_peek(gtype);
    return (class_type *)(c ? G_ENUM_CLASS(c) : c);
  }
  static value_type *get_value(class_type *klass, int v)
  {
    return g_enum_get_value(klass, v);
  }
  static value_type *get_by_name(class_type *klass, const char *name)
  {
    return g_enum_get_value_by_name(klass, name);
  }
  static value_type *get_by_nick(class_type *klass, const char *name)
  {
    return g_enum_get_value_by_nick(klass, name);
  }
};

struct FlagsValueTraits
{
  typedef GFlagsClass class_type;
  typedef GFlagsValue value_type;

  static class_type *get_class(GType gtype)
  {
    auto c = g_type_class_peek(gtype);
    return (class_type *)(c ? G_FLAGS_CLASS(c) : c);
  }
  static value_type *get_value(class_type *klass, int v)
  {
    return g_flags_get_first_value(klass, v);
  }
  static value_type *get_by_name(class_type *klass, const char *name)
  {
    return g_flags_get_value_by_name(klass, name);
  }
  static value_type *get_by_nick(class_type *klass, const char *name)
  {
    return g_flags_get_value_by_nick(klass, name);
  }
};

template<typename EnumType, typename Traits>
class EnumValue
{
  typedef EnumValue self;

  typedef typename Traits::value_type value_type;
  typedef typename Traits::class_type class_type;

  value_type *value_;

  static class_type *get_class()
  {
    auto c = Traits::get_class(get_type_());
    if (!c) {
      detail::try_throw(std::invalid_argument("unknown class"));
    } else {
      return c;
    }
  }

  EnumValue(gint v) : EnumValue(static_cast<EnumType>(v)) {}
  EnumValue(value_type *_value) : value_(_value) {}

public:
  typedef EnumType enum_type;

  static GType get_type_() { return traits::gtype<EnumType>::get_type(); }

  EnumValue(EnumType v) : value_(Traits::get_value(get_class(), (int)v))
  {
    if (!value_)
      detail::try_throw(std::invalid_argument(
          "invalid value " + std::to_string((unsigned)v)));
  }

  static self get_by_name(const gi::cstring_v name)
  {
    auto v = Traits::get_by_name(get_class(), name.c_str());
    if (!v)
      detail::try_throw(std::invalid_argument("unknown name"));
    else
      return self(v);
  }
  static self get_by_nick(const gi::cstring_v nick)
  {
    auto v = Traits::get_by_nick(get_class(), nick.c_str());
    if (!v)
      detail::try_throw(std::invalid_argument("unknown nick"));
    else
      return self(v);
  }

  operator EnumType() const noexcept
  {
    return static_cast<EnumType>(value_->value);
  }

  gi::cstring_v value_name() const noexcept { return value_->value_name; }

  gi::cstring_v value_nick() const noexcept { return value_->value_nick; }
};

} // namespace detail

// enumeration nick/name helpers
template<typename EnumType>
using EnumValue = detail::EnumValue<EnumType, detail::EnumValueTraits>;

template<typename EnumType,
    typename std::enable_if<traits::is_enumeration<EnumType>::value &&
                            traits::gtype<EnumType>::value>::type * = nullptr>
inline EnumValue<EnumType>
value_info(EnumType v)
{
  return EnumValue<EnumType>(v);
}

// bitfield nick/name helpers
template<typename EnumType>
using FlagsValue = detail::EnumValue<EnumType, detail::FlagsValueTraits>;

template<typename EnumType,
    typename std::enable_if<traits::is_bitfield<EnumType>::value &&
                            traits::gtype<EnumType>::value>::type * = nullptr>
inline FlagsValue<EnumType>
value_info(EnumType v)
{
  return FlagsValue<EnumType>(v);
}

// bitfield operation helpers
template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType
operator|(FlagType lhs, FlagType rhs)
{
  return static_cast<FlagType>(
      static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType
operator&(FlagType lhs, FlagType rhs)
{
  return static_cast<FlagType>(
      static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType
operator^(FlagType lhs, FlagType rhs)
{
  return static_cast<FlagType>(
      static_cast<unsigned>(lhs) ^ static_cast<unsigned>(rhs));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType
operator~(FlagType flags)
{
  return static_cast<FlagType>(~static_cast<unsigned>(flags));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType &
operator|=(FlagType &lhs, FlagType rhs)
{
  return (lhs = static_cast<FlagType>(
              static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs)));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType &
operator&=(FlagType &lhs, FlagType rhs)
{
  return (lhs = static_cast<FlagType>(
              static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs)));
}

template<typename FlagType,
    typename std::enable_if<traits::is_bitfield<FlagType>::value>::type * =
        nullptr>
inline FlagType &
operator^=(FlagType &lhs, FlagType rhs)
{
  return (lhs = static_cast<FlagType>(
              static_cast<unsigned>(lhs) ^ static_cast<unsigned>(rhs)));
}

} // namespace gi

// sadly, the above templates are not picked up by ADL
// and might therefore only end up used in case of a `using namespace gi`

// so also explicitly add operators in generated namespace
// (by means of macro to simplify generation)

// see macro interface

/* NOTE:
 *
 * enumeration/bitfield TypeX is currently represented by an enum class TypeX
 * along with a namespace TypeXNS_ that holds the member functions.
 *
 * This could be merged into single class TypeX
 * (possibly using templated helpers);
 *
 * class TypeX
 * {
 *   enum Enum {
 *     VALUE_A,
 *    VALUE_B
 *   }
 *   Enum value;
 *
 *  (or C++17):
 *  inline static const TypeX VALUE_A;
 *  (note; constexpr not possible with incomplete type)
 *
 *   constructor (Enum)
 *   operator Enum()
 *   ... etc ...
 *   std::string value_name();  [opt]
 *   std::string value_nick();  [opt]
 * }
 *
 * Disadvantage is that TypeX::VALUE_A would not have type TypeX,
 * although easily converted from/to TypeX, and so it would mostly work.
 * However, the operators above would have to be (even more) complicated and
 * accept various combinations of TypeX and TypeX::Enum (in case of a bitfield).
 *
 * All in all, the type mismatch (and moderately rare member function
 * occurrence) does not warrant going this way at this time.  Also avoid C++17
 * for now, so as it stands ...
 */

#endif // GI_ENUMFLAG_HPP
