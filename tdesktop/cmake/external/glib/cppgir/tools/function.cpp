#include "function.hpp"

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator.hpp>

#include <map>
#include <tuple>

namespace
{ // anonymous

const int INDEX_RETURN{-5};
const int INDEX_INSTANCE{-1};
const int INDEX_CF{150};
const int INDEX_ERROR{100};
const std::string MOVE = "std::move";

struct FunctionGenerator : public GeneratorBase
{
  using Output = FunctionDefinition::Output;

  const ElementFunction &func;
  // duplicated from above
  const std::string &kind;
  const std::string &klass, &klasstype;
  // errors collected along the way (trigger abort)
  std::vector<std::string> errors;
  // collected dependencies
  DepsSet &deps;
  // collected CallArgs definitions
  std::ostream *call_args_decl{};
  // tweak debug level upon error
  bool ignored = false;
  bool introspectable = true;
  bool allow_deprecated = false;

  // also detect various function generation alternatives
  enum class opt_except {
    NOEXCEPT,
    THROW,
    EXPECTED,
    GERROR,
    DEFAULT = NOEXCEPT,
    ALT = GERROR
  };
  std::set<opt_except> do_except = {opt_except::DEFAULT};
  enum class opt_output { PARAM, TUPLE, DEFAULT = PARAM, ALT = TUPLE };
  std::set<opt_output> do_output = {opt_output::DEFAULT};
  enum class opt_nullable {
    PRESENT,
    DISCARD,
    DEFAULT = PRESENT,
    ALT = DISCARD
  };
  std::set<opt_nullable> do_nullable = {opt_nullable::DEFAULT};
  // basic value container
  enum class opt_basic_container {
    PASS,
    COLLECTION,
    DEFAULT = PASS,
    ALT = COLLECTION
  };
  // default is fixed and always processed
  std::set<opt_basic_container> do_basic_container;

  struct Options
  {
    opt_except except{};
    opt_output output{};
    opt_nullable nullable{};
    opt_basic_container basic_container{};
    Options(opt_except _except, opt_output _output, opt_nullable _nullable)
        : except(_except), output(_output), nullable(_nullable)
    {}
    Options() {}
  };

  // global info
  // track callback's user_data
  int found_user_data = INDEX_DEFAULT;
  // const method
  bool const_method = false;
  // typical async method;
  // has GAsyncReadyCallback param with async scope
  std::string async_cb_wrapper;
  // indexed by GIR numbering (first non-instance parameter index 0)
  std::map<int, Parameter> paraminfo;
  // param numbers referenced by some other parameter
  std::set<int> referenced;
  // defined/declared CallArgs
  // (various exception variants may re-use the same)
  std::set<std::string> call_args_types;
  // collects generated declaration and implementation
  std::ostringstream oss_decl, oss_impl;

  using FunctionData = FunctionDefinition;

  struct FunctionDataExtended : public FunctionDefinition
  {
    // (indexed as above)
    std::vector<decltype(cpp_decl)::mapped_type> cpp_decl_unfolded;

    FunctionDataExtended() = default;

    FunctionDataExtended(FunctionDefinition odef)
        : FunctionDefinition(std::move(odef))
    {
      auto &def = *this;
      // process decl map into plain list
      def.cpp_decl_unfolded.clear();
      for (auto &&e : def.cpp_decl)
        def.cpp_decl_unfolded.emplace_back(e.second);
    }
  };

public:
  FunctionGenerator(GeneratorContext &_ctx, const std::string _ns,
      const ElementFunction &_func, const std::string &_klass,
      const std::string &_klasstype, DepsSet &_deps, std::ostream *_call_args,
      bool _allow_deprecated = false)
      : GeneratorBase(_ctx, _ns), func(_func), kind(func.kind), klass(_klass),
        klasstype(_klasstype), deps(_deps), call_args_decl(_call_args),
        allow_deprecated(_allow_deprecated)
  {
    assert(func.name.size());
    assert(func.kind.size());
    assert(func.c_id.size());
    assert(func.functionexp.size());
  }

  static bool ends_with(std::string const &value, std::string const &ending)
  {
    if (ending.size() > value.size())
      return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
  };

  void handle_skip(const skip &ex)
  {
    // we tried but it went wrong ...
    if (errors.empty() && !introspectable) {
      errors.push_back("not introspectable");
      // ... so let's not complain about it
      ignored = true;
    }
    if (ex.cause == skip::INVALID) {
      auto level = ignored ? Log::DEBUG : Log::WARNING;
      if (check_suppression(ns, kind, func.c_id))
        level = Log::DEBUG;
      logger(level, "skipping {} {}; {}", kind, func.c_id, ex.what());
    } else if (ex.cause == skip::IGNORE) {
      ignored = true;
    }
    errors.push_back(ex.what());
  }

  bool check_errors()
  {
    // check errors
    if (errors.size()) {
      auto reason = ignored ? "IGNORE" : "SKIP";
      auto err = fmt::format(
          "// {}; {}", reason, boost::algorithm::join(errors, ", "));
      oss_decl << err << std::endl;
      // no impl for signals
      if (kind != EL_SIGNAL)
        oss_impl << err << std::endl;
      return true;
    }
    return false;
  }

  // all parameters should have ctype info except for signal case
  void check_ctype(const ArgInfo &info)
  {
    if (info.ctype.empty() && kind != EL_SIGNAL)
      throw skip("missing c:type info");
  }

  void collect_param(const pt::ptree::value_type &n, int &param_no)
  {
    // instance parameter not in cpp signature
    auto &el = n.first;
    bool instance = (el == EL_INSTANCE_PARAMETER);
    param_no += instance ? 0 : 1;
    // directly parse into target
    // so we always have minimal attributes for raw fallback
    auto &pinfo = paraminfo[param_no];
    pinfo.instance = instance;
    pinfo.name = unreserve(get_name(n.second), false);
    // gather a bunch of attributes
    pinfo.direction = get_attribute(n.second, AT_DIRECTION, DIR_IN);
    pinfo.transfer = get_attribute(n.second, AT_TRANSFER);
    pinfo.closure = get_attribute<int>(n.second, AT_CLOSURE, -10);
    pinfo.destroy = get_attribute<int>(n.second, AT_DESTROY, -10);
    pinfo.callerallocates =
        get_attribute<int>(n.second, AT_CALLER_ALLOCATES, 0);
    pinfo.scope = get_attribute(n.second, AT_SCOPE, "");
    bool allownone = get_attribute<bool>(n.second, AT_ALLOW_NONE, 0);
    // limited backwards compatbility for allow-none
    bool is_out = pinfo.direction.find(DIR_OUT) != pinfo.direction.npos;
    pinfo.optional =
        (is_out && allownone) || get_attribute<bool>(n.second, AT_OPTIONAL, 0);
    pinfo.nullable =
        (!is_out && allownone) || get_attribute<bool>(n.second, AT_NULLABLE, 0);
    // might fail
    parse_arginfo(n.second, &pinfo.tinfo);
    check_ctype(pinfo.tinfo);
  }

  void collect_node(const pt::ptree::value_type &entry)
  {
    auto &node = entry.second;

    // should be ignored in some cases
    try {
      auto deprecated = get_attribute<int>(node, AT_DEPRECATED, 0);
      if (ctx.match_ignore.matches(ns, kind, {func.name, func.c_id}))
        throw skip("marked ignore", skip::IGNORE);
      if (deprecated && !allow_deprecated)
        throw skip("deprecated", skip::IGNORE);
      // there are a few nice-to-have in this case (e.g. some _get_source)
      // so let's try anyway but not complain too much if it goes wrong
      auto intro = get_attribute<int>(node, AT_INTROSPECTABLE, 1);
      // except when renaming is at hand
      auto shadowed = get_attribute(node, AT_SHADOWED_BY, "");
      if (shadowed.size() && !intro)
        throw skip("not introspectable; shadowed-by " + shadowed, skip::IGNORE);
      // otherwise mark and try ...
      introspectable = intro;
    } catch (skip &ex) {
      handle_skip(ex);
    }

    try {
      auto &ret = node.get_child(EL_RETURN);
      // return value
      // always try to read minimal info (e.g. ctype)
      // as it might be needed for method fallback
      auto &pinfo = paraminfo[INDEX_RETURN];
      pinfo.direction = DIR_RETURN;
      // best effort for now, see also below
      pinfo.transfer = get_attribute(ret, AT_TRANSFER, TRANSFER_FULL);
      pinfo.nullable = get_attribute<bool>(ret, AT_NULLABLE, 0);
      parse_arginfo(ret, &pinfo.tinfo);
      // override constructor to return class instance
      if (kind == EL_CONSTRUCTOR)
        parse_typeinfo(klasstype, pinfo.tinfo);
      check_ctype(pinfo.tinfo);
      pinfo.transfer = pinfo.tinfo.cpptype == CPP_VOID
                           ? TRANSFER_FULL
                           : get_attribute(ret, AT_TRANSFER);
    } catch (skip &ex) {
      handle_skip(ex);
    }

    { // first pass to collect parameter info
      int param_no = INDEX_INSTANCE;
      auto params = node.get_child_optional(EL_PARAMETERS);
      if (params.is_initialized()) {
        for (auto &n : params.value()) {
          try {
            auto el = n.first;
            if (el == EL_PARAMETER || el == EL_INSTANCE_PARAMETER) {
              collect_param(n, param_no);
            }
          } catch (skip &ex) {
            handle_skip(ex);
          }
        }
      }
    }
  }

  // could be dropped if wrap/unwrap is made more casual about const-stuff
  // but on the other hand these checks catch some incorrect annotations
  // so some warning or discarding still has merit
  void verify_const_transfer(const ArgInfo &info, const std::string &transfer,
      const std::string &direction, bool /*wrap*/)
  {
    auto ctype = info.ctype;
    // never mind in case of signal
    if (kind == EL_SIGNAL)
      return;
    // note that const for parameter is not always picked up reliably
    // so if the ctype is const, it probably really is
    // (and so we might complain or warn)
    // but if the ctype is not const, the real one in function might
    // actually be (so we can't know for sure and then should not warn)
    // const and transfer full should not go together
    if (is_const(ctype) && transfer != TRANSFER_NOTHING) {
      auto level =
          check_suppression(ns, kind, func.c_id) ? Log::DEBUG : Log::WARNING;
      logger(level, "warning; {}; const transfer {} mismatch [{}]", func.c_id,
          transfer, direction);
    }
#if 0
    if ((flags & TYPE_BASIC) && (flags & TYPE_CLASS)) {
      // string transfer none preferably unwraps to a const
      // but as mentioned above; this check is not reliable
      if (!wrap && !is_const (ctype) && transfer == TRANSFER_NOTHING)
        throw skip ("transfer none on non-const string");
    }
#endif
  }

  bool process_param_in_callback(int param_no, const Parameter &pinfo,
      const Options & /*options*/, FunctionData &def, bool defaultable)
  {
    auto closure = pinfo.closure;
    auto scope = pinfo.scope;
    auto destroy = pinfo.destroy;
    auto &pname = pinfo.name;
    auto &&cpptype = pinfo.tinfo.cppreftype(pinfo.transfer);
    const bool callee = kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD;

    logger(Log::LOG, "param_in_callback {} {}", param_no, pname);
    // lots of sanity to check
    static const std::string SCOPE_ASYNC_DEP("async_dep");
    const bool async_method = !async_cb_wrapper.empty();
    if (closure < 0)
      throw skip("callback misses closure info");
    auto it = paraminfo.find(closure);
    if (it == paraminfo.end())
      throw skip("callback has invalid closure parameter");
    const auto &cctype = it->second.ptype;
    if (get_pointer_depth(cctype) != 1)
      throw skip("invalid callback closure parameter type " + cctype);
    // if async method, there is at least 1 async scope callback
    // if call also supports another callback, it could in principle
    // have any valid scope, but quite likely it has no "sane" scope,
    // and its user_data parameter is assumed to live within the async scope
    // so allow for this case in the sanity checks below
    // though this case is only dealt with in regular callforward code
    // (not in callback code)
    if (scope.empty()) {
      if (!async_method)
        throw skip("callback misses scope info");
      else if (callee)
        throw skip(kind + "; async_dep scope not supported");
      scope = SCOPE_ASYNC_DEP;
    }
    if ((scope == SCOPE_NOTIFIED) != (destroy >= 0)) {
      if (!async_method)
        throw skip("callback destroy info mismatch");
      scope = SCOPE_ASYNC_DEP;
    }
    if (destroy >= 0) {
      it = paraminfo.find(destroy);
      if (it == paraminfo.end()) {
        throw skip("callback has invalid destroynotify parameter");
      } else {
        auto girname = it->second.tinfo.girname;
        // mind qualification
        if (girname != GIR_GDESTROYNOTIFY)
          throw skip(
              "invalid callback destroy notify parameter type " + girname);
      }
    }
    // there may be multiple callbacks, but they should not overlap
    // so check if we already entered something for the call parameter
    if (def.c_call.find(closure) != def.c_call.end())
      throw skip("callback closure parameter already used");
    if (def.c_call.find(destroy) != def.c_call.end())
      throw skip("callback destroy parameter already used");

    { // this could be callback parameter within a callback or virtual method
      // so collect the needed parts for the argument trait
      // (which have been validated above)
      auto &ti = def.arg_traits[param_no];
      ti.args = {param_no, closure};
      if (destroy >= 0)
        ti.args.push_back(destroy);
      // determine custom arg trait type from type name
      // (with suitable suffix and in internal namespace)
      auto index = cpptype.rfind(GI_SCOPE);
      auto insert = index != cpptype.npos ? index + GI_SCOPE.size() : 0;
      ti.custom = cpptype;
      ti.custom.insert(insert, GI_NS_INTERNAL + GI_SCOPE);
      ti.custom += GI_SUFFIX_CB_TRAIT;
    }

    // declaration always the same
    def.cpp_decl[param_no] = fmt::format("{} {}", cpptype, pname);
    // could be nullable/optional
    if (pinfo.nullable && defaultable) {
      def.cpp_decl[param_no] += " = nullptr";
    } else {
      defaultable = false;
    }

    // some variation in handling
    auto cbw_pname = pname + "_wrap_";
    // pass along empty callback as NULL
    if (scope == SCOPE_CALL) {
      auto s = fmt::format("auto {} = {} ? unwrap (std::move ({}), "
                           "gi::scope_call) : nullptr",
          cbw_pname, pname, pname);
      def.pre_call.push_back(s);
      s = fmt::format("std::unique_ptr<std::remove_pointer<decltype({})>:"
                      ":type> {}_sp ({})",
          cbw_pname, cbw_pname, cbw_pname);
      def.pre_call.push_back(s);
    } else if (scope == SCOPE_NOTIFIED || scope == SCOPE_ASYNC ||
               scope == SCOPE_ASYNC_DEP) {
      // consider async_dep like notified but without a destroy;
      // ownership of the wrapper (pointer) is made part of the async callback
      auto pscope =
          scope != SCOPE_ASYNC ? "gi::scope_notified" : "gi::scope_async";
      def.pre_call.push_back(
          fmt::format("auto {} = {} ? unwrap (std::move ({}), {}) : nullptr",
              cbw_pname, pname, pname, pscope));
      // track wrapper for potential use below
      // (may have to take ownership of another callback wrapper)
      const auto &ctype = pinfo.tinfo.ctype;
      if (scope == SCOPE_ASYNC &&
          ctype.find("AsyncReadyCallback") != ctype.npos &&
          async_cb_wrapper.empty())
        async_cb_wrapper = cbw_pname;
      if (destroy >= 0) {
        def.c_call[destroy] =
            fmt::format("{} ? &{}->destroy : nullptr", cbw_pname, cbw_pname);
      } else {
        assert(scope != SCOPE_NOTIFIED);
        if (scope == SCOPE_ASYNC_DEP && !async_cb_wrapper.empty()) {
          // make async scope callback take ownership of this wrapper callback
          def.pre_call.push_back(
              fmt::format("{}->take_data ({})", async_cb_wrapper, cbw_pname));
        }
      }
    } else {
      throw skip("invalid callback scope " + scope);
    }
    // common part
    def.c_call[param_no] =
        fmt::format("{} ? &{}->wrapper : nullptr", cbw_pname, cbw_pname);
    def.c_call[closure] = cbw_pname;

    return defaultable;
  }

  // returns defaultable status
  bool process_param_in_data(int param_no, const Parameter &pinfo,
      const Options &options, FunctionData &def, bool defaultable)
  {
    auto flags = pinfo.tinfo.flags;
    auto &pname = pinfo.name;
    auto &&cpptype = pinfo.tinfo.cppreftype(pinfo.transfer);
    auto &ctype = pinfo.ptype;
    auto &transfer = pinfo.transfer;
    auto &tinfo = pinfo.tinfo;
    const bool callee = kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD;

    typedef std::vector<std::pair<int, std::string>> callexp_t;
    callexp_t callexps;
    std::string callexp, cpp_decl;

    logger(Log::LOG, "param_in_data {} {}", param_no, pname);
    if (flags & TYPE_VALUE) {
      // value types always passed simply
      // mind g(const)pointer case (const not in normalized girtype)
      cpp_decl = ((flags & TYPE_ENUM) ? cpptype : ctype) + " " + pname;
      callexp = flags & TYPE_ENUM
                    ? fmt::format(GI_NS_SCOPED + "unwrap ({})", pname)
                    : pname;
    } else if (flags & TYPE_CLASS) {
      // otherwise have to mind ownership issues
      if (pinfo.nullable && options.nullable != opt_nullable::PRESENT) {
        // discard nullable
        // NOTE: default value not possible with only forward
        // declaration
        callexp = "nullptr";
      } else {
        // preserve const signature
        // at least, if it makes sense; full transfer on const does not
        // (as we may have to move from it)
        if ((is_const(ctype) && transfer == TRANSFER_NOTHING)) {
          if (flags & TYPE_OBJECT) {
            // side-step ref/unref, in as much as such matters
            cpp_decl = fmt::format("const {} & {}", cpptype, pname);
          } else {
            cpp_decl = fmt::format("const {} {}", cpptype, pname);
          }
        } else {
          cpp_decl = fmt::format("{} {}", cpptype, pname);
        }
        auto wpname = pname;
        // may need to move from a boxed wrapper or string
        bool is_string = (flags & TYPE_BASIC);
        if ((is_string || (flags & TYPE_BOXED)) &&
            pinfo.transfer == TRANSFER_FULL)
          wpname = fmt::format("{}({})", MOVE, pname);
        callexp = fmt::format(GI_NS_SCOPED + "unwrap ({}, {})", wpname,
            get_transfer_parameter(transfer));
      }
    } else if (flags & TYPE_CONTAINER) {
      auto &&cppreftype = tinfo.first.cppreftype(pinfo.transfer);
      auto tmpvar = pname + "_w";
      auto unwrapvar = pname + "_i";
      std::string coltype;
      std::string coleltype = tinfo.first.argtype;
      if (flags & TYPE_MAP) {
        coltype = get_list_type(tinfo);
        coleltype = fmt::format(
            "std::pair<{}, {}>", tinfo.first.argtype, tinfo.second.argtype);
      } else if ((flags & TYPE_LIST) || tinfo.zeroterminated) {
        if (flags & TYPE_LIST)
          coltype = get_list_type(tinfo);
        if (tinfo.zeroterminated)
          coltype = "gi::ZTSpan";
      } else if (tinfo.fixedsize) {
        coltype = fmt::format("gi::FSpan<{}>", tinfo.fixedsize);
      } else {
        // need length parameter
        auto len = tinfo.length;
        auto it = len >= 0 ? paraminfo.find(len) : paraminfo.end();
        if (it == paraminfo.end())
          throw skip("array has invalid length parameter");
        const bool basicvalue = (tinfo.first.flags & TYPE_BASIC) &&
                                (tinfo.first.flags & TYPE_VALUE);
        // annotations are often wrong with low-level buffers
        // so stick to simple interface below in that case
        // (which also works in case of bogus in/out mixup)
        if (!basicvalue ||
            options.basic_container == opt_basic_container::COLLECTION) {
          if (callee)
            def.arg_traits[param_no].args = {param_no, len};
          callexps.emplace_back(len, fmt::format("{}._size()", pname));
          coltype = "gi::DSpan";
        } else {
          // this is an input parameter, so add const
          // (as what will be passed is likely const)
          cpp_decl = fmt::format("const {} * {}", cppreftype, pname);
          // plain basic case, so essentially pass-through
          auto &linfo = it->second;
          callexps.emplace_back(param_no, pname);
          def.cpp_decl[len] = linfo.tinfo.cpptype + " " + linfo.name;
          callexps.emplace_back(len, linfo.name);
          // mark that the alternative above is a possibility
          do_basic_container.insert(opt_basic_container::COLLECTION);
        }
      }
      if (coltype.size() && coleltype.size()) {
        // for a regular function, enable auto-list creation/conversion
        // which then manages ownership as a temporary during call expression
        // but no such otherwise (neither output neither for callee input)
        auto suffix = callee ? "" : "Parameter";
        auto cpp_list = fmt::format("gi::Collection{}<{}, {}, {}>", suffix,
            coltype, coleltype, get_transfer_parameter(pinfo.transfer, true));
        cpp_decl = fmt::format("{} {}", cpp_list, pname);
        // move needed in case of full transfer with single ownership
        // so let's move anyways in all cases
        unwrapvar = fmt::format("{}({})", MOVE, pname);
        auto s = fmt::format("auto {} = unwrap ({}, {})", tmpvar, unwrapvar,
            get_transfer_parameter(pinfo.transfer));
        def.pre_call.push_back(s);
        callexps.emplace_back(param_no, tmpvar);
      } else {
        // plain case above should have handled
        assert(callexps.size());
      }
    } else {
      throw std::logic_error("invalid flags");
    }
    // transfer to def
    if (callexps.empty()) {
      if (callexp.empty())
        throw std::logic_error("unhandled flags");
      callexps.emplace_back(std::move(param_no), std::move(callexp));
    }
    if (cpp_decl.size()) {
      def.cpp_decl.emplace(std::move(param_no), std::move(cpp_decl));
      // a declaration here has no defaults
      // so no defaults further down
      defaultable = false;
    }
    for (auto &&s : callexps) {
      // use intermediate var for clarity
      if (s.first == param_no) {
        // sigh, our input name may actually be an expression
        // FIXME ?? fall back to global data
        auto varname = paraminfo[param_no].name + "_to_c";
        def.pre_call.emplace_back(
            fmt::format("auto {} = {}", varname, s.second));
        def.c_call[s.first] = varname;
      } else {
        def.c_call[s.first] = s.second;
      }
    }
    return defaultable;
  }

  bool process_param_in(int param_no, const Parameter &pinfo,
      const Options &options, FunctionData &def, bool defaultable)
  {
    auto flags = pinfo.tinfo.flags;

    verify_const_transfer(
        pinfo.tinfo, pinfo.transfer, pinfo.direction, kind == EL_CALLBACK);
    if (pinfo.instance) {
      // check sanity
      assert(flags & TYPE_CLASS);
      // method is const if it can operate on a const C struct
      const_method = const_method || is_const(pinfo.ptype);
      def.c_call[param_no] = "gobj_()";
      // NOTE transfer != NOTHING might be a silly case (e.g. _unref)
      // or a useful one (such as some _merge cases)
      defaultable = false;
    } else if (flags & TYPE_CALLBACK) {
      defaultable =
          process_param_in_callback(param_no, pinfo, options, def, defaultable);
    } else {
      defaultable =
          process_param_in_data(param_no, pinfo, options, def, defaultable);
    }
    return defaultable;
  }

  std::string get_list_type(const GeneratorBase::ArgInfo &info)
  {
    assert(info.flags & (TYPE_LIST | TYPE_MAP));
    // should be in qualified GLib.listsuffix form
    assert(info.girname.find("GLib.") == 0);
    auto c = info.girname;
    c.erase(c.begin() + 1, c.begin() + 5);
    return c;
  }

  std::string make_out_type(const Parameter &pinfo)
  {
    const auto &info = pinfo.tinfo;
    if (!info.flags)
      throw skip(fmt::format("return type {} not supported", info.cpptype));
    else if (info.flags & (TYPE_LIST | TYPE_ARRAY)) {
      std::string listtype;
      if (info.flags & TYPE_LIST) {
        listtype = get_list_type(pinfo.tinfo);
      } else if (info.zeroterminated) {
        listtype = "gi::ZTSpan";
      } else if (info.fixedsize) {
        listtype = fmt::format("gi::FSpan<{}>", info.fixedsize);
      } else {
        listtype = "gi::DSpan";
      }
      return fmt::format("gi::Collection<{}, {}, {}>", listtype,
          info.first.argtype, get_transfer_parameter(pinfo.transfer, true));
    } else if (info.flags & TYPE_MAP) {
      return fmt::format("gi::Collection<{}, std::pair<{}, {}>, {}>",
          get_list_type(pinfo.tinfo), info.first.argtype, info.second.argtype,
          get_transfer_parameter(pinfo.transfer, true));
    } else if ((info.flags & TYPE_BOXED) && pinfo.callerallocates) {
      // these always really have transfer full semantics;
      // the caller has performed allocation and thus has ownership
      return info.cppreftype(TRANSFER_FULL);
    } else {
      auto ret = info.cppreftype(pinfo.transfer);
      return ret;
    }
  }

  void process_param_out_array(int param_no, const Parameter &pinfo,
      const Options &options, FunctionData &def)
  {
    auto pname = pinfo.name;
    auto &tinfo = pinfo.tinfo;
    auto transfer = pinfo.transfer;
    bool inout = pinfo.direction == DIR_INOUT;
    bool as_param = (options.output == opt_output::PARAM) || inout;

    std::string exp_size;
    int length = -1;
    Parameter *plinfo = nullptr;

    if (tinfo.fixedsize) {
      def.cpp_decl[param_no] = fmt::format("{} {}[{}]",
          tinfo.first.cppreftype(pinfo.transfer), pname, tinfo.fixedsize);
      exp_size = std::to_string(tinfo.fixedsize);
    } else {
      // must have this (likely checked already though ...)
      length = tinfo.length;
      if (length < 0)
        throw skip("array misses length info");
      auto it = paraminfo.find(length);
      if (it == paraminfo.end())
        throw skip("array has invalid length parameter");
      plinfo = &it->second;
      auto &linfo = it->second;
      inout |= linfo.direction == DIR_INOUT;
      // in case of length parameter it is never treated as an output
      // since the length is a required input
      // so it is always in the declaration
      def.cpp_decl[param_no] =
          tinfo.first.cppreftype(pinfo.transfer) + " * " + pname;
      // also handle length parameter here
      def.cpp_decl[length] = linfo.tinfo.cpptype + " " + linfo.name;
      exp_size = linfo.name;
    }
    // FIXME ?? no real examples here and semantics not entirely clear
    // (probably a single lvalue container argument could be used here)
    if (inout)
      throw skip("inout array not supported");
    // typically lots of different pointer types around (e.g. gpointer)
    // so establish own types all the way
    auto tname_cpp = pname + "_cpptype";
    auto tname_cpp_def = fmt::format(
        "typedef {} {}", tinfo.first.cppreftype(pinfo.transfer), tname_cpp);
    auto tname_c = pname + "_ctype";
    auto tname_c_def =
        fmt::format("typedef traits::ctype<{}>::type {}", tname_cpp, tname_c);
    auto outvar = (pname.size() ? pname : "_ret") + "_o";
    auto cppouttype = make_out_type(pinfo);
    std::string wrap_data;
    std::string cppoutvar;
    // some remaining restriction in callee case
    auto check_callee_support = [this, &pinfo]() {
      if (kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD) {
        throw skip(kind + " " + pinfo.direction + " array not supported");
      }
    };
    if (pinfo.callerallocates) {
      if ((tinfo.first.flags & TYPE_BASIC) &&
          (tinfo.first.flags & TYPE_VALUE)) {
        // optimization; avoid intermediate copy below
        // simply pass along input and done
        def.c_call[param_no] = pname;
        if (length >= 0)
          def.c_call[length] = plinfo->name;
        return;
      }
      check_callee_support();
      // pretty much expected
      if (plinfo && plinfo->direction != DIR_IN)
        throw skip("unexpected length parameter");
      // vexing parse parentheses
      def.pre_call.push_back(tname_cpp_def);
      def.pre_call.push_back(tname_c_def);
      def.pre_call.push_back(fmt::format(
          "detail::unique_ptr<{}> {} ((g_malloc_n({}, sizeof({}))))", tname_c,
          outvar, exp_size, tname_c));
      def.c_call[param_no] = fmt::format("{}.get()", outvar);
      if (length >= 0)
        def.c_call[length] = exp_size;
      wrap_data = "outvar.release()";
      // normalize transfer as we allocated above
      if (transfer != TRANSFER_FULL)
        transfer = TRANSFER_CONTAINER;
    } else {
      if (pinfo.direction != DIR_RETURN) {
        // prepare an output parameter var
        def.pre_call.push_back(tname_cpp_def);
        def.pre_call.push_back(tname_c_def);
        def.pre_call.push_back(fmt::format("{} *{}", tname_c, outvar));
        def.c_call[param_no] = fmt::format("({}) &{}", pinfo.ptype, outvar);
      } else {
        check_callee_support();
        // assign return to var
        def.ret_format = fmt::format("auto {} = {{}}", outvar);
      }
      wrap_data = outvar;
      cppoutvar = pname;
      if (length >= 0) {
        auto &linfo = *plinfo;
        // in either case, lvalue container as parameter/return
        def.cpp_decl[param_no] = fmt::format(cppouttype + " & " + cppoutvar);
        // arranged above
        assert(exp_size == linfo.name);
        if (linfo.direction != DIR_IN) {
          // no size parameter in Cpp signature, use local variable
          def.cpp_decl.erase(length);
          def.pre_call.push_back(
              fmt::format("{} {}", linfo.tinfo.cpptype, linfo.name));
          def.c_call[length] = fmt::format("&{}", linfo.name);
        } else {
          // use/keep input size parameter in Cpp signature
          def.c_call[length] = exp_size;
        }
        // add length to callee arg trait (regardless of in/out, etc)
        def.arg_traits[param_no].args = {param_no, length};
        if (linfo.direction != DIR_OUT) {
          // an additional argument is needed in Cpp signature
          // (as the input size is/can not passed as part of array collection)
          def.cpp_decl_extra[length] =
              fmt::format("{} {}", linfo.tinfo.cpptype, linfo.name);
          // if needed, use any non-void type to mark inout case
          // (no longer passthrough, Cpp signature is plain, C signature is ptr)
          if (linfo.direction == DIR_INOUT) {
            auto &ti = def.arg_traits[length];
            ti.args = {length};
            ti.inout = true;
            ti.custom = "bool";
            ti.transfer = linfo.transfer;
          }
          // TODO however, no known cases and untested at present, so skip
          // (if not already skipped above by inout check)
          check_callee_support();
        }
      }
      // in case return; always use container wrapper as return
      if (pinfo.direction == DIR_RETURN || (!as_param && length >= 0)) {
        // so no cpp parameter
        def.cpp_decl.erase(param_no);
        // output container is temp helper var instead
        // make name if return value
        if (cppoutvar.empty())
          cppoutvar = "_temp_ret";
        def.post_call.push_back(fmt::format("{} {}", cppouttype, cppoutvar));
        def.cpp_outputs.push_back({cppouttype, cppoutvar});
      }
    }
    if (cppoutvar.empty()) {
      cppoutvar = pname + "_temp_wrap_";
      def.post_call.push_back(fmt::format("{} {}", cppouttype, cppoutvar));
    }
    def.post_call.push_back(
        fmt::format("{} = gi::wrap_to<{}>({}, {}, {})", cppoutvar, cppouttype,
            wrap_data, exp_size, get_transfer_parameter(transfer)));
  }

  // returns defaultable status
  bool process_param_out(int param_no, const Parameter &pinfo,
      const Options &options, FunctionData &def, bool defaultable)
  {
    auto pname = pinfo.name;
    auto &tinfo = pinfo.tinfo;
    auto &ctype = pinfo.ptype;
    int flags = tinfo.flags;
    auto &transfer = pinfo.transfer;
    bool inout = pinfo.direction == DIR_INOUT;
    const bool callee = kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD;

    verify_const_transfer(
        tinfo, transfer, pinfo.direction, kind != EL_CALLBACK);
    if (pinfo.instance)
      throw skip("instance out");

    // zero-terminated handled below
    if ((flags & TYPE_ARRAY) && !tinfo.zeroterminated) {
      process_param_out_array(param_no, pinfo, options, def);
      return false;
    }

    if (pinfo.direction == DIR_RETURN) {
      if (tinfo.cpptype == CPP_VOID) {
        def.ret_format = "{}";
      } else {
        auto cpp_ret = make_out_type(pinfo);
        auto tmpvar = "_temp_ret";
        def.ret_format = fmt::format("auto {} = {{}}", tmpvar);
        def.cpp_outputs.push_back({cpp_ret,
            fmt::format(make_wrap_format(tinfo, transfer, cpp_ret), tmpvar)});
      }
      return false;
    }

    // inout never in output tuple
    bool as_param = (options.output == opt_output::PARAM) || inout;

    // one-to-one non-array cases
    // this also handles list/map output with suitable pointer depth
    auto paramtype = make_out_type(pinfo);
    bool optional = false;
    if (as_param) {
      optional = pinfo.optional;
      auto pass = pinfo.optional ? " * " : " & ";
      // no (nullptr) default for pointer output
      // as that leads to call ambiguity with the output tuple variant
      // if no argument given in call
      def.cpp_decl[param_no] = paramtype + pass + pname;
      defaultable = false;
    } else {
      // FIXME ?? could consider defaultable in this case ??
      defaultable = false;
    }

    if ((flags & TYPE_BOXED) && pinfo.callerallocates) {
      if (callee) {
        // parameter can actually be considered an input (pointer/wrapper)
        // (with transfer none, as caller has ownership)
        def.cpp_decl.erase(param_no);
        auto tpinfo = pinfo;
        tpinfo.direction = DIR_IN;
        def.arg_traits[param_no].transfer = tpinfo.transfer = TRANSFER_NOTHING;
        return process_param_in_data(
            param_no, tpinfo, options, def, defaultable);
      }
      // special case; use provided output plain struct directly
      // mimic typical call sequence; declare local struct and pass that
      auto cvar = pname + "_c";
      // such call is typically used for things that do not need cleanup
      // (e.g. GstMapInfo, etc), though GValue is a popular and prominent
      // exception
      // in any case, this means the struct is not opaque
      // so we have no known way to allocate or free
      // (well, we could free a boxed GType but not allocate)
      // so we will really leave it up to the caller
      // and only allocate in case of a GValue or a "plain struct" case
      // (if non-optional)
      // (which is already tricky if custom setup/free needed after all)
      std::string deref;
      if (!as_param) {
        // make pname point to a local variable
        def.pre_call.push_back(fmt::format("{} {}", paramtype, cvar));
        def.pre_call.push_back(fmt::format("auto {} = &{}", pname, cvar));
        // which is then returned
        auto output = cvar;
        def.cpp_outputs.push_back({paramtype, output});
        deref = "*";
      } else if (pinfo.optional) {
        deref = "*";
      }
      if (!pinfo.optional) {
        // this should only really do something for GValue or c-boxed
        // no deref check needed here, as it is ok in either case
        def.pre_call.push_back(
            fmt::format("detail::allocate({}{})", deref, pname));
      }
      def.pre_call.push_back(fmt::format(
          "static_assert(sizeof({}) == sizeof(*({}{}).gobj_()), \"\")",
          tinfo.dtype, deref, pname));
      auto cvalue =
          fmt::format("({}*) ({}{}).gobj_()", tinfo.dtype, deref, pname);
      def.c_call[param_no] =
          deref.size() ? fmt::format("{} ? {} : nullptr", pname, cvalue)
                       : cvalue;
      // no additional post-call
      // (already done above in case of return value)
      return defaultable;
    }

    // otherwise use a temp variable and then wrap that one
    auto outvar = pname + "_o";
    // adjust arginfo for wrapping
    auto opinfo = pinfo;
    auto &otinfo = opinfo.tinfo;
    if (ctype[ctype.size() - 1] != GI_PTR)
      throw skip(pname + "; inconsistent pointer type");
    // adjust type
    opinfo.ptype = ctype.substr(0, ctype.size() - 1);

    // optionally init the temp variable using input (if such)
    std::string init(" {}");
    if (inout && as_param) {
      // operate on a temporary definition
      // only need to re-use transformation of input to call expression
      auto tdef = def;
      tdef.pre_call.clear();
      // optionally tweak the input expression (type adjusted above)
      if (optional)
        opinfo.name.insert(0, "*");
      process_param_in_data(param_no, opinfo, options, tdef, defaultable);
      // be nice, include requested code
      for (auto &&p : tdef.pre_call)
        def.pre_call.emplace_back(p);
      // rest we handle here
      init = fmt::format(" = {}", tdef.c_call[param_no]);
    }

    // always same precall, but slight variation for pointer/ref
    def.pre_call.push_back(opinfo.ptype + " " + outvar + init);
    auto call = std::string("&") + outvar;
    def.c_call[param_no] =
        optional ? fmt::format("{} ? {} : nullptr", pname, call) : call;
    auto wrapf =
        fmt::format(make_wrap_format(otinfo, transfer, paramtype), outvar);
    // assign post call or return
    if (as_param) {
      auto guard = optional ? fmt::format("if ({}) ", pname) : EMPTY;
      auto deref = optional ? "*" : "";
      def.post_call.push_back(
          fmt::format("{}{}{} = {}", guard, deref, pname, wrapf));
    } else {
      def.cpp_outputs.push_back({paramtype, wrapf});
    }
    return defaultable;
  }

  // process info to construct function
  // returns defaultable status
  bool process_param(int param_no, const Parameter &pinfo,
      const Options &options, FunctionData &def, bool defaultable)
  {
    auto &tinfo = pinfo.tinfo;
    auto ctype = tinfo.ctype;
    int flags = tinfo.flags;

    // check type first, so we do not raise any complaints on unknown type
    if (!flags)
      throw skip(
          fmt::format("{} type {} not supported", pinfo.name, tinfo.cpptype),
          skip::OK);
    // sanity check on pointer depth to verify annotation
    // array annotation or out parameter is frequently missing
    auto argdepth = std::count(pinfo.ptype.begin(), pinfo.ptype.end(), GI_PTR);
    auto rpdepth = get_pointer_depth(pinfo.tinfo.ctype);
    if ((kind != EL_SIGNAL) && (argdepth != rpdepth)) {
      // this might happen for an input array of C-boxed structs
      // (rather than the expected array of pointers to something-boxed)
      if ((pinfo.tinfo.flags & TYPE_CONTAINER) &&
          (pinfo.tinfo.first.flags & TYPE_BOXED))
        throw skip(fmt::format("{} {} boxed array not supported (depth {})",
            pinfo.name, pinfo.direction, rpdepth));
      throw skip(fmt::format("inconsistent {} {} pointer depth ({} vs {})",
          pinfo.name, pinfo.direction, rpdepth, argdepth));
    }

    // perhaps this param is part of another's one (e.g. callback)
    // processing various checks to bail out early
    if (def.c_call.find(param_no) != def.c_call.end()) {
      logger(Log::LOG, "call fragment for parameter {} already specified",
          param_no);
      return false;
    } else if (((kind != EL_CALLBACK && kind != EL_SIGNAL) ||
                   (pinfo.closure < 0)) &&
               referenced.count(param_no)) {
      /* managing parameter might come later, so skip this one for now
       * if a call has to be emitted
       * (also; user_data closure in callback can reference itself,
       * and that has to be discovered below
       * userdata parameter also has to be processed below in callback case
       * (to insert proper callexp)
       */
      logger(Log::LOG, "parameter {} referenced elsewhere", param_no);
      return false;
    }
    // on with it now
    // standard transfer, can be overridden
    if (!pinfo.instance)
      def.arg_traits[param_no] = {
          pinfo.transfer, pinfo.direction == DIR_INOUT, {param_no}};
    if (pinfo.direction != DIR_IN) {
      // (in)out or return
      defaultable =
          process_param_out(param_no, pinfo, options, def, defaultable);
    } else {
      defaultable =
          process_param_in(param_no, pinfo, options, def, defaultable);
      // handle callback user_data
      if (kind == EL_CALLBACK) {
        if (pinfo.closure >= 0) {
          if (pinfo.closure != param_no)
            throw skip("invalid closure user_data");
          if (ctype != "gpointer")
            throw skip("invalid type user_data");
          if (found_user_data >= 0)
            throw skip("duplicate user_data");
          found_user_data = param_no;
          // user_data not included in signature (nor transfer)
          def.cpp_decl.erase(param_no);
          def.arg_traits.erase(param_no);
        }
      }
    }

    return defaultable;
  }

  std::string join_outputs(
      const std::vector<FunctionDefinition::Output> &outputs,
      std::string FunctionDefinition::Output::*m,
      std::string (*transform)(std::string) = nullptr)
  {
    std::vector<std::string> temp;
    for (auto &&o : outputs)
      temp.emplace_back(transform ? transform(o.*m) : o.*m);
    return boost::algorithm::join(temp, ", ");
  }

  struct DeclData
  {
    std::string cpptype;
    std::string varname;
    bool is_ref{};
    bool has_init{};
  };

  auto parse_var_name(const std::string &decl)
  {
    std::string name;
    bool is_ref = false;
    bool has_init = false;
    const char *last = nullptr;
    const char *start = nullptr;
    for (auto it = decl.rbegin(); it != decl.rend(); ++it) {
      if (*it == '=') {
        name.clear();
        has_init = true;
        last = nullptr;
        continue;
      }
      if (isspace(*it) && !last)
        continue;
      if (!isalnum(*it) && *it != '_' && name.empty()) {
        if (it == decl.rbegin()) {
          // weird format ?!
          throw skip(
              fmt::format("unexpected declaration {}", decl), skip::TODO);
          break;
        }
        start = &*it + 1;
        name = decl.substr(start - decl.data(), last - start + 1);
      } else if (!last) {
        last = &*it;
      }
      if (*it == '&') {
        is_ref = true;
      }
    }
    // see definition above
    assert(start > decl.data());
    --start;
    while (isspace(*start) && start > decl.data())
      --start;
    auto cpptype = start ? decl.substr(0, start - decl.data() + 1) : "";
    return std::tuple{cpptype, name.size() ? name : decl, is_ref, has_init};
  }

  struct CallArgsData
  {
    std::string cpp_type_name;
    std::string cpp_decl;
    std::string cpp_call;
  };

  CallArgsData make_function_call_args(const Options &options,
      const FunctionDataExtended &def, const std::string &fname)
  {
    CallArgsData result;

    auto struct_name = klasstype + (klasstype.size() ? "_" : "") +
                       unreserve(fname) + "_CallArgs";
    // different options lead to different signature, so needs different type
    std::vector<std::string> tags;
    if (options.output != opt_output::PARAM) {
      struct_name += "In";
      tags.push_back("gi::ca_in_tag");
    }
    if (options.basic_container != opt_basic_container::DEFAULT) {
      struct_name += "BC";
      tags.push_back("gi::ca_bc_tag");
    }
    // in case of a variant, use a first argument tag type to allow unambiguous
    // use of designated initializer syntax in call
    if (!tags.empty())
      result.cpp_decl =
          fmt::format("gi::ca<{}>, ", boost::algorithm::join(tags, ","));

    // let's make it
    std::ostringstream argtype;
    argtype << "struct " << struct_name << " { " << std::endl;

    std::vector<std::string> varnames;
    const std::string argname = "args";
    int count = 0;
    int optional = 0;
    // at least this much is needed in function signature
    // given the type name, a name clash is unlikely
    // but place into args sub-namespace, also for sake of convenience/clarity
    result.cpp_decl +=
        fmt::format("{}{}{} {}", GI_NS_ARGS, GI_SCOPE, struct_name, argname);
    // collect declarations
    for (const auto &e : def.cpp_decl) {
      // skip error, maintained as separate parameter
      auto [cpptype, varname, is_ref, has_init] = parse_var_name(e.second);
      if (e.first != INDEX_ERROR) {
        ++count;
        auto member = e.second;
        // ref is already required
        // if it has init, already optional
        optional += !!has_init;
        if (!is_ref && !has_init) {
          // lookup type
          auto it = paraminfo.find(e.first);
          if (it == paraminfo.end()) {
            // should not happen, where does it come from then
            throw skip("unexpected parameter", skip::TODO);
          }
          auto &pinfo = it->second;
          if (pinfo.nullable || pinfo.optional) {
            ++optional;
            // arrange init
            // callback does not allow (accidental) default construction
            member += (pinfo.tinfo.flags & TYPE_CALLBACK) ? "{nullptr}" : "{}";
          } else {
            member = fmt::format("gi::required<{}> {}", cpptype, varname);
          }
        }
        argtype << indent << member << ";" << std::endl;
        varname.insert(0, argname + '.');
        // mind move-only owning types, so move all non-refs
        if (!is_ref)
          varname = fmt::format("{}({})", MOVE, varname);
      } else {
        result.cpp_decl += ", " + e.second;
      }
      varnames.push_back(varname);
    }
    argtype << "};";

    // only really create if needed
    // perhaps no input, only output arguments, or depending on options
    if (!count || ctx.options.call_args < 0 ||
        optional < ctx.options.call_args || !call_args_decl)
      return result;

    // ok, makes sense, finish up
    result.cpp_type_name = struct_name;
    result.cpp_call = boost::algorithm::join(varnames, ", ");

    // could have seen this type before, in another signature variation
    // NOTE the other parts could be different this time though
    auto [_, inserted] = call_args_types.insert(struct_name);
    if (inserted)
      *call_args_decl << argtype.str() << std::endl << std::endl;

    return result;
  }

  void make_function(const Options &options, const FunctionDataExtended &def,
      const std::string &fname, bool use_call_args = false)
  {
    auto &name = func.name;
    // determine return type
    std::string cpp_ret(CPP_VOID);
    if (def.cpp_outputs.size() == 1) {
      cpp_ret = def.cpp_outputs[0].type;
    } else if (def.cpp_outputs.size() > 1) {
      cpp_ret = fmt::format(
          "std::tuple<{}>", join_outputs(def.cpp_outputs, &Output::type));
    }
    if (options.except == opt_except::EXPECTED) {
      cpp_ret = fmt::format("gi::result<{}>", cpp_ret);
    }
    const auto &cpp_decl = def.cpp_decl_unfolded;
    // generate based on type
    if (kind == EL_SIGNAL) {
      auto decl_name = name;
      // always include instance in signature
      auto cpp_decls = cpp_decl;
      cpp_decls.insert(cpp_decls.begin(), qualify(klasstype, TYPE_OBJECT));
      // the function type specified as gi::signal_proxy template parameter
      // serves 2 purposes;
      // + provide a compile-time type check
      // + select each parameter's corresponding GValue GType
      //   (according to the association defined in gi/value.hpp)
      // so we should make sure to pick the proper type, especially for the
      // latter item, as e.g. gint64 may simply be a long
      // (which would not map to a G_TYPE_INT64)
      auto normalize = [](std::string &subject, const std::string &in,
                           const std::string &sub) {
        if (subject.find(in) == 0 &&
            (subject.size() == in.size() || subject[in.size()] == ' ')) {
          subject.replace(subject.begin(), subject.begin() + in.size(), sub);
        }
      };
      for (auto &decl : cpp_decls) {
        normalize(decl, "gint64", "long long");
        normalize(decl, "guint64", "unsigned long long");
      }
      // convert signal name to valid identifier
      std::replace(decl_name.begin(), decl_name.end(), '-', '_');
      auto ret = fmt::format("gi::signal_proxy<{}({})>", cpp_ret,
          boost::algorithm::join(cpp_decls, ", "));
      oss_decl << fmt::format(
                      "{0} signal_{1}()\n{{ return {0} (*this, \"{2}\"); }}",
                      ret, decl_name, name)
               << std::endl;
    } else {
      // internal namespace for callforward helper parts to avoid clutter
      NamespaceGuard nsg_decl(oss_decl);
      NamespaceGuard nsg_impl(oss_impl);
      if (kind == EL_CALLBACK) {
        nsg_decl.push(GI_NS_INTERNAL, false);
        nsg_impl.push(GI_NS_INTERNAL, false);
      }
      const char *CB_SUFFIX = "_CF";
      CallArgsData ca_data;
      if (use_call_args) {
        try {
          // always use original function name, not a shadow/renamed one
          // otherwise different function (signatures) define same typename
          ca_data = make_function_call_args(options, def, func.name);
        } catch (const skip &exc) {
          auto msg = fmt::format("fname CallArgs failed; {}", exc.what());
          logger(Log::WARNING, msg);
          oss_decl << "// " << msg << std::endl;
        }
        // bail out if not really applicable
        if (ca_data.cpp_type_name.empty())
          return;
        // arrange forward declare
        deps.insert(
            {GI_NS_ARGS, std::string("struct ") + ca_data.cpp_type_name});
      }
      bool vm = kind == EL_VIRTUAL_METHOD;
      auto rfname = unreserve(fname, vm);
      auto make_sig = [&](bool impl) {
        auto funcsuffix = kind == EL_CALLBACK ? CB_SUFFIX : "";
        auto prefix =
            impl
                ? EMPTY
                : (vm ? "virtual "
                      : ((kind != EL_METHOD) && klass.size() ? "static " : ""));
        auto klprefix = klass.size() && impl ? klass + "::" : "";
        auto pure = (vm && !impl ? " = 0" : "");
        if (!impl)
          prefix += GI_INLINE + ' ';
        // virtual method might throw in addition to error parameter
        auto sig =
            prefix +
            fmt::format("{} {}{}{} ({}){}{}{}", cpp_ret, klprefix, rfname,
                funcsuffix,
                use_call_args ? ca_data.cpp_decl
                              : boost::algorithm::join(cpp_decl, ", "),
                (const_method ? " const" : ""),
                (((options.except == opt_except::THROW) || (vm && func.throws))
                        ? ""
                        : " noexcept"),
                pure);
        // remove default values in definition
        static const std::regex re_defaultv(" =[^,)]*", std::regex::optimize);
        return impl ? std::regex_replace(sig, re_defaultv, "") : sig;
      };
      if (!def.cf_ctype.empty())
        oss_decl << def.cf_ctype << ';' << std::endl;
      oss_decl << make_sig(false) << ";" << std::endl;
      oss_impl << make_sig(true) << std::endl;
      // in CallArgs case, simply forward to existing standard function
      if (use_call_args) {
        // no ADL occurs for a method, which finds a declaration in a class
        // but for functions, it might find a similar method in another ns
        // (depending on the arguments involved)
        // mind static functions
        if (klasstype.empty() && kind == EL_FUNCTION)
          rfname.insert(0, ns + GI_SCOPE);
        oss_impl << "{ return " << rfname << " (" << ca_data.cpp_call << "); }"
                 << std::endl
                 << std::endl;
        return;
      }
      // transform to list for joining
      std::vector<std::string> temp;
      for (auto &&e : def.c_call)
        temp.emplace_back(e.second);
      auto call = fmt::format(
          "{} ({})", def.c_callee, boost::algorithm::join(temp, ", "));
      call = fmt::format(def.ret_format, call);
      std::vector<std::string> pre_return;
      std::string returns;
      if (def.cpp_outputs.size() == 1) {
        // avoid -Wpessimizing-move on std::move(x)
        // so pass along as-is
        auto &ret = def.cpp_outputs[0].value;
        returns = fmt::format("return {};", ret);
      } else if (def.cpp_outputs.size() > 1) {
        // wrap in a move()
        // may be needed for move-only types in temp variables
        // however, again avoid -Wpessimizing-move
        // (in case of moving from an expression/temporary)
        // so assign to an intermediate ref and move from that
        // (and should not hurt in other cases)
        // auto moved_outputs = def.cpp_outputs;
        static const std::string tmp_prefix = "tmp_return_";
        int count = 0;
        std::vector<std::string> outputs;
        for (auto &e : def.cpp_outputs) {
          pre_return.push_back(
              fmt::format("auto &&{}{} = {}", tmp_prefix, ++count, e.value));
          outputs.push_back(fmt::format("{}({}{})", MOVE, tmp_prefix, count));
        }
        returns = fmt::format("return std::make_tuple ({});",
            boost::algorithm::join(outputs, ","));
      }
      if (options.except == opt_except::EXPECTED && returns.empty()) {
        // expected<void> is no longer void, so needs explicit return
        returns = "return {};";
      }
      oss_impl << "{" << std::endl;
      // prevent calling NULL in case of vmethod
      // (i.e. Subclass::method != Superclass::method)
      if (kind == EL_VIRTUAL_METHOD) {
        // FIXME in the new approach the class/interface struct entry
        // is only filled if really needed, so it preserves the superclass
        // (unless needed) whether that is NULL or otherwise.
        // In particular, this fallback implementation should not get called
        // (which we could enforce if the new approach becomes the only one).
        // It might still get explicitly called, but then it should first be
        // checked whether the struct entry is non-NULL (as typically done
        // by C code).  So, even in that case, we should not hit the situation
        // below that is forced to return some default value
        // (and that could also be enforced in future).
        // So, eventually, the nasty default return should not apply at runtime.
        // Until then, hoping for the best
        // (though *mm does no better here) ...
        // Log a fairly serious warning, as such should such no longer happen
        // (in the auto-detected or manual specification approach).
        auto retexp = fmt::format(
            "{{ g_critical (\"no method in class struct\"); return {}; }}",
            cpp_ret == CPP_VOID ? EMPTY : "{}");
        retexp = fmt::format("if (!{}) {}", func.functionexp, retexp);
        oss_impl << indent << retexp << std::endl;
      }
      for (const auto &p : def.pre_call)
        oss_impl << indent << p << ';' << std::endl;
      oss_impl << indent << call << ';' << std::endl;
      for (const auto &p : def.post_call)
        oss_impl << indent << p << ';' << std::endl;
      for (const auto &p : pre_return)
        oss_impl << indent << p << ';' << std::endl;
      if (returns.size())
        oss_impl << indent << returns << std::endl;
      oss_impl << "}" << std::endl;
      // generate helper trait type when used as argument in another callback
      if (kind == EL_CALLBACK) {
        auto base = unreserve(fname);
        oss_decl << fmt::format("GI_CB_ARG_CALLBACK_CUSTOM({}, {}, {});",
                        base + GI_SUFFIX_CB_TRAIT, base + GI_SUFFIX_CF_CTYPE,
                        base + CB_SUFFIX)
                 << std::endl;
      }
    }
    if (kind == EL_CALLBACK) { // callback
      // NOTE limited container support at present
      // argument trait/transfers; return type to start with
      auto transfers = make_arg_traits(def.arg_traits, def.c_sig);
      // discard expected trailing callforward parameters in regular declaration
      // (return value also specified in transfers)
      assert(def.arg_traits.size() + (2 - 1) == cpp_decl.size());
      auto cpp_decls = boost::make_iterator_range(
          cpp_decl.begin(), cpp_decl.begin() + def.arg_traits.size() - 1);
      oss_decl << fmt::format("typedef {}callback<{}({}), {}> {}",
                      GI_NS_DETAIL_SCOPED, cpp_ret,
                      boost::algorithm::join(cpp_decls, ", "), transfers,
                      unreserve(name))
               << ";" << std::endl;
    }
  }

  FunctionDefinition process()
  {
    auto &name = func.name;

    const bool callee = kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD;
    // pass over parameters to collect info to assemble signature
    struct signature
    {
      std::string c_ret;
      std::vector<std::string> c_decl;
    };
    signature sig_ctype, sig_ptype;
    // as above, but now each entry contains a comment annotation
    signature annotations;
    std::map<int, int> array_sizes;

    // collect flags into a comment to append to parameter name in declaration
    auto annotate_parameter = [](const FunctionParameter &p) -> std::string {
      if ((p.tinfo.flags & TYPE_VALUE) || p.tinfo.cpptype == CPP_VOID)
        return {};
      auto dir = (p.direction.find(DIR_OUT) != p.direction.npos)
                     ? fmt::format(",{}", p.direction)
                     : "";
      return fmt::format(" /*{}{}{}{}{}*/", p.transfer, dir,
          p.optional ? ",opt" : "", p.nullable ? ",nullable" : "",
          p.callerallocates ? ",ca" : "");
    };

    int callbacks = 0;
    Parameter *callback = nullptr;
    for (auto &p : paraminfo) {
      try {
        auto &pinfo = p.second;
        assert(pinfo.name.size() || pinfo.direction == DIR_RETURN);
        assert((pinfo.direction == DIR_RETURN) == (p.first == INDEX_RETURN));
        assert(pinfo.transfer.size());
        assert(pinfo.direction.size());
        auto annotation = annotate_parameter(pinfo);
        // c signature
        if (p.first == INDEX_RETURN) {
          sig_ctype.c_ret = pinfo.tinfo.ctype;
          annotations.c_ret = annotation;
        } else {
          sig_ctype.c_decl.push_back(pinfo.tinfo.ctype + " " + pinfo.name);
          annotations.c_decl.push_back(annotation);
        }
        auto flags = pinfo.tinfo.flags;
        // also sanity checks
        if (pinfo.direction == DIR_RETURN) {
          // NOTE no more check on none return needed
          // as return type adequately specifies this situation (e.g. cstring_v)
          // and usual caution wrt return "temporary string" then apply
          // (and floating return is handled by callback wrapping code)
          // NOTE constructor return transfer is often marked none = floating
          // so that needs a ref_sink (which wrap() will arrange)
        } else {
          // overall checks
          bool function_type = false;
          if (callee || kind == EL_SIGNAL) {
            function_type = true;
            // no defaults in signatures
            pinfo.nullable = false;
            pinfo.optional = false;
          }
          if (kind == EL_SIGNAL) {
            // a signal is handled much like a callback,
            // so more complex cases need argument trait info (like callback)
            // but that needs more changes in gi support code
            if (pinfo.direction != DIR_IN && !(flags & TYPE_BASIC))
              throw skip(
                  kind + ' ' + pinfo.direction + " parameter not supported");
            if ((flags & TYPE_ARRAY) && !pinfo.tinfo.zeroterminated)
              throw skip(kind + ' ' + pinfo.direction +
                         " array parameter not supported");
          }
          // trigger additional applicable overload generation
          if (!function_type) {
            if (pinfo.direction == DIR_OUT)
              do_output.insert(opt_output::ALT);
            if ((flags & TYPE_CLASS) && pinfo.nullable &&
                pinfo.direction == DIR_IN)
              do_nullable.insert(opt_nullable::ALT);
          }
        }
        // no known cases, but check anyway
        if ((pinfo.direction != DIR_IN) && (flags & TYPE_CALLBACK))
          throw skip(kind + " " + pinfo.direction +
                     " callback parameter not supported");
        track_dependency(deps, pinfo.tinfo);
        // in case of a callback type definition,
        // closure on userdata refers to itself (to identify callback userdata)
        // otherwise, closure attribute should only be on callback argument,
        // but is sometimes also on user_data referring back to callback
        // remove the latter circular reference
        if (!(flags & TYPE_CALLBACK) && pinfo.closure != p.first)
          pinfo.closure = INDEX_DEFAULT;
        // collect parameters that are referenced from elsewhere
        // and as such managed elsewhere
        for (auto p : {&pinfo.closure, &pinfo.destroy, &pinfo.tinfo.length})
          if (*p >= 0)
            referenced.insert(*p);
        array_sizes[pinfo.tinfo.length] = p.first;
        // check if (single) callback parameter
        if (flags & TYPE_CALLBACK) {
          ++callbacks;
          if (pinfo.closure < 0)
            callback = &pinfo;
        }
      } catch (skip &ex) {
        handle_skip(ex);
      }
    }

    // userdata parameter may not be properly annotated
    // (especially for functions considered not introspectable)
    // so, in case of only 1 callback parameter, try to guess userdata
    // (see also issue #85;
    // only limited cases so far, e.g. g_bytes_new_with_free_func)
    // as there is no way to guess the annotated scope,
    // we either have to hope that at least that one is properly annotated,
    // or alternatively let's only guess in specific circumstances
    if (callbacks == 1 && callback &&
        callback->tinfo.girname == GIR_GDESTROYNOTIFY &&
        callback->scope == SCOPE_ASYNC) {
      auto &e = *paraminfo.rbegin();
      // check last parameter
      auto &pinfo = e.second;
      if ((pinfo.name == "data" || ends_with(pinfo.name, "_data")) &&
          (pinfo.tinfo.girname == "gpointer")) {
        // assign as userdata to single callback
        callback->closure = e.first;
        // callback->
        referenced.insert(e.first);
        logger(Log::DEBUG, "{}; guessed {} userdata parameter {}", func.c_id,
            callback->name, pinfo.name);
      }
    }

    // track if func has non-void return
    bool has_return = false;
    // another pass to determine expected normalized parameter type
    // unfortunately some array length size parameters share annotation
    // with the array, so a caller-allocates out array lead to wrong ptr
    // depth (if not compensated for that)
    for (auto &p : paraminfo) {
      try {
        auto &pinfo = p.second;
        // special case; out array length is not so consistent
        // always marked as out (even when passed no-ptr)
        // even when out, then caller-allocates is reversed from a
        // regular out int
        if (pinfo.direction == DIR_OUT) {
          auto it = array_sizes.find(p.first);
          if (it != array_sizes.end()) {
            // normalize based on declaration
            pinfo.direction =
                get_pointer_depth(pinfo.tinfo.ctype) > 0 ? DIR_OUT : DIR_IN;
            // make sure we end up with ptr
            if (pinfo.direction == DIR_OUT)
              pinfo.callerallocates = false;
          }
        }
        pinfo.ptype =
            make_ctype(pinfo.tinfo, pinfo.direction, pinfo.callerallocates);
        // collect deduced signature
        if (p.first == INDEX_RETURN) {
          has_return = pinfo.tinfo.cpptype != CPP_VOID;
          sig_ptype.c_ret = pinfo.ptype;
        } else {
          sig_ptype.c_decl.push_back(pinfo.ptype + " " + pinfo.name);
        }
      } catch (skip &ex) {
        handle_skip(ex);
      }
    }

    // could affect declaration signature
    if (func.throws) {
      // fixed signature
      sig_ctype.c_decl.push_back("GError ** error");
      sig_ptype.c_decl.push_back("GError ** error");
      // stay in sync with parameters, even if empty
      annotations.c_decl.push_back({});
      do_except = {opt_except::GERROR};
      if (!callee)
        do_except.insert(
            ctx.options.expected ? opt_except::EXPECTED : opt_except::THROW);
    } else if (ctx.options.dl && func.lib_symbol) {
      do_except = {
          ctx.options.expected ? opt_except::EXPECTED : opt_except::THROW};
    }

    // we collected enough info so far to reconstruct original declaration
    auto make_declaration = [&](bool def, const std::string name,
                                const signature &sig,
                                const signature *append = nullptr) {
      auto c_sig_fmt = !def ? "{} {} ({})" : "typedef {} (*{}) ({})";
      const signature *actual = &sig;
      signature combined;
      if (append && sig.c_decl.size() == append->c_decl.size()) {
        combined.c_ret = sig.c_ret + append->c_ret;
        for (std::size_t i = 0; i < sig.c_decl.size(); ++i)
          combined.c_decl.push_back(sig.c_decl[i] + append->c_decl[i]);
        actual = &combined;
      }
      return fmt::format(c_sig_fmt, actual->c_ret, name,
          boost::algorithm::join(actual->c_decl, ", "));
    };

    { // dump original/derived declarations
      bool is_signal = kind == EL_SIGNAL;
      const char *prefix = is_signal ? "(signal) " : "";
      // dump both parsed and derived
      for (auto &sig : {sig_ctype, sig_ptype}) {
        auto c_sig =
            make_declaration(kind == EL_CALLBACK, func.c_id, sig, &annotations);
        oss_decl << "// " << prefix << c_sig << ";" << std::endl;
        if (!is_signal)
          oss_impl << "// " << c_sig << ";" << std::endl;
      }
    }

    // name for error return parameter
    const static std::string ERROR_PARAM = "_error";

    auto make_definition = [&](Options options, bool fallback = false) {
      logger(Log::LOG, "generating {} with except {}, output {}, nullable {}",
          func.c_id, (int)options.except, (int)options.output,
          (int)options.nullable);
      FunctionDefinition def;
      def.c_callee = func.functionexp;
      if (callee) {
        auto c_sig = sig_ptype;
        // instance parameter should not be included
        if (kind == EL_VIRTUAL_METHOD) {
          assert(!c_sig.c_decl.empty());
          c_sig.c_decl.erase(c_sig.c_decl.begin());
        }
        def.c_sig = make_declaration(false, "", c_sig, nullptr);
      }

      if (fallback) {
        sig_ctype = signature{};
        logger(Log::INFO, "method {} creating fallback", name);
        // generate virtual method with original C signature as-is
        // however, do qualify/scope type names to avoid mixup with ns etc
        auto ctype = [](const FunctionParameter &pinfo) {
          if (pinfo.ptype.find(GI_SCOPE) == 0)
            return GI_SCOPE + pinfo.tinfo.ctype;
          return pinfo.tinfo.ctype;
        };
        for (const auto &p : paraminfo) {
          auto &pinfo = p.second;
          track_dependency(deps, pinfo.tinfo);
          if (pinfo.instance) {
            sig_ctype.c_decl.push_back(ctype(pinfo));
            def.c_call[p.first] = "gobj_()";
          } else if (pinfo.direction == DIR_RETURN) {
            sig_ctype.c_ret = ctype(pinfo);
            if (pinfo.tinfo.cpptype == CPP_VOID) {
              def.ret_format = "{}";
            } else {
              // assign return to var
              auto outvar = "result_";
              def.ret_format = fmt::format("auto {} = {{}}", outvar);
              def.cpp_outputs.push_back({ctype(pinfo), outvar});
            }
          } else {
            sig_ctype.c_decl.push_back(ctype(pinfo));
            def.cpp_decl[p.first] = ctype(pinfo) + " " + pinfo.name;
            def.c_call[p.first] = pinfo.name;
          }
        }
        // also consider optional GError
        if (func.throws) {
          const int LAST = INDEX_ERROR;
          const std::string evar = "error_";
          auto &p = def.cpp_decl[LAST] = "::GError **" + evar;
          def.c_call[LAST] = evar;
          sig_ctype.c_decl.push_back(p);
        }
        // commit/specify that a method def was made
        def.name = name;
      }

      // arrange for dynamic load if so requested
      std::string symbol_name;
      if (ctx.options.dl && func.lib_symbol) {
        symbol_name = "_symbol_name";
        def.pre_call.push_back(
            fmt::format("const char *{} = \"{}\"", symbol_name, def.c_callee));
        def.c_callee = fmt::format(
            "detail::load_symbol(internal::_libs(), {})", symbol_name);
      }

      if (kind != EL_CALLBACK) {
        // enforce deduced function signature by cast
        // i.e. type cast to deduced function type
        static const std::string call_wrap_t = "call_wrap_t";
        static const std::string call_wrap_v = "call_wrap_v";
        def.pre_call.push_back(make_declaration(
            true, call_wrap_t, fallback ? sig_ctype : sig_ptype));
        def.pre_call.push_back(fmt::format("{} {} = ({}) {}", call_wrap_t,
            call_wrap_v, call_wrap_t, def.c_callee));
        def.c_callee = call_wrap_v;
        if (symbol_name.size()) {
          std::string check_exp;
          if (options.except == opt_except::EXPECTED) {
            check_exp =
                fmt::format("if (!{}) "
                            "return gi::detail::make_unexpected(gi::detail::"
                            "missing_symbol_error({}))",
                    call_wrap_v, symbol_name);
          } else if (options.except == opt_except::THROW) {
            check_exp = fmt::format(
                "if (!{}) "
                "gi::detail::try_throw(gi::detail::missing_symbol_error({}))",
                call_wrap_v, symbol_name);
          } else if (options.except == opt_except::GERROR) {
            // this could potentially cover up quite some error
            // (in case of null error parameter, but so be it ...)
            check_exp = fmt::format(
                "if (!{0}) {{"
                "if ({2}) *{2} = gi::detail::missing_symbol_error({1}); "
                "return {3}; "
                "}}",
                call_wrap_v, symbol_name, ERROR_PARAM, has_return ? "{}" : "");
          } else {
            // no other options should be possible
            // (due to options arranged for above)
            assert(false);
          }
          def.pre_call.push_back(check_exp);
        }
      }

      if (fallback)
        return def;

      // process parameters from last to first
      // that way we can track whether it is possible to specify a default
      // which is only acceptable if so for all later parameters
      // (or later ones are dropped from signature)
      // init defaultable status to start
      // trailing GError prevents default
      bool defaultable = options.except != opt_except::GERROR;

      // in case of callback;
      // check if some parameter is specified as closure (aka userdata)
      // as it may sadly be missing
      bool has_closure = false;
      if (kind == EL_CALLBACK) {
        for (auto it = paraminfo.rbegin(); it != paraminfo.rend(); ++it) {
          auto &&e = *it;
          if (e.second.closure >= 0) {
            has_closure = true;
            break;
          }
        }
      }

      // if userdata/closure not specified for a closure,
      // accept last parameter as userdata based on name
      if (kind == EL_CALLBACK && !has_closure && paraminfo.size()) {
        auto &e = *paraminfo.rbegin();
        if (e.second.name == "data" || ends_with(e.second.name, "_data")) {
          e.second.closure = e.first;
          logger(Log::DEBUG, "{}; guessed userdata parameter {}", func.c_id,
              e.second.name);
        }
      }

      for (auto it = paraminfo.rbegin(); it != paraminfo.rend(); ++it) {
        auto &&e = *it;
        try {
          defaultable =
              process_param(e.first, e.second, options, def, defaultable);
        } catch (const skip &ex) {
          handle_skip(ex);
        }
      }

      // reverse some results
      std::reverse(def.cpp_outputs.begin(), def.cpp_outputs.end());

      // add potentially missing transfers from length parameters
      // that were not added previously
      // FIXME reorganize data to avoid such
      if (errors.empty() &&
          (kind == EL_CALLBACK || kind == EL_VIRTUAL_METHOD)) {
        for (auto &&p : paraminfo) {
          auto index = p.first;
          if (def.cpp_decl.count(index) && !def.arg_traits.count(index)) {
            if (array_sizes.count(index)) {
              def.arg_traits[p.first].transfer = p.second.transfer;
              def.arg_traits[p.first].args = {p.first};
            } else {
              handle_skip(skip("missing callback transfer info"));
            }
          }
        }
      }

      // enforce deduced function signature by cast
      for (auto &&p : def.c_call) {
        auto it = paraminfo.find(p.first);
        if (it != paraminfo.end()) {
          auto &pinfo = it->second;
          p.second = fmt::format("({}) ({})", pinfo.ptype, p.second);
        }
      }

      // callback/callforward; add additional call/userdata parameters
      if (kind == EL_CALLBACK && found_user_data >= 0 &&
          paraminfo.count(found_user_data)) {
        // need a type for the C function to call
        auto cfc = unreserve(func.name) + GI_SUFFIX_CF_CTYPE;
        def.cf_ctype =
            make_declaration(true, cfc, fallback ? sig_ctype : sig_ptype);
        // which is specified in an extra paramater
        static const std::string param_func = "_call";
        def.cpp_decl[INDEX_CF] = fmt::format("{} {}", cfc, param_func);
        // userdata also given in parameter; re-use original parameter name
        // (which should have been used in regular in parameter processing)
        def.cpp_decl[INDEX_CF + 1] =
            fmt::format("gpointer {}", paraminfo.at(found_user_data).name);
        // call supplied function
        def.c_callee = param_func;
        // def.c_call userdata part should have been arranged as usual
      }

      // if marked throws, GError argument is not mentioned in argument list
      // deal with it here
      switch (options.except) {
        case opt_except::THROW:
        case opt_except::EXPECTED:
          // could be here due to dl only
          if (!func.throws)
            break;
          def.pre_call.push_back("GError *error = NULL");
          def.c_call[def.c_call.size() + 10] = "&error";
          if (options.except == opt_except::THROW) {
            def.post_call.push_back("gi::check_error (error)");
          } else {
            // terminating ; added later
            def.post_call.push_back(
                "if (error) return gi::detail::make_unexpected (error)");
          }
          break;
        case opt_except::GERROR: {
          // simulate optional output error parameter
          options.output = opt_output::PARAM;
          Parameter err;
          err.optional = true;
          err.direction = DIR_OUT;
          err.transfer = TRANSFER_FULL;
          err.name = ERROR_PARAM;
          parse_typeinfo("GLib.Error", err.tinfo);
          err.ptype = err.tinfo.ctype = "GError**";
          // process non-nullable to make sure there is no signature
          // ambiguity
          try {
            process_param_out(INDEX_ERROR, err, options, def, false);
            auto &ti = def.arg_traits[INDEX_ERROR];
            ti.transfer = err.transfer;
            auto lparam =
                paraminfo.empty() ? 0 : paraminfo.crbegin()->first + 1;
            ti.args = {lparam};
          } catch (const skip &ex) {
            handle_skip(ex);
          }
        }
        default:
          break;
      }
      return def;
    };

    // mild check on overload conflict
    // TODO improve check ??
    // what about defaultable arguments and overlap that causes
    std::set<std::vector<std::string>> signatures;

    // pass over to produce cpp declarations and content
    // generate each option
    FunctionDataExtended def;
    if (!check_errors()) {
      for (auto &&output : do_output) {
        for (auto &&except : do_except) {
          for (auto &&nullable : do_nullable) {
            // context for this option run
            Options options(except, output, nullable);
          resume:
            do_basic_container.clear();
            def = make_definition(options);

            // only care about callbacks with (trailing) user_data
            auto last = paraminfo.end();
            --last;
            if (kind == EL_CALLBACK && found_user_data != last->first)
              errors.push_back("not a callback since no user_data");

            if (check_errors())
              goto exit;

            // duplicate could happen for a special boxed output
            // which is never returned in a tuple
            // normalize without const
            // conflict might otherwise occur e.g. in case of
            // (string output, string input nullable)

            auto cpp_sig = def.cpp_decl_unfolded;
            static const std::regex re_const("const ", std::regex::optimize);
            for (auto &d : cpp_sig)
              d = std::regex_replace(d, re_const, EMPTY);
            if (signatures.count(cpp_sig)) {
              logger(Log::DEBUG,
                  "discarding duplicate signature for " + func.c_id);
            } else {
              auto fname =
                  !callee && !func.shadows.empty() ? func.shadows : name;
              make_function(options, def, fname);
              // mark ok
              def.name = name;
              signatures.insert(cpp_sig);
              // also produce a CallArgs variant if desired
              // full signature is needed
              if (ctx.options.call_args >= 0 && func.lib_symbol &&
                  options.nullable == opt_nullable::PRESENT)
                make_function(options, def, fname, true);
              // another signature alternative
              // unlike other ones, this one is discovered in make_definition
              // so dynamically add another run to these loops
              if (ctx.options.basic_collection && func.lib_symbol &&
                  do_basic_container.size()) {
                options.basic_container = *do_basic_container.begin();
                goto resume;
              }
            }
          }
        }
      }
    }

  exit:
    // generate a raw fallback virtual method upon failure
    if (ctx.options.classfull && def.name.empty() &&
        kind == EL_VIRTUAL_METHOD) {
      Options options(
          opt_except::NOEXCEPT, opt_output::PARAM, opt_nullable::PRESENT);
      def = make_definition(options, true);
      if (def.name.size())
        make_function(options, def, name);
    }

    return def;
  }

  FunctionDefinition process(const pt::ptree::value_type *entry,
      const std::vector<Parameter> *params, std::ostream &out,
      std::ostream &impl)
  {
    assert(!params || !entry);

    logger(Log::LOG, "processing " + func.c_id);
    try {
      // check if we made it all the way
      // remove callback from known types if failure
      bool success = kind == EL_CALLBACK ? false : true;
      ScopeGuard g([&] {
        if (!success)
          ctx.repo.discard(func.name);
      });

      if (entry) {
        collect_node(*entry);
      } else {
        int pno = 0;
        for (auto &p : *params) {
          int index = pno;
          if (p.direction == DIR_RETURN) {
            index = INDEX_RETURN;
          } else if (p.instance) {
            index = INDEX_INSTANCE;
          } else {
            ++pno;
          }
          paraminfo[index] = p;
        }
      }

      auto def = process();
      // only trigger remove on callback as such has a top-level GIR entry
      // whereas e.g. a method does not
      // (and its name might conflict/match a top-level one)
      success = success || (def.name.size() > 0);

      out << oss_decl.str() << std::endl;
      impl << oss_impl.str() << std::endl;
      return def;
    } catch (std::runtime_error &ex) {
      auto err = fmt::format("// FAILURE on {}; {}", func.c_id, ex.what());
      out << err << std::endl;
      impl << err << std::endl;
    }

    return FunctionDefinition();
  }
};

} // namespace

std::string
make_arg_traits(const std::map<int, FunctionDefinition::ArgTrait> &traits,
    const std::string &c_sig)
{
  // check if in 1-to-1 case
  bool complex = false;
  for (auto &&t : traits) {
    if (t.second.args.size() > 1) {
      complex = true;
      break;
    }
  }
  // another pass to set up trait
  std::vector<std::string> transfers;
  std::string ret_transfer;
  for (auto &&t : traits) {
    auto &ti = t.second;
    auto tt = GeneratorBase::get_transfer_parameter(ti.transfer, true);
    // skip return
    if (t.first < 0) {
      ret_transfer = tt;
      continue;
    }
    if (complex) {
      auto transform = [](int index) { return std::to_string(index); };
      auto li = boost::algorithm::join(
          ti.args | boost::adaptors::transformed(transform), ", ");
      auto custom = ti.custom.empty() ? "void" : ti.custom;
      tt = fmt::format("detail::arg_info<{}, {}, {}, detail::args_index<{}>>",
          tt, ti.inout ? "true" : "false", custom, li);
    } else if (ti.inout) {
      tt = fmt::format("detail::arg_info<{}, true>", tt);
    }
    transfers.emplace_back(std::move(tt));
  }
  assert(!ret_transfer.empty());
  auto ret = fmt::format("{}, std::tuple<{}>", ret_transfer,
      boost::algorithm::join(transfers, ", "));
  // also add C signature if needed
  if (complex)
    ret += ", " + c_sig;
  return ret;
}

// process a function (or alike) and callback
//
// for a call;
// const on parameter is maintained, always added for string
// return value and output parameters always non-const
//
// callback; likewise (but not output params for now)
//
// klass: actual name of class being declared/defined (e.g. xxxBase)
// klasstype: intended target type of class (e.g. xxx), used in signal instance
// / constructor returns: last processed function definition (possibly only 1)
FunctionDefinition
process_element_function(GeneratorContext &_ctx, const std::string _ns,
    const pt::ptree::value_type &entry, std::ostream &out, std::ostream &impl,
    const std::string &klass, const std::string &klasstype,
    GeneratorBase::DepsSet &deps, std::ostream *call_args,
    bool allow_deprecated)
{
  ElementFunction func;

  auto &kind = func.kind = entry.first;
  auto &node = entry.second;

  auto &name = func.name = get_name(node);
  auto c_name = (kind == EL_SIGNAL || kind == EL_VIRTUAL_METHOD)
                    ? name
                    : get_attribute(node,
                          kind == EL_CALLBACK ? AT_CTYPE : AT_CIDENTIFIER);
  func.c_id = (kind == EL_VIRTUAL_METHOD) ? klasstype + "::" + c_name : c_name;
  // global qualifier needed as c_name might otherwise resolve wrongly
  // e.g. if g_mkdir is macro to mkdir (and resolve to namespaced mkdir)
  // in case of dl loading, the symbol name is needed as-is
  std::string qualifier =
      (kind == EL_FUNCTION || kind == EL_METHOD) && !_ctx.options.dl ? "::"
                                                                     : "";
  func.functionexp = kind == EL_VIRTUAL_METHOD
                         ? fmt::format("get_struct_()->{}", c_name)
                         : qualifier + c_name;

  func.throws = get_attribute<int>(node, AT_THROWS, 0);
  func.shadows = get_attribute(node, AT_SHADOWS, "");
  func.lib_symbol =
      (kind == EL_FUNCTION || kind == EL_METHOD || kind == EL_CONSTRUCTOR);

  FunctionGenerator gen(
      _ctx, _ns, func, klass, klasstype, deps, call_args, allow_deprecated);
  gen.const_method = (kind == EL_METHOD) && _ctx.options.const_method;
  return gen.process(&entry, nullptr, out, impl);
}

FunctionDefinition
process_element_function(GeneratorContext &_ctx, const std::string _ns,
    const ElementFunction &func, const std::vector<Parameter> &params,
    std::ostream &out, std::ostream &impl, const std::string &klass,
    const std::string &klasstype, GeneratorBase::DepsSet &deps)
{
  FunctionGenerator gen(_ctx, _ns, func, klass, klasstype, deps, nullptr);
  return gen.process(nullptr, &params, out, impl);
}
