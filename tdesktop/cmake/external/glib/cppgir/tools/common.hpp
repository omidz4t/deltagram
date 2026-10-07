#ifndef COMMON_HPP
#define COMMON_HPP

#include <iostream>
#include <string>

#include "format.hpp"

namespace
{
const char GI_PTR = '*';
const std::string GI_SUFFIX_REF = "_Ref";
const std::string GI_SUFFIX_CF_CTYPE = "_CF_CType";
const std::string GI_SUFFIX_CB_TRAIT = "_CB_Trait";

const std::string GIR_GOBJECT("GObject.Object");
const std::string GIR_GINITIALLYUNOWNED("GObject.InitiallyUnowned");
const std::string GIR_GVARIANT("GLib.Variant");
const std::string GIR_VOID("none");
const std::string GIR_GDESTROYNOTIFY("GLib.DestroyNotify");

const std::string GDESTROYNOTIFY("GDestroyNotify");
const std::string CPP_VOID("void");
const std::string GIR_SUFFIX(".gir");

const std::string PT_ATTR("<xmlattr>");
const std::string GI_NS("gi");
const std::string GI_NS_INTERNAL("internal");
const std::string GI_NS_IMPL("impl");
const std::string GI_NS_ARGS("callargs");
const std::string GI_SCOPE("::");
const std::string GI_NS_SCOPED("gi::");
const std::string GI_NS_DETAIL_SCOPED("gi::detail::");
const std::string GI_REPOSITORY_NS("repository");
const std::string GI_INLINE("GI_INLINE_DECL");
const std::string GI_CLASS_IMPL_BEGIN("GI_CLASS_IMPL_BEGIN");
const std::string GI_CLASS_IMPL_END("GI_CLASS_IMPL_END");
const std::string GI_DISABLE_DEPRECATED_WARN_BEGIN(
    "GI_DISABLE_DEPRECATED_WARN_BEGIN");
const std::string GI_DISABLE_DEPRECATED_WARN_END(
    "GI_DISABLE_DEPRECATED_WARN_END");
const std::string EMPTY;

const std::string EL_REPOSITORY("repository");
const std::string EL_CINCLUDE("c:include");
const std::string EL_ALIAS("alias");
const std::string EL_ENUM("enumeration");
const std::string EL_FLAGS("bitfield");
const std::string EL_MEMBER("member");
const std::string EL_CONST("constant");
const std::string EL_OBJECT("class");
const std::string EL_INTERFACE("interface");
const std::string EL_RECORD("record");
const std::string EL_CALLBACK("callback");

const std::string EL_FUNCTION("function");
const std::string EL_CONSTRUCTOR("constructor");
const std::string EL_METHOD("method");
const std::string EL_VIRTUAL_METHOD("virtual-method");
const std::string EL_FIELD("field");
const std::string EL_PROPERTY("property");
const std::string EL_SIGNAL("glib:signal");

const std::string EL_RETURN("return-value");
const std::string EL_PARAMETERS("parameters");
const std::string EL_INSTANCE_PARAMETER("instance-parameter");
const std::string EL_PARAMETER("parameter");

const std::string EL_TYPE("type");
const std::string EL_ARRAY("array");
const std::string EL_VARARGS("varargs");
const std::string EL_IMPLEMENTS("implements");

const std::string AT_SHARED_LIBRARY("shared-library");
const std::string AT_DEPRECATED("deprecated");
const std::string AT_INTROSPECTABLE("introspectable");
const std::string AT_FOREIGN("foreign");
const std::string AT_SHADOWS("shadows");
const std::string AT_SHADOWED_BY("shadowed-by");
const std::string AT_DISGUISED("disguised");
const std::string AT_MOVED_TO("moved-to");
const std::string AT_VERSION("version");

const std::string AT_NAME("name");
const std::string AT_TRANSFER("transfer-ownership");
const std::string AT_DIRECTION("direction");
const std::string AT_CLOSURE("closure");
const std::string AT_DESTROY("destroy");
const std::string AT_SCOPE("scope");
const std::string AT_NULLABLE("nullable");
const std::string AT_OPTIONAL("optional");
const std::string AT_ALLOW_NONE("allow-none");
const std::string AT_CALLER_ALLOCATES("caller-allocates");
const std::string AT_GLIB_GET_TYPE("glib:get-type");
const std::string AT_GLIB_FUNDAMENTAL("glib:fundamental");
const std::string AT_GLIB_TYPE_STRUCT("glib:type-struct");
const std::string AT_GLIB_IS_TYPE_STRUCT_FOR("glib:is-gtype-struct-for");

const std::string AT_PARENT("parent");

const std::string AT_LENGTH("length");
const std::string AT_ZERO_TERMINATED("zero-terminated");
const std::string AT_FIXED_SIZE("fixed-size");

const std::string AT_THROWS("throws");
const std::string AT_CTYPE("c:type");
const std::string AT_CIDENTIFIER("c:identifier");

const std::string TRANSFER_NOTHING("none");
const std::string TRANSFER_FULL("full");
const std::string TRANSFER_CONTAINER("container");

const std::string SCOPE_NOTIFIED("notified");
const std::string SCOPE_ASYNC("async");
const std::string SCOPE_CALL("call");

const std::string DIR_IN("in");
const std::string DIR_OUT("out");
const std::string DIR_INOUT("inout");
const std::string DIR_RETURN("return");

const std::string AT_READABLE("readable");
const std::string AT_WRITABLE("writable");
const std::string AT_PRIVATE("private");
} // namespace

enum class Log { NONE, ERROR, WARNING, INFO, DEBUG, LOG };

extern Log _loglevel;

template<typename T>
void
logger(Log level, const T &m)
{
  static std::string lvl[] = {"NONE", "ERROR", "WARN", "INFO", "DEBUG", "LOG"};
  if (level <= _loglevel)
    std::cerr << lvl[(int)level] << " " << m << std::endl;
}

template<typename T, typename ARG, typename... ARGS>
void
logger(Log level, const T &m, ARG &&arg, ARGS &&...args)
{
  logger(level,
      fmt::format(m, std::forward<ARG>(arg), std::forward<ARGS>(args)...));
}

inline bool
is_qualified(const std::string &name)
{
  return (name.find(':') != name.npos) || (name.find('.') != name.npos);
}

inline std::string
toupper(const std::string &s)
{
  auto c = s;
  for (auto &ch : c)
    ch = std::toupper(ch);
  return c;
}

inline std::string
tolower(const std::string &s)
{
  auto c = s;
  for (auto &ch : c)
    ch = std::tolower(ch);
  return c;
}

#endif // COMMON_HPP
