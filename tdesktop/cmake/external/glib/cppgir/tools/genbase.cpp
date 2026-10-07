#include "genbase.hpp"

#include <map>

static std::string
qualify(const std::string &ns, const std::string &cpptype, int flags)
{
  if (cpptype.find(':') == cpptype.npos && !cpptype.empty() &&
      !(flags & TYPE_BASIC))
    return ns + "::" + cpptype;
  return cpptype;
}

GeneratorBase::GeneratorBase(GeneratorContext &_ctx, const std::string _ns)
    : ctx(_ctx), ns(_ns)
{}

std::string
GeneratorBase::qualify(const std::string &cpptype, int flags) const
{
  return ::qualify(ns, cpptype, flags);
}

void
GeneratorBase::parse_typeinfo(
    const std::string &girname, TypeInfo &result) const
{
  auto ti = ctx.repo.lookup(girname);
  if (ti && ti->info)
    result = *ti->info;
}

GeneratorBase::ArgInfo
GeneratorBase::parse_arginfo(const pt::ptree &node, ArgInfo *routput) const
{
  ArgInfo lresult;
  ArgInfo &result = routput ? *routput : lresult;

  const pt::ptree *pntype{};
  std::string kind;
  // try most likely in turn
  for (auto &&v : {EL_TYPE, EL_ARRAY, EL_VARARGS}) {
    const auto &ntypeo = node.get_child_optional(v);
    if (ntypeo.is_initialized()) {
      kind = v;
      pntype = &ntypeo.get();
    }
  }
  if (kind.empty() || !pntype)
    throw skip("type info not found");
  if (kind == EL_VARARGS)
    throw skip("varargs not supported", skip::OK);
  auto &ntype = *pntype;
  bool discard_element = false;
  // no name for array
  if (kind == EL_ARRAY) {
    result.flags = TYPE_ARRAY;
    result.length = get_attribute<int>(ntype, AT_LENGTH, -10);
    result.zeroterminated = get_attribute<int>(ntype, AT_ZERO_TERMINATED, 1);
    result.fixedsize = get_attribute<int>(ntype, AT_FIXED_SIZE, 0);
    if (result.length < 0 && !result.zeroterminated && !result.fixedsize)
      throw skip("inconsistent array info");
    auto name = get_name(ntype, std::nothrow);
    // could be GBytes, etc
    if (name.size()) {
      // transform to plain argument if not ignored
      // FIXME maybe custom wrappers might be more convenient ??
      if (ctx.match_ignore.matches(ns, EL_RECORD, {name}))
        throw skip(name + " array not supported", skip::OK);
      // fresh state and discard element info processing
      result = ArgInfo{};
      discard_element = true;
      parse_typeinfo(name, result);
    }
  } else {
    parse_typeinfo(get_name(ntype), result);
  }
  // should be present (but not so for signal)
  // though const info seems to be missing in array case
  result.ctype = get_attribute(ntype, AT_CTYPE, "");

  // special case where const is preserved for the cpp type
  if (result.girname == "gpointer" && is_const(result.ctype))
    parse_typeinfo("gconstpointer", result);

  // handle element type info
  int i = 0;
  for (const auto &n : ntype) {
    if (n.first == "type" && !discard_element) {
      ArgTypeInfo &ti = i == 0 ? result.first : result.second;
      auto elname = get_name(n.second);
      parse_typeinfo(elname, ti);
      if (!ti.flags)
        throw skip("container element not supported", skip::OK);
      // probably not, but give it a shot
      ti.ctype = get_attribute(n.second, AT_CTYPE, "");
      // avoid specialized bool vector
      if (ti.cpptype == "bool")
        ti.cpptype = "gboolean";
      // some char arrays (e.g. g_regex* parameters) specify utf8 element type
      // along with ctype char rather than char*
      // so discard utf8 in that case and go for a plain array
      if (kind == EL_ARRAY && elname == "utf8" && ti.ctype == "gchar")
        parse_typeinfo(ti.ctype, ti);
      ++i;
    }
  }

  // sanity checks
  if ((result.flags & (TYPE_ARRAY | TYPE_LIST))) {
    if (i != 1)
      throw skip("inconsistent list element info");
    if (result.flags & TYPE_ARRAY) {
      // NOTE apparently both are possible, see e.g. g_shell_parse_arg*
      // in that case it is handled zeroterminated
      // and the size param becomes a regular one
      if (result.zeroterminated) {
        result.length = -1;
        result.fixedsize = 0;
      }
      // 1 pointer level more than the basic type
      result.pdepth = ((result.first.flags & TYPE_CLASS) ? 1 : 0) + 1;
    }
  } else if (result.flags & TYPE_MAP) {
    if (i != 2)
      throw skip("inconsistent map element info");
  } else {
    if (i != 0)
      throw skip("inconsistent type info");
  }

  return result;
}

std::string
GeneratorBase::make_ctype(
    const ArgInfo &info, const std::string &direction, bool callerallocates)
{
  std::string result;
  bool out = !(direction == DIR_IN || direction == DIR_RETURN);
  if (info.flags & TYPE_ARRAY) {
    auto ret = info.first.argtype + GI_PTR;
    if (out && !callerallocates)
      ret += GI_PTR;
    result = ret;
  } else if (info.flags & TYPE_CALLBACK) {
    result = info.cpptype + "::cfunction_type";
  } else {
    if (!out || callerallocates) {
      result = info.argtype;
    } else {
      result = info.argtype + GI_PTR;
    }
  }
  return (is_const(info.ctype) ? std::string("const ") : EMPTY) + result;
}

void
GeneratorBase::track_dependency(DepsSet &deps, const ArgInfo &info) const
{
  auto track = [&](const std::string &cpptype, int flags) {
    // other items (e.g. enums) are included before anyway
    if (flags & TYPE_CLASS && !(flags & (TYPE_BASIC | TYPE_TYPEDEF))) {
      // always track scoped
      assert(is_qualified(cpptype));
      deps.insert({"", cpptype});
      if (flags & TYPE_BOXED)
        deps.insert({"", cpptype + GI_SUFFIX_REF});
    }
  };
  if (!(info.flags & TYPE_CONTAINER)) {
    track(info.cpptype, info.flags);
  } else {
    track(info.first.cpptype, info.first.flags);
    if (info.flags & TYPE_MAP)
      track(info.second.cpptype, info.second.flags);
  }
}

bool
GeneratorBase::check_suppression(const std::string &ns, const std::string &kind,
    const std::string &name) const
{
  // always generate
  ctx.suppressions.insert(ctx.match_sup.format(ns, kind, name));
  return ctx.match_sup.matches(ns, kind, {name});
}

std::string
GeneratorBase::make_wrap_format(const ArgInfo &info,
    const std::string &transfer, const std::string &outtype)
{
  std::string fmts;
  auto format = "{}";
  if (info.flags & TYPE_CLASS) {
    fmts = fmt::format(GI_NS_SCOPED + "wrap ({}, {})", format,
        get_transfer_parameter(transfer));
  } else if (info.flags & TYPE_ENUM) {
    fmts = GI_NS_SCOPED + "wrap ({})";
  } else if (info.flags & (TYPE_LIST | TYPE_ARRAY | TYPE_MAP)) {
    assert(!outtype.empty());
    fmts = fmt::format(GI_NS_SCOPED + "wrap_to<{}>({}, {})", outtype, format,
        get_transfer_parameter(transfer));
  } else {
    fmts = format;
  }
  return fmts;
}

std::string
GeneratorBase::get_transfer_parameter(const std::string &transfer, bool _type)
{
  std::map<std::string, std::string> m{
      {TRANSFER_NOTHING, "transfer_none"},
      {TRANSFER_FULL, "transfer_full"},
      {TRANSFER_CONTAINER, "transfer_container"},
  };
  auto it = m.find(transfer);
  if (it == m.end())
    throw std::runtime_error("invalid transfer " + transfer);
  return GI_NS_SCOPED + it->second + (_type ? "_t" : "");
}
