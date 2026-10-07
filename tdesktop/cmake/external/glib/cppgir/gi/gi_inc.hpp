#ifndef GI_INC_HPP
#define GI_INC_HPP

// gi preprocessor/macro interface
// aka global module fragment part

#define GI_VERSION_MAJAOR (2)
#define GI_VERSION_MINOR (0)
#define GI_VERSION_MICRO (0)

#ifdef GI_INLINE
#define GI_INLINE_DECL inline
#else
#define GI_INLINE_DECL
#endif

// == we could be included in some module setting
#ifdef GI_MODULE_IN_INTERFACE
#if __cplusplus < 201703L
#error "need at least C++17 for modules"
#endif
#define GI_MODULE_EXPORT export
#define GI_MODULE_INLINE inline
#define GI_MODULE_STATIC_OR_INLINE inline
#else // GI_MODULE_IN_INTERFACE
#define GI_MODULE_EXPORT
#define GI_MODULE_INLINE
#define GI_MODULE_STATIC_OR_INLINE static
#endif

// only used in module context
#ifdef GI_MODULE_EXTERN
#define GI_MODULE_EXTERN_BEGIN extern "C++" {
#define GI_MODULE_EXTERN_END }
#else
#define GI_MODULE_EXTERN_BEGIN
#define GI_MODULE_EXTERN_END
#endif
// related; another clang warning
// https://github.com/llvm/llvm-project/issues/68615
// `#include <filename>' attaches the declarations to the named module`
#ifdef __clang__
#define GI_MODULE_DISABLE_WARN_BEGIN \
  _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic ignored \"-Winclude-angled-in-module-purview\"")
#define GI_MODULE_DISABLE_WARN_END _Pragma("GCC diagnostic pop")
#else
#define GI_MODULE_DISABLE_WARN_BEGIN
#define GI_MODULE_DISABLE_WARN_END
#endif

#define GI_MODULE_BEGIN \
  GI_MODULE_DISABLE_WARN_BEGIN \
  GI_MODULE_EXTERN_BEGIN

#define GI_MODULE_END \
  GI_MODULE_EXTERN_END \
  GI_MODULE_DISABLE_WARN_BEGIN
// ==

// lots of declarations might be attributed as deprecated,
// but not so annotated, so let's avoid warning floods
// also handle complaints about const qualified casts
// (due to silly const qualified scalar parameters)
#define GI_DISABLE_DEPRECATED_WARN_BEGIN \
  _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"") \
          _Pragma("GCC diagnostic push") \
              _Pragma("GCC diagnostic ignored \"-Wignored-qualifiers\"")

#define GI_DISABLE_DEPRECATED_WARN_END \
  _Pragma("GCC diagnostic pop") _Pragma("GCC diagnostic pop")

// typically clang might warn but gcc might complain about pragma clang ...
#ifdef GI_CLASS_IMPL_PRAGMA
#ifndef GI_CLASS_IMPL_BEGIN
#define GI_CLASS_IMPL_BEGIN \
  _Pragma("GCC diagnostic push") \
      _Pragma("GCC diagnostic ignored \"-Woverloaded-virtual\"")
#endif

#ifndef GI_CLASS_IMPL_END
#define GI_CLASS_IMPL_END _Pragma("GCC diagnostic pop")
#endif
#else
#define GI_CLASS_IMPL_BEGIN
#define GI_CLASS_IMPL_END
#endif

// attempt to auto-discover exception support:
#ifndef GI_CONFIG_EXCEPTIONS
#if defined(_MSC_VER)
#include <cstddef> // for _HAS_EXCEPTIONS
#endif
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (_HAS_EXCEPTIONS)
#define GI_CONFIG_EXCEPTIONS 1
#else
#define GI_CONFIG_EXCEPTIONS 0
#endif
#endif

#include <cstddef>

#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <iterator>
#include <map>
#include <vector>

#if __has_include("nonstd/expected.hpp")
#if __cpp_concepts >= 202002L
// this is also required in gcc's libstdc++ expected
#else
// so if that is missing, then prevent delegation to <expected>
#define nsel_CONFIG_SELECT_EXPECTED nsel_EXPECTED_NONSTD
#endif
#include "nonstd/expected.hpp"
#else
#include <expected>
#endif

#if __cplusplus >= 201703L
#include <optional>
#include <string_view>
#endif

// define << operator if not unwanted
#ifndef GI_NO_STRING_IOS
#include <iostream>
#endif

#if GI_DL
#include <dlfcn.h>
#endif

#include <assert.h>
#include <string.h>

#include <glib-object.h>
#include <glib.h>
#include <gobject/gvaluecollector.h>

// various macros from all over the place
// sadly, module refactor means breaking things apart

// exception
// exception specification is generated according to settings and situation
// some derived code (e.g. overrides) may need to follow suit accordingly
#if GI_EXPECTED
// no exception if reported through expected
#define GI_NOEXCEPT_DECL(nonthrowing) noexcept
#elif GI_DL
// otherwise, everything can start failing if resolved at runtime
#define GI_NOEXCEPT_DECL(nonthrowing)
#else
// otherwise, depends on whether (wrapped) function is (GError) throwing
#define GI_NOEXCEPT_DECL(nonthrowing) noexcept(nonthrowing)
#endif

// callback
#define GI_CB_ARG_CALLBACK_CUSTOM(Type, CF_CType, CF_handler) \
  struct Type \
  { \
    using handler_cb_type = CF_CType; \
    static constexpr auto handler = CF_handler; \
  }

// boxed
// should be used within gi.repository namespace
#define GI_ENABLE_BOXED_COPY(CType) \
  template<> \
  struct enable_boxed_copy<CType> : public std::true_type \
  {};

// enumflag
#define GI_FLAG_OPERATORS(FlagType) \
  inline FlagType operator|(FlagType lhs, FlagType rhs) \
  { \
    return static_cast<FlagType>( \
        static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs)); \
  } \
\
  inline FlagType operator&(FlagType lhs, FlagType rhs) \
  { \
    return static_cast<FlagType>( \
        static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs)); \
  } \
\
  inline FlagType operator^(FlagType lhs, FlagType rhs) \
  { \
    return static_cast<FlagType>( \
        static_cast<unsigned>(lhs) ^ static_cast<unsigned>(rhs)); \
  } \
\
  inline FlagType operator~(FlagType flags) \
  { \
    return static_cast<FlagType>(~static_cast<unsigned>(flags)); \
  } \
\
  inline FlagType &operator|=(FlagType &lhs, FlagType rhs) \
  { \
    return (lhs = static_cast<FlagType>( \
                static_cast<unsigned>(lhs) | static_cast<unsigned>(rhs))); \
  } \
\
  inline FlagType &operator&=(FlagType &lhs, FlagType rhs) \
  { \
    return (lhs = static_cast<FlagType>( \
                static_cast<unsigned>(lhs) & static_cast<unsigned>(rhs))); \
  } \
\
  inline FlagType &operator^=(FlagType &lhs, FlagType rhs) \
  { \
    return (lhs = static_cast<FlagType>( \
                static_cast<unsigned>(lhs) ^ static_cast<unsigned>(rhs))); \
  }

#define GI_ENUM_NUMERIC(EnumType) \
  inline std::underlying_type<EnumType>::type operator+(EnumType e) \
  { \
    return static_cast<std::underlying_type<EnumType>::type>(e); \
  }

// objectclass
// helper macro to obtain data from factory (if provided)
#define GI_MEMBER_INIT_DATA(ClassType, factory) \
  (factory ? ((ClassType(*)())(factory))() : ClassType());

// conflicts might arise between interfaces and/or class
// generate some dummy check types to force failure
#define GI_MEMBER_CHECK_CONFLICT(member) _check_member_conflict_##member

// generated code tries to detect a defined member in SubClass as follows
#define GI_MEMBER_DEFAULT_HAS_DEFINITION(BaseDef, member) \
  template<typename SubClass> \
  constexpr static bool has_definition(const member##_t *, const SubClass *) \
  { \
    /* the use of conflict type check is only to trigger a compiler error \
     * if there is such a conflict \
     * in that case, manual specification of definitions are needed \
     * (which will then avoid this code path instantiation) \
     * (type should never be void, so merely serves as dummy check) \
     */ \
    return std::is_void<typename SubClass::GI_MEMBER_CHECK_CONFLICT( \
               member)>::value || \
           !std::is_same<decltype(&BaseDef::member##_), \
               decltype(&SubClass::member##_)>::value; \
  }

// helper macro used in generated code
#define GI_MEMBER_DEFINE(BaseDef, member) \
  struct member##_tag; \
  using member##_t = detail::member_type<member##_tag>; \
  member##_t member; \
  GI_MEMBER_DEFAULT_HAS_DEFINITION(BaseDef, member)

// the automated way might/will fail in case of overload resolution failure
// (due to member conflicts with interfaces)
// so the following can be used to specify definition situation
// should be used in an inner struct DefinitionData in the SubClass
#define GI_DEFINES_MEMBER(BaseDef, member, _defines) \
  template<typename SubClass> \
  constexpr static bool defines( \
      const BaseDef::TypeInitData::member##_t *, const SubClass *) \
  { \
    return _defines; \
  }

// uses function overload on all of the above to determine
// if member of DefData is defined/overridden in SubClass
// (and should then be registered in the class/interface struct)
#define GI_MEMBER_HAS_DEFINITION(SubClass, DefData, member) \
  DefData::defines((member##_t *)(nullptr), (SubClass *)(nullptr))

#endif // GI_INC_HPP
