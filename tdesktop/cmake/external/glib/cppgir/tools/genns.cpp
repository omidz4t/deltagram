#include "genns.hpp"
#include "fs.hpp"
#include "function.hpp"
#include "genbase.hpp"

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>

#include <boost/property_tree/xml_parser.hpp>

#include <iostream>
#include <ostream>
#include <sstream>
#include <tuple>

namespace
{
class File : public std::ostringstream
{
  std::string ns;
  std::string fname;
  bool preamble;
  bool guard;
  bool nsdir;
  NamespaceGuard nsg;

  static std::string root;
  static bool changed;

public:
  // ok, single threaded processing
  static void set_root(const std::string &dir) { root = dir; }

  static void set_changed(bool c) { changed = c; }

  static std::string prepdirs(
      const std::string &_ns, const std::string &_fname, bool nsdir)
  {
    fs::path p(root);
    if (nsdir)
      p /= tolower(_ns);
    fs::create_directories(p);
    p /= tolower(_fname);
    return p.string();
  }

  File(const std::string &_ns, const std::string _fname, bool _need_ns = true,
      bool _need_guard = true, bool _nsdir = true)
      : ns(_ns), fname(_fname), preamble(_need_ns), guard(_need_guard),
        nsdir(_nsdir), nsg(*this)
  {
    write_pre();
  }

  std::string get_rel_path() const
  {
    return nsdir ? (fs::path(tolower(ns)) / tolower(fname)).string()
                 : tolower(fname);
  }

private:
  void write_pre()
  {
    *this << "// AUTO-GENERATED\n\n";
    auto definc = fmt::format("_GI_{}_{}_", toupper(ns), toupper(fname));
    for (auto &v : definc)
      if (!isalnum(v))
        v = '_';
    if (guard) {
      *this << "#ifndef " << definc << std::endl;
      *this << "#define " << definc << std::endl;
      *this << std::endl;
    }
    if (preamble)
      nsg.push(ns);
  }

  void write_post()
  {
    if (preamble)
      nsg.pop();
    if (guard)
      *this << "#endif" << std::endl;
    finish();
  }

  static std::string read(const std::string &fname)
  {
    std::ifstream f(fname);
    std::ostringstream oss;
    oss << f.rdbuf();
    return oss.str();
  }

  void finish()
  {
    auto fullname = prepdirs(ns, fname, nsdir);
    auto content = str();

    // this is a bit racy if multiple parties try to write
    // worst case multiple will/should write the same content
    auto write = changed ? read(fullname) != content : true;
    if (write) {
      std::ofstream output(fullname);
      output << content;
    }
  }

public:
  ~File() { write_post(); }
};

std::string File::root;
bool File::changed{};

class NamespaceGeneratorImpl : private GeneratorBase, public NamespaceGenerator
{
  std::string version_;
  std::vector<std::string> deps_;
  pt::ptree root_;
  pt::ptree tree_;
  // helper exp
  std::regex re_unqualify_;
  bool allow_deprecated_{};

public:
  NamespaceGeneratorImpl(GeneratorContext &ctx, const std::string &filename)
      : GeneratorBase(ctx, "")
  {
    logger(Log::INFO, "reading {}", filename);
    pt::read_xml(filename, root_);
    // extract dependencies
    for (auto &&n : root_.get_child("repository")) {
      if (n.first == "include") {
        auto &&name = get_name(n.second);
        auto &&version = get_attribute(n.second, AT_VERSION);
        deps_.emplace_back(name + "-" + version);
        logger(Log::INFO, indent + "dependency " + deps_.back());
      }
    }
    // all else is in a subtree
    tree_ = root_.get_child("repository.namespace");
    ns = get_name(tree_);
    version_ = get_attribute(tree_, AT_VERSION);
    re_unqualify_ =
        std::regex(fmt::format("^{}(::|\\.)", ns), std::regex::optimize);
  }

  std::string get_ns() const { return ns + "-" + version_; }

  std::vector<std::string> get_dependencies() const { return deps_; }

private:
  // in most cases no problems arise if both M1.SomeName and M2.SomeOtherName
  // both map to GSomeName
  // but some generated code, like declare_XXX template specialization assumes
  // a 1-to-1 mapping, as should be the case in with sane GIR
  // however, if the latter are somehow not sane, then duplicate definitions
  // might arise (ODR and all that), although it *really is* a GIR bug
  //
  // so try to mitigate that here
  // check that only 1 (qualified) girname/cppname claims/maps to a ctype
  void check_odr(const std::string &cpptype, const std::string &ctype) const
  {
    // verify this ctype has not been seen before
    // a particular ctype can only be defined/claimed by one GIR symbol
    // (in one module), otherwise lots of things will go wrong
    // GIRs also have an "alias" concept, so that should be used instead
    auto conflict = ctx.repo.check_odr(cpptype, ctype);
    if (!conflict.empty()) {
      throw std::runtime_error(
          fmt::format("{} maps to {}, already claimed by {};\n"
                      "Please verify/fix GIRs and/or add ignore as needed.",
              cpptype, ctype, conflict));
    }
  }

  std::string make_declare(bool decl_ctype, const std::string &cpptype,
      const std::string &ctype) const
  {
    check_odr(cpptype, ctype);
    return fmt::format(
        "template<> struct declare_{}_of<{}>\n{{ typedef {} type; }}; ",
        (decl_ctype ? "ctype" : "cpptype"), (decl_ctype ? cpptype : ctype),
        (decl_ctype ? ctype : cpptype));
  }

  void process_element_alias(const pt::ptree &node, std::ostream &out) const
  {
    std::ostringstream oss;
    auto name = get_name(node);
    // skip some stuff
    if (name.find("autoptr") != name.npos)
      throw skip("autoptr", skip::OK);
    auto ctype = get_attribute(node, AT_CTYPE);

    std::string deftype;
    TypeInfo tinfo;
    auto tnode = node.get_child(EL_TYPE);
    auto btype = get_name(tnode);
    parse_typeinfo(btype, tinfo);
    if (tinfo.flags & TYPE_BASIC) {
      // if typedef refers to a basic type, let's refer here to (global
      // scope) C type (this is mostly the case, e.g. GstClockTime, etc)
      deftype = GI_SCOPE + ctype;
    } else if (tinfo.flags) {
      assert(tinfo.cpptype.size());
      // otherwise we alias to the corresponding wrapped type
      // (e.g. GtkAllocation)
      deftype = tinfo.cpptype;
    }
    auto aliasfmt = "typedef {} {};\n";
    if (deftype.size())
      out << fmt::format(aliasfmt, deftype, name);
    // also mind ref type
    if (tinfo.flags & TYPE_BOXED) {
      assert(deftype.size());
      out << fmt::format(
          aliasfmt, deftype + GI_SUFFIX_REF, name + GI_SUFFIX_REF);
    }
    out << std::endl;
  }

  void process_element_enum(const pt::ptree::value_type &n, std::ostream &out,
      std::ostream *_out_impl) const
  {
    auto &kind = n.first;
    auto &node = n.second;
    std::ostringstream oss;
    auto name = get_name(node);
    auto ctype = get_attribute(node, AT_CTYPE);

    if (_out_impl) {
      auto &out_impl = *_out_impl;
      // need to generate namespaced function members
      std::ostringstream oss_decl;
      std::ostringstream oss_impl;
      DepsSet dummy;
      for (const auto &n : node) {
        if (n.first == EL_FUNCTION) {
          process_element_function(n, oss_decl, oss_impl, "", "", dummy);
        }
      }
      if (oss_decl.tellp()) {
        auto enumns = name + "NS_";
        NamespaceGuard ns_d(out);
        ns_d.push(enumns, false);
        out << oss_decl.str();

        NamespaceGuard ns_i(out_impl);
        ns_i.push(enumns, false);
        out_impl << oss_impl.str();
      }
      return;
    }

    // enum name might end up matching (uppercase) define
    name = unreserve(name);

    // otherwise generate enum
    NamespaceGuard nsg(oss);
    nsg.push(ns);
    // auto-adjust underlying type to avoid warnings;
    // outside the range of underlying type ‘int’
    // (no forward declare of enum in C, so type should be complete)
    oss << fmt::format(
        "enum class {} : std::underlying_type<{}>::type {{\n", name, ctype);
    std::set<std::string> names;
    for (const auto &n : node) {
      if (n.first == EL_MEMBER) {
        auto value = get_attribute(n.second, AT_CIDENTIFIER);
        auto name = get_name(n.second);
        // some enums/flags declare different names with same value
        // which seems to confuse gobject-introspection
        // (e.g. GstVideoFrameFlags, GstVideoBufferFlags, etc)
        // and then they end up with repeated/duplicate name in GIR
        // so let's only pick the first one of those
        if (!std::get<1>(names.insert(name))) {
          logger(Log::WARNING, "{} skipping duplicate {}", ctype, value);
          continue;
        }
        oss << indent
            << fmt::format("{} = {},\n", unreserve(toupper(name)), value);
      }
    }
    oss << "};\n\n";
    // add operators in case of flag = bitfield
    if (kind == EL_FLAGS)
      oss << fmt::format("GI_FLAG_OPERATORS({})\n\n", name);
    nsg.pop();

    // declare ctype info
    nsg.push(GI_REPOSITORY_NS);
    name = ns + "::" + name;
    oss << make_declare(true, name, ctype) << std::endl;
    oss << make_declare(false, name, ctype) << std::endl;
    oss << std::endl;

    // declare gtype info
    auto gtype = get_attribute(node, AT_GLIB_GET_TYPE, "");
    if (gtype.size()) {
      oss << fmt::format("template<> struct declare_gtype_of<{}>\n"
                         "{{ static GType get_type() {{ return {}(); }} }};",
                 name, gtype)
          << std::endl;
      oss << std::endl;
    }

    // in case of a flag = bitfield, also mark it as such
    if (kind == EL_FLAGS) {
      oss << fmt::format(
                 "template<> struct is_bitfield<{}> : public std::true_type\n"
                 "{{}};",
                 name)
          << std::endl;
      oss << std::endl;
    }

    oss << std::endl;
    nsg.pop();
    out << oss.str() << std::endl;
  }

  void process_element_const(const pt::ptree &node, std::ostream &out) const
  {
    auto name = get_name(node);
    std::ostringstream oss;
    try {
      ArgInfo tinfo = parse_arginfo(node);
      auto cpptype = tinfo.cpptype;

      auto value = get_attribute(node, AT_CTYPE);
      std::string cast;
      std::string namesuffix;
      if (tinfo.flags & TYPE_BASIC) {
        if (tinfo.flags & TYPE_CLASS) {
          // special string case
          // need some suffix to make the variable (pointer) const
          cpptype = "gchar";
          namesuffix = "[]";
        } else {
          // normalize back to C (e.g. char*)
          cpptype = tinfo.ctype;
        }
      } else {
        // need to cast to enum type (flags/enum)
        if (tinfo.flags & TYPE_ENUM) {
          cast = fmt::format("({}) ", cpptype);
        } else if (!(tinfo.flags & TYPE_VALUE)) {
          throw skip("constant type " + cpptype + " not supported");
        }
        // accept alias typedef
      }
      // avoid clashes with existing defines
      name = unreserve(name);
      // NOTE what about an inline var in C++17
      // recall that const implies static in C++
      oss << fmt::format("GI_MODULE_INLINE const {} {}{} = {}{};", cpptype,
          name, namesuffix, cast, value);
    } catch (skip &ex) {
      oss << "// SKIP constant " << name << "; " << ex.what();
    }
    out << oss.str() << std::endl << std::endl;
  }

  void process_element_property(const pt::ptree::value_type &entry,
      std::ostream &out, std::ostream &impl, const std::string &klass,
      DepsSet &deps) const
  {
    auto &node = entry.second;

    ArgInfo tinfo;
    auto name = get_name(node);
    auto read = get_attribute<int>(node, AT_READABLE, 1);
    auto write = get_attribute<int>(node, AT_WRITABLE, 1);
    tinfo = parse_arginfo(node);

    auto &&cpptype = tinfo.cpptype;
    if (!tinfo.flags)
      throw skip("unknown type " + cpptype);
    // let's not go here for now
    if (tinfo.flags & TYPE_CONTAINER)
      throw skip("container property not supported", skip::TODO);

    track_dependency(deps, tinfo);

    // directly write all in decl
    auto &oss = out;
    (void)impl;

    auto decl_name = name;
    std::replace(decl_name.begin(), decl_name.end(), '-', '_');
    std::string proptype = "property_proxy";
    if (!read) {
      proptype = "property_proxy_write";
    } else if (!write) {
      proptype = "property_proxy_read";
    }
    proptype = GI_NS_SCOPED + proptype;

    auto qklass = qualify(klass, TYPE_OBJECT);
    if (write)
      oss << fmt::format("{3}<{0}, {4}> property_{2}()\n"
                         "{{ return {3}<{0}, {4}> (*this, \"{1}\"); }}",
                 cpptype, name, decl_name, proptype, qklass)
          << std::endl;
    if (read)
      oss << fmt::format("const {3}<{0}, {4}> property_{2}() const\n"
                         "{{ return {3}<{0}, {4}> (*this, \"{1}\"); }}",
                 cpptype, name, decl_name, proptype, qklass)
          << std::endl;
    oss << std::endl;
  }

  void process_element_field(const pt::ptree::value_type &entry,
      std::ostream &out, std::ostream &impl, const std::string &klass,
      const std::string &klasstype, DepsSet &deps) const
  {
    auto &node = entry.second;

    auto name = get_name(node);
    auto readable = get_attribute<int>(node, AT_READABLE, 1);
    auto writable = get_attribute<int>(node, AT_WRITABLE, 1);
    auto priv = get_attribute<int>(node, AT_PRIVATE, 0);

    ArgInfo tinfo;
    try {
      tinfo = parse_arginfo(node);

      // only very plain basic fields
      // type must be known
      // no private
      // no int* cookies or other strange things
      if (!tinfo.flags || priv ||
          get_pointer_depth(tinfo.ctype) != tinfo.pdepth ||
          is_volatile(tinfo.ctype))
        return;
    } catch (...) {
      // simply fail silently here and never mind
      return;
    }

    if (!(tinfo.flags & (TYPE_VALUE | TYPE_CLASS)))
      return;

    // unconditionally reserve this to avoid clash with non-field member
    ElementFunction func;
    func.kind = EL_METHOD;
    func.name = unreserve(name, true);
    func.c_id = klasstype + "::" + name;

    Parameter instance;
    // real one, no base
    parse_typeinfo(klasstype, instance.tinfo);
    instance.instance = true;
    instance.name = "obj";
    instance.direction = DIR_IN;

    // field parameter
    Parameter param;
    param.tinfo = tinfo;
    param.name = "_value";
    param.transfer = TRANSFER_NOTHING;

    // common helper lambda for below
    auto make_wrapper = [&](const std::string &funcname,
                            const std::string funcdef,
                            const std::vector<Parameter> &params) {
      func.functionexp = funcname;
      // buffer result
      std::ostringstream oss;
      auto def = ::process_element_function(
          ctx, ns, func, params, out, oss, klass, klasstype, deps);
      // now write all if wrapping accessor did not fail
      if (def.name.size())
        impl << funcdef << std::endl << oss.str();
    };

    // a static helper is used in the following,
    // rather than a compact local lambda,
    // as the latter does not always cast to any function type
    // also, internal casts are applied in the helper to handle enum/int
    // and pointer type conversions

    // also, the ctype info is derived from the cpptype as with func
    // argument due to either
    // + ctype missing, e.g. enum,
    // + or minor discrepancies, e.g. gpointer vs gconstpointer

    if (readable) {
      param.direction = DIR_RETURN;
      param.tinfo.ctype = make_ctype(param.tinfo, param.direction, false);
      instance.tinfo.ctype = fmt::format("const {}*", instance.tinfo.dtype);
      // define helper function
      auto funcname = "_field_" + name + "_get";
      auto funcdef = fmt::format(
          "GI_MODULE_STATIC_OR_INLINE {} {} ({} {}) {{ return ({}) obj->{}; }}",
          param.tinfo.ctype, funcname, instance.tinfo.ctype, instance.name,
          param.tinfo.ctype, name);
      make_wrapper(funcname, funcdef, {param, instance});
    }

    // not safe to assume any particular ownership of some struct field
    // so let's not meddle with it other than the very basic cases
    if (writable && (tinfo.flags & TYPE_VALUE)) {
      param.direction = DIR_IN;
      param.tinfo.ctype = make_ctype(param.tinfo, param.direction, false);
      instance.tinfo.ctype = instance.tinfo.dtype + "*";
      // define helper function
      auto funcname = "_field_" + name + "_set";
      auto funcdef =
          fmt::format("GI_MODULE_STATIC_OR_INLINE void {} ({} {}, {} {}) {{ "
                      "obj->{} = (decltype(obj->{})) {}; }}",
              funcname, instance.tinfo.ctype, instance.name, param.tinfo.ctype,
              param.name, name, name, param.name);
      // void return
      Parameter vparam;
      parse_typeinfo(GIR_VOID, vparam.tinfo);
      vparam.direction = DIR_RETURN;
      make_wrapper(funcname, funcdef, {vparam, param, instance});
    }
  }

  FunctionDefinition process_element_function(
      const pt::ptree::value_type &entry, std::ostream &out, std::ostream &impl,
      const std::string &klass, const std::string &klasstype, DepsSet &deps,
      std::ostream *call_args = nullptr) const
  {
    return ::process_element_function(ctx, ns, entry, out, impl, klass,
        klasstype, deps, call_args, allow_deprecated_);
  }

  // unqualify (current ns qualifed) type
  std::string unqualify(const std::string &name) const
  {
    return std::regex_replace(name, re_unqualify_, "");
  }

  static std::string get_record_filename(const std::string &rname, bool impl)
  {
    return tolower(rname) + (impl ? "_impl" : "") + ".hpp";
  }

  static std::string make_include(const std::string &hname, bool local)
  {
    if (hname.empty())
      return hname;
    char open = local ? '"' : '<';
    char close = local ? '"' : '>';
    std::ostringstream oss;
    oss << "#include " << open << hname << close;
    return oss.str();
  }

  // make include for a qualified dependency
  // (not in current namespace)
  std::string make_dep_include(const std::string &girname) const
  {
    auto lname = unqualify(girname);
    return lname.size() && !is_qualified(lname)
               ? make_include(get_record_filename(lname, false), true) + '\n'
               : EMPTY;
  }

  std::string make_dep_declare(const DepsSet &deps, bool only_ns = false) const
  {
    std::ostringstream oss;
    NamespaceGuard nsg(oss);
    std::string last_ns;
    for (const auto &de : deps) {
      if (last_ns != de.first) {
        oss << std::endl;
        nsg.pop();
        last_ns = de.first;
        if (!last_ns.empty())
          nsg.push(last_ns, false);
      }
      if (only_ns && last_ns.empty())
        continue;
      const auto &d = de.second;
      if (d.size() > 7 && (d[6] == ' ' || d[7] == ' ')) {
        // there is a space in there, so it is not just a type name
        // but rather struct/class XXX (a CallArgs case)
        oss << d << ';' << std::endl;
        continue;
      }
      auto c = unqualify(d);
      if (!is_qualified(c))
        oss << "class " << c << ";" << std::endl;
    }
    // apparently guard destructor runs too late
    nsg.pop();
    return oss.str();
  }

  static std::string make_conditional_include(
      const std::string &hname, bool local)
  {
    if (hname.empty())
      return hname;

    auto templ = R"|(
#if defined(__has_include)
#if __has_include({}{}{})
#include {}{}{}
#endif
#endif
)|";
    char open = local ? '"' : '<';
    char close = local ? '"' : '>';
    return fmt::format(templ, open, hname, close, open, hname, close);
  }

  static constexpr const char *const CLASS_PLACEHOLDER{"CLASS_PLACEHOLDER"};

  // minor convencience helper type
  struct TypeClassInfo
  {
    // (optionally) gir qualified
    std::string parentgir;
    // info on type-struct
    TypeInfo ti;
  };

  TypeClassInfo collect_type_class_info(const std::string &girname) const
  {
    // class struct is skipped above,
    // but we do look for it here to find *Class/*Interface struct
    TypeClassInfo result;
    auto &repo = ctx.repo;
    auto &node = repo.tree(girname).second;
    result.parentgir = get_attribute(node, AT_PARENT, "");
    // NOTE parent might be in different ns
    if (result.parentgir.size())
      result.parentgir = repo.qualify(result.parentgir, girname);
    auto cpptype = get_attribute(node, AT_GLIB_TYPE_STRUCT, "");
    if (cpptype.empty())
      throw skip(girname + " missing type-struct info");
    cpptype = repo.qualify(cpptype, girname);
    parse_typeinfo(cpptype, result.ti);
    // more useful in this setting
    result.ti.cpptype = unqualify(result.ti.cpptype);
    if (result.ti.dtype.empty())
      throw skip(girname + " missing C type-struct info");
    return result;
  }

  typedef std::vector<std::pair<std::string, FunctionDefinition>>
      VirtualMethods;
  void process_element_record_class(const pt::ptree::value_type &entry,
      const std::vector<TypeInfo> &interfaces, const std::string &decl,
      const std::string &impl, const VirtualMethods &methods,
      std::ostream &out_decl, std::ostream &out_impl) const
  {
    auto &node = entry.second;
    auto name = get_name(node);
    bool interface = (entry.first == EL_INTERFACE);

    // run up parent hierarchy to check if all those can be properly
    // generated
    auto class_info = collect_type_class_info(name);
    TypeClassInfo parent_class_info;
    // no parent for interface
    if (class_info.parentgir.size()) {
      parent_class_info = collect_type_class_info(class_info.parentgir);
      auto rec_info = parent_class_info;
      while (rec_info.parentgir.size())
        rec_info = collect_type_class_info(rec_info.parentgir);
    }

    // collect ok interfaces
    std::vector<TypeInfo> itfs_info;
    for (auto &&itf : interfaces) {
      try {
        itfs_info.emplace_back(collect_type_class_info(itf.girname).ti);
        // base class needs to be declared
        out_decl << make_dep_include(itf.girname);
      } catch (...) {
      }
    }
    out_decl << std::endl;

    // put into inner namespace to avoid name clash
    // declaration part
    NamespaceGuard ns_decl(out_decl);
    ns_decl.push(ns);

    ns_decl.push(GI_NS_IMPL);
    ns_decl.push(GI_NS_INTERNAL);

    // class definition
    // explicitly specify a non-public non-virtual destructor
    auto def_templ = R"|(
class {0}
{{
typedef {0} self;
public:
typedef {1} instance_type;
typedef {2} {3}_type;

{6}
struct TypeInitData;

protected:
{5} ~{0}() = default;
static {5} void {3}_init (gpointer {3}_struct, gpointer );

{4}
}};

)|";

    std::string kind = interface ? "interface" : "class";
    const auto klassnamedef = class_info.ti.cpptype + "Def";
    // used for interfaces
    const std::string suffix_class_impl = "ClassImpl";
    const auto klassname =
        class_info.ti.cpptype + (interface ? suffix_class_impl : "");

    // conflict check and type init lists
    std::string conflict_check, type_init, type_init_calc;
    conflict_check.reserve(1024);
    type_init.reserve(1024);
    type_init_calc.reserve(1024);
    std::string calc_indent = indent + indent;
    for (auto &&method : methods) {
      auto &&n = method.first;
      conflict_check +=
          fmt::format("using GI_MEMBER_CHECK_CONFLICT({}) = self;\n", n);
      type_init +=
          fmt::format("{}GI_MEMBER_DEFINE({}, {})\n", indent, klassname, n);
      type_init_calc +=
          fmt::format("{}{}GI_MEMBER_HAS_DEFINITION(SubClass, DefData, {})",
              type_init_calc.empty() ? "" : ",\n", calc_indent, n);
    }

    // (note that e.g. xlib cases prefix might not help, or ns interference)
    // drop inline mark on pure virtual interface functions;
    // does not quite make sense and might otherwise lead to compiler
    // warnings
    static const std::regex re_inline(GI_INLINE + ' ', std::regex::optimize);
    out_decl << fmt::format(def_templ, klassnamedef, qualify(name, TYPE_OBJECT),
        class_info.ti.dtype, kind, std::regex_replace(decl, re_inline, ""),
        GI_INLINE, conflict_check);
    if (interface)
      out_decl << fmt::format("using {}Impl = detail::InterfaceImpl<{}>;", name,
                      klassnamedef)
               << std::endl;

    // avoid ambiguous unqualified name lookup of friend class below
    // (as the scopes of parent classes are involved then as well)
    auto class_templ = R"|(
class {0}: public {1}
{{
friend class internal::{4};
typedef {0} self;
typedef {1} super;

protected:
using super::super;
{2}
{3}
}};

)|";

    auto classextra = R"|(
private:
// make local helpers private
using super::get_struct_;
using super::gobj_;

protected:
// disambiguation helper types
{}
)|";

    // qualification helper; ensure impl::internal qualified
    // (also adds current ns if needed)
    auto implqualify = [&](const std::string &cpptype) {
      auto result = cpptype;
      auto pos = result.find(GI_SCOPE.c_str());
      auto insert = GI_SCOPE + GI_NS_IMPL + GI_SCOPE + GI_NS_INTERNAL;
      if (pos != result.npos) {
        result.insert(pos, insert);
      } else {
        insert = ns + insert + GI_SCOPE;
        result.insert(0, insert);
      }
      return result;
    };

    // collect interface types and helper types
    std::ostringstream oss_types;
    std::vector<std::string> itfs;
    for (auto &&itf : itfs_info) {
      auto kname = itf.cpptype + suffix_class_impl;
      kname = implqualify(kname);
      itfs.push_back(kname);
      // fall back to ctype as it needs to be fully qualified
      static const std::regex re_descope(GI_SCOPE, std::regex::optimize);
      auto tprefix = std::regex_replace(itf.dtype, re_descope, "");
      oss_types << fmt::format("typedef {} {}_type;\n", kname, tprefix);
    }
    auto extra = interface ? EMPTY : fmt::format(classextra, oss_types.str());
    // determine baseclasses
    // qualified klassnamedef to avoid ambiguous parent class lookup
    // (when used as a typedef within class)
    std::string superclass =
        interface ? fmt::format("detail::InterfaceClassImpl<{}>", name + "Impl")
                  : fmt::format("detail::ClassTemplate<{}, {}{}{}>",
                        implqualify(klassnamedef),
                        implqualify(parent_class_info.ti.cpptype),
                        itfs.size() ? ", " : "",
                        boost::algorithm::join(itfs, ", "));
    static const std::regex re_virtual("virtual ", std::regex::optimize);
    static const std::regex re_pure("= 0", std::regex::optimize);
    auto idecl = std::regex_replace(decl, re_virtual, "");
    idecl = std::regex_replace(idecl, re_pure, "override");
    // add macro for optional warning suppression
    if (!interface)
      out_decl << GI_CLASS_IMPL_BEGIN << std::endl << std::endl;
    out_decl << fmt::format(
        class_templ, klassname, superclass, extra, idecl, klassnamedef);

    // type init
    // at this later stage as it contains template definitions
    // that refer to the above class
    auto type_init_templ = R"|(
struct {0}::TypeInitData
{{
{1}
template<typename SubClass>
constexpr static TypeInitData factory()
{{
  {3}using DefData = detail::DefinitionData<SubClass, TypeInitData>;
  return {{
{2}
  }};
}}
}};
)|";
    out_decl << fmt::format(type_init_templ, klassnamedef, type_init,
        type_init_calc, methods.empty() ? "// " : "");
    // end internal ns
    ns_decl.pop(1);

    if (!interface)
      out_decl << GI_CLASS_IMPL_END << std::endl
               << std::endl
               << fmt::format("using {}Impl = detail::ObjectImpl<{}, {}::{}>;",
                      name, name, GI_NS_INTERNAL, klassname)
               << std::endl
               << std::endl;

    // implementation part
    NamespaceGuard ns_impl(out_impl);
    ns_impl.push(ns);

    ns_impl.push(GI_NS_IMPL);
    ns_impl.push(GI_NS_INTERNAL);

    auto &ctype = class_info.ti.dtype;
    out_impl << fmt::format(
        "void {}::{}_init (gpointer {}_struct, gpointer factory)\n{{\n",
        klassnamedef, kind, kind);
    out_impl << indent
             << fmt::format(
                    "{} *methods = ({} *) {}_struct;\n", ctype, ctype, kind);
    // avoid warning if no methods
    out_impl << indent << "(void) methods;" << std::endl << std::endl;
    // init data from factory
    out_impl
        << indent
        << "auto init_data = GI_MEMBER_INIT_DATA(TypeInitData, factory);\n";
    out_impl << indent << "(void) init_data;" << std::endl << std::endl;

    for (auto &&method : methods) {
      auto &&n = method.first;
      auto &&def = method.second;
      std::vector<std::string> args, transfers;
      for (auto &&arg : def.cpp_decl)
        args.push_back(arg.second);
      auto cpp_return =
          def.cpp_outputs.size() ? def.cpp_outputs[0].type : CPP_VOID;
      auto sig = fmt::format(
          "{} (*) ({})", cpp_return, boost::algorithm::join(args, ", "));
      std::string transferargs, precheck;
      if (def.arg_traits.empty()) {
        // only arrange to call the raw methods
        // if new-style detection (or explicit specification) is in place
        precheck = " && factory";
        transferargs = "std::nullptr_t";
      } else {
        transferargs = make_arg_traits(def.arg_traits, def.c_sig);
      }
      // add a hard cast to deal with const differences (e.g. string vs
      // const char*)
      out_impl << indent
               << fmt::format("if (init_data.{0}{3}) methods->{0} = (decltype "
                              "(methods->{0})) "
                              "gi::detail::method_wrapper<self, {1}, "
                              "{2}>::wrapper<&self::{0}_>;",
                      n, sig, transferargs, precheck)
               << std::endl;
    }
    out_impl << "}" << std::endl << std::endl;

    auto iimpl =
        std::regex_replace(impl, std::regex(CLASS_PLACEHOLDER), klassname);
    out_impl << iimpl;
  }

  std::vector<TypeInfo> record_collect_interfaces(
      const pt::ptree::value_type &entry, const TypeInfo &current,
      const TypeInfo &parent, DepsSet &deps) const
  {
    auto &repo = ctx.repo;
    auto &node = entry.second;
    auto name = get_name(node);

    // listed interfaces also include parent's interfaces
    // so we should subtract those for good measure

    auto collect_interfaces = [&](const pt::ptree &node,
                                  const std::string &basename) {
      std::set<std::string> result;
      auto p = node.equal_range(EL_IMPLEMENTS);
      assert(basename.size());
      for (auto &q = p.first; q != p.second; ++q) {
        auto n = get_name(q->second, std::nothrow);
        if (n.size()) {
          result.insert(repo.qualify(n, basename));
        }
      }
      return result;
    };

    std::vector<std::string> itfs;
    // ensure all GIR names fully qualified
    auto itfs_local = collect_interfaces(node, current.girname);
    if (parent.girname.size()) {
      // qualify relative to parent
      auto itfs_parent =
          collect_interfaces(repo.tree(parent.girname).second, parent.girname);
      // only keep those not matching a parent's interface (both fully
      // qualified)
      for (auto &&n : itfs_local) {
        if (itfs_parent.find(n) == itfs_parent.end())
          itfs.push_back(n);
      }
    } else {
      std::copy(itfs_local.begin(), itfs_local.end(), std::back_inserter(itfs));
    }

    std::vector<TypeInfo> interfaces;
    for (auto &n : itfs) {
      TypeInfo itf;
      parse_typeinfo(n, itf);
      // should know about this interface by now
      // but let's continue anyway
      if (!itf.flags) {
        logger(Log::WARNING, "{} implements unknown interface {}", name, n);
        continue;
      }
      deps.insert({"", itf.cpptype});
      interfaces.push_back(itf);
    }

    return interfaces;
  }

  // returns (decl header name, impl header name)
  std::tuple<std::string, std::string> process_element_record(
      const pt::ptree::value_type &entry, bool onlyverify,
      std::ostream *call_args = nullptr) const
  {
    auto &kind = entry.first;
    auto &node = entry.second;
    auto name = get_name(node);

    logger(Log::LOG, "checking {} {} {}", kind, name, onlyverify);

    TypeInfo current;
    parse_typeinfo(name, current);
    // only consider real type, but keep info around
    if (!current.flags)
      return std::make_tuple("", "");
    auto &ctype = current.dtype;
    // could happen for a GType only case (e.g. GstFraction)
    if (!ctype.size())
      throw skip("no c:type info");
    bool is_object_base = (current.girname == GIR_GOBJECT);

    // check parent
    TypeInfo parent;
    auto &repo = ctx.repo;
    // 1 special case here
    if (kind == EL_OBJECT && !is_object_base) {
      auto parentgir = get_attribute(node, AT_PARENT, "");
      if (parentgir.empty())
        throw skip("missing parent class");
      parse_typeinfo(parentgir, parent);
      if (!parent.flags)
        throw skip("unknown parent " + parentgir);
      // parent might be found, but might have problems of its own
      // so that needs to be checked recursively
      // (at least if within current ns)
      if (onlyverify && !is_qualified(parentgir)) {
        try {
          process_element_record(repo.tree(parentgir), onlyverify);
        } catch (const skip &ex) {
          throw skip(std::string("parent problem; ") + ex.what());
        }
      }
    }

    if (onlyverify || is_object_base)
      return std::make_tuple("", "");

    // no throwing or giving up after this point
    // we have now committed to coming up with a class
    // declaration/definition (even if it is a pretty empty one)
    logger(Log::LOG, "processing {} {}", kind, name);

    // generate classes in subnamespace
    const std::string nsbase("base");
    auto namebase = name + "Base";
    // avoid qualification later on with standard ns (e.g. Gst)
    auto qnamebase = nsbase + "::" + namebase;

    // collect output of members and their dependencies
    std::ostringstream oss_decl, oss_class_decl;
    std::ostringstream oss_impl, oss_class_impl;
    VirtualMethods vmethods;
    DepsSet deps;
    for (const auto &n : node) {
      auto el = n.first;
      int introspectable = get_attribute<int>(n.second, AT_INTROSPECTABLE, 1);
      try {
        // try even if marked introspectable
        // some are useful in non-runtime setting
        if (el == EL_FUNCTION || el == EL_CONSTRUCTOR || el == EL_METHOD ||
            el == EL_SIGNAL) {
          process_element_function(
              n, oss_decl, oss_impl, qnamebase, name, deps, call_args);
        } else if (el == EL_VIRTUAL_METHOD && introspectable &&
                   ctx.options.classimpl) {
          // placeholder replaced suitably later on
          auto def = process_element_function(n, oss_class_decl, oss_class_impl,
              CLASS_PLACEHOLDER, name, deps, call_args);
          if (def.name.size())
            vmethods.push_back({def.name, def});
        } else if (el == EL_FIELD && introspectable) {
          process_element_field(n, oss_decl, oss_impl, qnamebase, name, deps);
        } else if (el == EL_PROPERTY && introspectable) {
          process_element_property(n, oss_decl, oss_impl, qnamebase, deps);
        }
      } catch (std::runtime_error &ex) {
        handle_exception(n, ex);
      }
    }

    // actual output to file
    auto fname_decl = get_record_filename(name, false);
    File out_decl(ns, fname_decl, false);
    auto fname_impl = get_record_filename(name, true);
    File out_impl(ns, fname_impl, false);

    // a superclass needs full declaration (not only forward declaration)
    // skip include for GObject, which would lead to object.hpp,
    // which is not generated in GObject namespace, but rather provided by gi
    // so it actually references gi/object.hpp, which is only works if gi/
    // is part of the include path, which is a bit confusing/messy
    // (as it is/should always be referenced by gi/xyz, e.g. gi/gi.hpp)
    if (parent.girname != GIR_GOBJECT)
      out_decl << make_dep_include(parent.girname);
    out_decl << std::endl;

    // implemented interfaces are also dependency
    std::vector<TypeInfo> interfaces =
        record_collect_interfaces(entry, current, parent, deps);

    // namespace in decl before forward class decl
    NamespaceGuard ns_decl(out_decl);
    ns_decl.push(ns);

    // all declarations are included prior to implementation
    // forward class declarations in declaration
    deps.erase({"", current.cpptype});
    out_decl << make_dep_declare(deps);
    out_decl << std::endl;

    // also forward declare 'oneself' since only base is defined below
    out_decl << "class " << name << ";" << std::endl;
    out_decl << std::endl;

    // namespace in impl following includes
    NamespaceGuard ns_impl(out_impl);
    ns_impl.push(ns);

    // base class subnamespace
    ns_decl.push(nsbase);
    ns_impl.push(nsbase);

    // class definition
    auto obj_templ = R"|(
#define {3} {4}::{0}
class {0} : public {2}
{{
typedef {2} super_type;
public:
typedef {1} BaseObjectType;

{0} (std::nullptr_t = nullptr) : super_type() {{}}

BaseObjectType *gobj_() {{ return (BaseObjectType*) super_type::gobj_(); }}
const BaseObjectType *gobj_() const {{ return (const BaseObjectType*) super_type::gobj_(); }}
BaseObjectType *gobj_copy_() const {{ return (BaseObjectType*) super_type::gobj_copy_(); }}

)|";

    auto boxed_templ = R"|(
#define {2} {3}::{0}
class {0} : public {1}
{{
typedef {1} super_type;
public:

{0} (std::nullptr_t = nullptr) : super_type() {{}}

)|";

    auto gtype = get_attribute(node, AT_GLIB_GET_TYPE, "");
    auto basedef = toupper(fmt::format("GI_{}_{}_BASE", ns, name));
    // class definition
    bool is_variant = false;
    if (kind == EL_OBJECT) {
      out_decl << fmt::format(
          obj_templ, namebase, ctype, parent.cpptype, basedef, nsbase);
    } else if (kind == EL_INTERFACE) {
      out_decl << fmt::format(
          obj_templ, namebase, ctype, "gi::InterfaceBase", basedef, nsbase);
    } else {
      auto tmpl = GI_NS_DETAIL_SCOPED +
                  (gtype.size() ? "GBoxedWrapperBase" : "CBoxedWrapperBase");
      auto parent = fmt::format("{}<{}, {}>", tmpl, namebase, ctype);
      // override if special fundamental boxed-like case
      if ((is_variant = (current.girname == GIR_GVARIANT))) {
        parent = "detail::VariantWrapper";
        gtype.clear();
      }
      out_decl << fmt::format(boxed_templ, namebase, parent, basedef, nsbase);
    }

    if (gtype.size()) {
      out_decl << fmt::format(
          "static GType get_type_ () G_GNUC_CONST {{ return {}(); }} ", gtype);
      out_decl << std::endl << std::endl;
    }

    // add some helpers to obtain known implemented interfaces
    for (auto &&_itf : interfaces) {
      // temp extend for wrapping below
      ArgInfo itf;
      (TypeInfo &)itf = _itf;
      auto decl_fmt = "{0} {1}interface_ (gi::interface_tag<{0}>)";
      auto impl_fmt = decl_fmt;
      auto decl = fmt::format(decl_fmt, itf.cpptype, "");
      out_decl << GI_INLINE << ' ' << decl << ";" << std::endl << std::endl;
      // wrap a properly casted extra reference (ensure no sink'ing)
      auto impl = fmt::format(impl_fmt, itf.cpptype, (namebase + "::"));
      out_impl << impl << std::endl;
      auto towrap =
          fmt::format("({}::BaseObjectType*) gobj_copy_()", itf.cpptype);
      auto w =
          fmt::format(make_wrap_format(ArgInfo{itf}, TRANSFER_FULL), towrap);
      out_impl << fmt::format("{{ return {}; }}", w) << std::endl << std::endl;
      // conversion operator
      auto op_decl = fmt::format("operator {} ()", itf.cpptype);
      auto op_impl = fmt::format(
          "{}::{}\n{{ return interface_ (gi::interface_tag<{}>()); }}",
          namebase, op_decl, itf.cpptype);
      out_decl << GI_INLINE << ' ' << op_decl << ';' << std::endl << std::endl;
      out_impl << op_impl << std::endl << std::endl;
    }

    out_decl << oss_decl.str();
    out_decl << "}; // class" << std::endl;
    out_decl << std::endl;
    ns_decl.pop();

    out_impl << oss_impl.str();
    out_impl << std::endl;
    ns_impl.pop();

    // optionally include supplements/overrides for this class
    auto include_extra = [&](File &out, bool impl) {
      for (auto &&suffix : {"_extra_def", "_extra"}) {
        auto header =
            (fs::path(tolower(ns)) / get_record_filename(name + suffix, impl))
                .string();
        out << make_conditional_include(header, false) << std::endl;
      }
    };
    include_extra(out_decl, false);
    include_extra(out_impl, true);

    ns_decl.push(GI_REPOSITORY_NS);
    // make fragment to define final class
    {
      NamespaceGuard nst(out_decl);
      nst.push(ns, false);
      auto supertype = basedef;
      auto reftype = name + GI_SUFFIX_REF;
      if (kind != EL_OBJECT && kind != EL_INTERFACE && !is_variant) {
        // boxed base templates define a few members with CppType return type
        // so in the CppTypeBase defined above that is still CppTypeBase
        // again add the template as subclass but now with final CppType
        auto tmpl = GI_NS_DETAIL_SCOPED +
                    (gtype.size() ? "GBoxedWrapper" : "CBoxedWrapper");
        supertype = fmt::format(
            "{}<{}, {}, {}, {}>", tmpl, name, ctype, basedef, reftype);
        // forward declaration of corresponding ref type
        out_decl << fmt::format("class {};", reftype) << std::endl << std::endl;
      }
      auto fmtclass =
          "class {0} : public {1}\n"
          "{{ typedef {1} super_type; using super_type::super_type; }};\n";
      out_decl << fmt::format(fmtclass, name, supertype) << std::endl;

      // in case of boxed, also define ref type
      if (kind != EL_OBJECT && kind != EL_INTERFACE && !is_variant) {
        auto tmpl = GI_NS_DETAIL_SCOPED +
                    (gtype.size() ? "GBoxedRefWrapper" : "CBoxedRefWrapper");
        supertype = fmt::format("{}<{}, {}, {}>", tmpl, name, ctype, basedef);
        out_decl << std::endl
                 << fmt::format(fmtclass, reftype, supertype) << std::endl;
      }
    }
    // GInitiallyUnowned is typedef'ed to GObject
    // so we have to avoid duplicate definition
    // FIXME generally no way to check for that using Gir, provide some
    // override here ??
    if (current.girname != GIR_GINITIALLYUNOWNED) {
      // declare type info
      out_decl << make_declare(false, ns + "::" + name, ctype) << std::endl;
      out_decl << std::endl;
    }
    ns_decl.pop();

    // now process the virtual method class parts
    if (ctx.options.classimpl && (kind == EL_OBJECT || kind == EL_INTERFACE)) {
      try {
        process_element_record_class(entry, interfaces, oss_class_decl.str(),
            oss_class_impl.str(), vmethods, out_decl, out_impl);
      } catch (const skip &ex) {
        logger(Log::WARNING, "skipping class generation for {}; {}", name,
            ex.what());
      }
    }

    return std::make_tuple(fname_decl, fname_impl);
  }

  std::string handle_exception(
      const pt::ptree::value_type &n, const std::runtime_error &ex) const
  {
    auto &el = n.first;
    const auto &name = get_name(n.second, std::nothrow);
    auto ex_skip = dynamic_cast<const skip *>(&ex);
    if (!ex_skip || ex_skip->cause == skip::INVALID) {
      auto msg = fmt::format("{} {} {}; {}",
          (ex_skip ? "skipping" : "EXCEPTION processing"), el, name, ex.what());
      Log level = ex_skip ? Log::WARNING : Log::ERROR;
      if (check_suppression(ns, el, name))
        level = Log::DEBUG;
      logger(level, msg);
    } else {
      logger(Log::DEBUG, "discarding {} {}; {}", el, name, ex.what());
    }
    return name;
  }

  typedef std::function<void(const pt::ptree::value_type &)> entry_processor;
  void process_entries(const pt::ptree &node, const entry_processor &proc)
  {
    for (auto it = node.begin(); it != node.end(); ++it) {
      auto &&n = *it;
      if (n.first == PT_ATTR)
        continue;
      try {
        proc(n);
      } catch (std::runtime_error &ex) {
        auto name = handle_exception(n, ex);
        // in any case give up on this name
        ctx.repo.discard(name);
      }
    }
  }

  bool visit_ok(const pt::ptree::value_type &n) const
  {
    const auto &girname = get_name(n.second, std::nothrow);
    auto ninfo = ctx.repo.lookup(girname);
    if (ninfo && !(ninfo->info && (ninfo->info->flags & TYPE_PREDEFINED))) {
      logger(Log::LOG, "visiting {} {}", n.first, girname);
      return true;
    } else {
      return false;
    }
  }

  const char *process_libs()
  {
    // also find needed shared libraries in case of dlopen
    auto sl = get_attribute(tree_, AT_SHARED_LIBRARY, "");
    std::vector<std::string> shlibs, qshlibs;
    boost::split(shlibs, sl, boost::is_any_of(","));
    for (auto &l : shlibs)
      if (l.size())
        qshlibs.push_back(fmt::format("\"{}\"", l));

    auto h_libs = "_libs.hpp";
    File libs(ns, h_libs);

    auto libs_templ = R"|(
namespace internal {{

GI_INLINE_DECL std::vector<const char*> _libs()
{{ return {{{0}}}; }}

}} // internal
)|";
    libs << fmt::format(libs_templ, boost::join(qshlibs, ","));

    return h_libs;
  }

public:
  std::string process_tree(const std::vector<std::string> &dep_headers)
  {
    logger(Log::INFO, "processing namespace {} {}", ns, version_);
    // set state for ns processing
    ctx.repo.set_ns(ns);
    File::set_root(ctx.options.rootdir);
    File::set_changed(ctx.options.only_changed);

    // check if deprecated should pass for this ns
    allow_deprecated_ = ctx.match_ignore.matches("deprecated", ns, {version_});

    // optionally process libs
    auto h_libs = ctx.options.dl ? process_libs() : "";

    auto h_types = "_types.hpp";
    File types(ns, h_types);
    // index run
    // collect info by name
    // also gather alias/type info
    entry_processor proc_index = [&](const pt::ptree::value_type &n) {
      const auto &name = get_name(n.second, std::nothrow);
      auto deprecated = get_attribute<int>(n.second, AT_DEPRECATED, 0);
      if (deprecated && !allow_deprecated_)
        return;
      auto &el = n.first;
      // redirect to oblivion
      // empty name might originate from a glib:boxed with glib:name attribute
      // discard those as well, as that is a glib type without C-type
      // (which are not really useful in our situation)
      if (name.empty() || ctx.match_ignore.matches(ns, el, {name})) {
        logger(Log::INFO, "ignoring {} {}", el, name);
      } else {
        ctx.repo.add(name, n);
        if (n.first == EL_ALIAS && visit_ok(n)) {
          process_element_alias(n.second, types);
        }
      }
    };
    process_entries(tree_, proc_index);

    // process basic types and callbacks
    auto h_enums = "_enums.hpp";
    auto h_flags = "_flags.hpp";
    auto h_constants = "_constants.hpp";
    auto h_callbacks = "_callbacks.hpp";
    auto h_callbacks_impl = "_callbacks_impl.hpp";
    auto h_functions = "_functions.hpp";
    auto h_functions_impl = "_functions_impl.hpp";
    auto h_call_args = "_callargs.hpp";

    File enums(ns, h_enums, false);
    File flags(ns, h_flags, false);
    File constants(ns, h_constants);
    File callbacks(ns, h_callbacks);
    File callbacks_impl(ns, h_callbacks_impl);
    File functions(ns, h_functions);
    File functions_impl(ns, h_functions_impl);

    // setup CallArgs handling
    // the definition of a CallArgs requires complete types for fields
    // so it will have to be among the last header file, collected as we go
    // actual file handled below
    std::ostringstream call_args;

    entry_processor proc_pass_1 = [&](const pt::ptree::value_type &n) {
      auto &el = n.first;
      if (el == EL_ENUM && visit_ok(n)) {
        process_element_enum(n, enums, nullptr);
      } else if (el == EL_FLAGS && visit_ok(n)) {
        process_element_enum(n, flags, nullptr);
      } else if (el == EL_CONST && visit_ok(n)) {
        process_element_const(n.second, constants);
      }
    };
    process_entries(tree_, proc_pass_1);

    // check class types we can handle and will provide a definition for
    entry_processor proc_pass_class = [&](const pt::ptree::value_type &n) {
      auto &el = n.first;
      if ((el == EL_RECORD || el == EL_OBJECT || el == EL_INTERFACE) &&
          visit_ok(n)) {
        process_element_record(n, true, &call_args);
      }
    };
    process_entries(tree_, proc_pass_class);

    // so the known classes will be declared/defined
    // check what callback typedefs that allows for
    // check class types we can handle and will provide a definition for
    DepsSet cb_deps;
    std::ostringstream cb_decl;
    entry_processor proc_pass_callbacks = [&](const pt::ptree::value_type &n) {
      auto &el = n.first;
      if (el == EL_CALLBACK && visit_ok(n)) {
        std::ostringstream null;
        process_element_function(n, cb_decl, callbacks_impl, "", "", cb_deps);
      }
    };
    process_entries(tree_, proc_pass_callbacks);
    // write callbacks
    // need to declare deps first
    callbacks << make_dep_declare(cb_deps);
    callbacks << std::endl;
    callbacks << cb_decl.str();

    // now we know all supported types and supported callbacks
    // pass over class types again and fill in
    std::set<std::pair<std::string, std::string>> includes;
    DepsSet functions_deps;
    std::ostringstream functions_decl;
    entry_processor proc_pass_2 = [&](const pt::ptree::value_type &n) {
      auto &el = n.first;
      if ((el == EL_RECORD || el == EL_OBJECT || el == EL_INTERFACE) &&
          visit_ok(n)) {
        auto &&res = process_element_record(n, false, &call_args);
        includes.insert({std::get<0>(res), std::get<1>(res)});
      } else if (el == EL_FUNCTION && visit_ok(n)) {
        // all types known now, so include CallArgs directly
        process_element_function(n, functions_decl, functions_impl, "", "",
            functions_deps, &call_args);
      } else if ((el == EL_ENUM || el == EL_FLAGS) && visit_ok(n)) {
        process_element_enum(n, functions, &functions_impl);
      }
    };
    process_entries(tree_, proc_pass_2);

    // write functions
    // need to declare CallArgs deps first
    // only those, as the others are already all declared by this stage
    if (auto fd = make_dep_declare(functions_deps, true); fd.size())
      functions << fd << std::endl;
    functions << functions_decl.str();

    // generate callargs
    if (call_args.tellp()) {
      File ca(ns, h_call_args);
      NamespaceGuard nsg_decl(ca);
      nsg_decl.push(GI_NS_ARGS, false);
      ca << call_args.str();
    } else {
      h_call_args = nullptr;
    }

    auto add_stub_include = [this](const std::string &suffix) {
      auto fpath = (fs::path(tolower(ns)) / (tolower(ns) + suffix)).string();
      return make_conditional_include(fpath, false);
    };

    auto add_stub_define = [this](bool impl, bool begin) {
      auto nsu = toupper(ns);
      auto infix = impl ? "IMPL_" : "";
      auto suffix = begin ? "BEGIN" : "END";
      auto macro = fmt::format("GI_INCLUDE_{}{}_{}", infix, nsu, suffix);
      // avoid deprecated warning floods
      auto guard = begin ? GI_DISABLE_DEPRECATED_WARN_BEGIN
                         : GI_DISABLE_DEPRECATED_WARN_END;
      // also allow for custom/override tweak
      return fmt::format("{}\n\n#ifdef {}\n{}\n#endif", guard, macro, macro);
    };

    auto h_ns = tolower(ns) + ".hpp";
    auto h_ns_inc = tolower(ns) + "_inc.hpp";
    auto h_ns_impl = tolower(ns) + "_impl.hpp";

    // generate overall includes
    File nsh_inc(ns, h_ns_inc, false);
    // enable dl load coding
    if (h_libs && *h_libs) {
      nsh_inc << "#ifndef GI_DL" << std::endl;
      nsh_inc << "#define GI_DL 1" << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    if (ctx.options.expected) {
      nsh_inc << "#ifndef GI_EXPECTED" << std::endl;
      nsh_inc << "#define GI_EXPECTED 1" << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    if (ctx.options.const_method) {
      nsh_inc << "#ifndef GI_CONST_METHOD" << std::endl;
      nsh_inc << "#define GI_CONST_METHOD 1" << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    if (ctx.options.classimpl) {
      nsh_inc << "#ifndef GI_CLASS_IMPL" << std::endl;
      nsh_inc << "#define GI_CLASS_IMPL 1" << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    if (ctx.options.call_args >= 0) {
      nsh_inc << "#ifndef GI_CALL_ARGS" << std::endl;
      nsh_inc << "#define GI_CALL_ARGS " << ctx.options.call_args << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    if (ctx.options.basic_collection) {
      nsh_inc << "#ifndef GI_BASIC_COLLECTION" << std::endl;
      nsh_inc << "#define GI_BASIC_COLLECTION 1" << std::endl;
      nsh_inc << "#endif" << std::endl;
    }
    nsh_inc << std::endl;
    nsh_inc << make_include("gi/gi_inc.hpp", false) << std::endl;
    nsh_inc << std::endl;
    // preserve some behaviour for standard non-module code
    nsh_inc << "#ifndef GI_MODULE_IN_INTERFACE" << std::endl;
    nsh_inc << make_include("gi/gi.hpp", false) << std::endl;
    nsh_inc << "#endif" << std::endl;
    nsh_inc << std::endl;
    // include gi deps; inc version
    for (auto &&d : dep_headers) {
      auto hd = boost::algorithm::replace_last_copy(d, ".hpp", "_inc.hpp");
      nsh_inc << make_include(hd, false) << std::endl;
    }
    nsh_inc << std::endl;
    // package includes
    // in some cases, these are totally missing
    // so the headers will have to be supplied by extra header override
    // repo supplied
    nsh_inc << add_stub_include("_setup_pre_def.hpp") << std::endl;
    // user supplied
    nsh_inc << add_stub_include("_setup_pre.hpp") << std::endl;
    // include the above before the includes
    // (so as to allow tweaking some defines in the overrides)
    auto node = root_.get_child(EL_REPOSITORY);
    auto p = node.equal_range(EL_CINCLUDE);
    for (auto &q = p.first; q != p.second; ++q) {
      // in some buggy cases, additional headers may have a full path
      // let's only keep the last part
      auto name = get_name(q->second);
      if (name.size() && name[0] == '/') {
        auto pos = name.rfind('/');
        pos = (pos != name.npos && pos) ? name.rfind('/', pos - 1) : pos;
        if (pos != name.npos)
          name = name.substr(pos + 1);
      }
      nsh_inc << make_include(name, false) << std::endl;
    }
    // we may also need to tweak or fix things after usual includes
    // repo supplied
    nsh_inc << add_stub_include("_setup_post_def.hpp") << std::endl;
    // user supplied
    nsh_inc << add_stub_include("_setup_post.hpp") << std::endl;
    nsh_inc << std::endl;

    File nsh(ns, h_ns, false);
    // include above part
    nsh << make_include(h_ns_inc, true) << std::endl;
    nsh << std::endl;
    // gi deps; full version
    nsh << "#ifndef GI_MODULE_NO_REC_INC" << std::endl;
    for (auto &&d : dep_headers)
      nsh << make_include(d, false) << std::endl;
    nsh << "#endif" << std::endl;
    // allow to tweak things (e.g. template specialization)
    // prior to generated code declaration/definition
    // repo supplied
    nsh << add_stub_include("_extra_pre_def.hpp") << std::endl;
    // user supplied
    nsh << add_stub_include("_extra_pre.hpp") << std::endl;
    nsh << std::endl;

    // guard begin
    nsh << add_stub_define(false, true) << std::endl;
    nsh << std::endl;

    // various basic declaration parts
    for (auto &h : {h_types, h_enums, h_flags, h_constants, h_callbacks})
      nsh << make_include(h, true) << std::endl;
    nsh << std::endl;

    // declarations
    for (auto &h : includes)
      nsh << make_include(h.first, true) << std::endl;
    nsh << std::endl;
    // global functions when we have seen all else
    nsh << make_include(h_functions, true) << std::endl;
    // all collected CallArgs
    if (h_call_args)
      nsh << make_include(h_call_args, true) << std::endl;
    nsh << std::endl;
    // allow for override/supplements
    // repo supplied
    nsh << add_stub_include("_extra_def.hpp") << std::endl;
    // user supplied
    nsh << add_stub_include("_extra.hpp") << std::endl;
    // guard end
    nsh << add_stub_define(false, false) << std::endl;
    nsh << std::endl;
    // include implementation header in inline case
    nsh << "#if defined(GI_INLINE) || defined(GI_INCLUDE_IMPL)" << std::endl;
    nsh << make_include(h_ns_impl, true) << std::endl;
    nsh << "#endif" << std::endl;
    nsh << std::endl;

    File nsh_impl(ns, h_ns_impl, false);
    // include declaration
    nsh_impl << make_include(h_ns, true) << std::endl;
    nsh_impl << std::endl;
    // guard begin
    nsh_impl << add_stub_define(true, true) << std::endl;
    nsh_impl << std::endl;
    // lib helper for symbol load
    nsh_impl << make_include(h_libs, true) << std::endl;
    nsh_impl << std::endl;
    // implementations
    for (auto &h : includes)
      nsh_impl << make_include(h.second, true) << std::endl;
    nsh_impl << std::endl;
    nsh_impl << make_include(h_callbacks_impl, true) << std::endl;
    nsh_impl << make_include(h_functions_impl, true) << std::endl;
    nsh_impl << std::endl;
    // likewise for override/supplement implementation
    // repo supplied
    nsh_impl << add_stub_include("_extra_def_impl.hpp") << std::endl;
    // user supplied
    nsh_impl << add_stub_include("_extra_impl.hpp") << std::endl;
    nsh_impl << std::endl;
    // guard end
    nsh_impl << add_stub_define(false, true) << std::endl;
    nsh_impl << std::endl;

    // a convenience cpp for non-inline
    auto cpp_ns = tolower(ns) + ".cpp";
    File cpp(ns, cpp_ns, false, false);
    cpp << make_include(h_ns_impl, true) << std::endl;

    // NOTE it is not possible to provide (core) gi as a module,
    // as it also provides a set of macros, and contains some forward
    // declarations (ParamFlags, SignalFlags) which are only fully defined
    // by generated code
    // however, symbol/definitions are attached to module, so a later one
    // can not define another one forward declared by another module
    // in essence; modules break forward declarations
    // so, 2 variants are created;
    // + a separate one per namespace, but the "lowest" one is gobject,
    //   which then also includes glib and gi
    // + a recursive one, a module that combines all dependency ns,
    //   so a large object and precompiled result
    auto nsl = tolower(ns);
    std::string glib_ns = "glib";
    std::string gobject_ns = "gobject";
    if (nsl != glib_ns) {
      // lowest gobject is actually recursive
      std::string modprefix{"gi.repo."};
      bool is_gobject = nsl == gobject_ns;
      if (!is_gobject) {
        // module form; separate
        auto cppm_ns = cpp_ns + "m";
        File cppm(ns, cppm_ns, false, false);
        auto module_templ = R"|(
module;
#define GI_INLINE 1
#define GI_MODULE_IN_INTERFACE 1
{0}
export module {1};
{2}
GI_MODULE_BEGIN
#define GI_MODULE_NO_REC_INC 1
export {{
{3}
}}
GI_MODULE_END
)|";
        auto modname = modprefix + nsl;
        std::ostringstream moddeps;
        bool got_gobject = false;
        for (auto &&d : dep_headers) {
          std::string path = fs::path(d).filename().string();
          boost::algorithm::erase_all(path, ".hpp");
          // sigh, glib/gobject collapsed along with core
          // replace references in non-duplicate way
          if (path == glib_ns)
            path = gobject_ns;
          if (path == gobject_ns) {
            if (got_gobject) {
              continue;
            } else {
              got_gobject = true;
            }
          }
          moddeps << "import " << modprefix << path << ';' << std::endl;
        }
        cppm << fmt::format(module_templ, make_include(h_ns_inc, true), modname,
            moddeps.str(), make_include(h_ns, true));
      }

      { // module form; recursive
        auto cppm_ns = (is_gobject ? cpp_ns : nsl + "_rec.cpp") + "m";
        File cppm(ns, cppm_ns, false, false);
        auto module_templ = R"|(
module;
#define GI_INLINE 1
#define GI_MODULE_IN_INTERFACE 1
{0}
export module {1};
GI_MODULE_BEGIN
#include "gi/gi.hpp"
export {{
{2}
}}
GI_MODULE_END
)|";
        auto modname = modprefix + nsl;
        if (!is_gobject)
          modname += ".rec";
        cppm << fmt::format(module_templ, make_include(h_ns_inc, true), modname,
            make_include(h_ns, true));
      }
    }

    // optional build tool convenience
    // generate refering files in rootdir
    if (ctx.options.output_top) {
      File top_cpp(ns, cpp_ns, false, false, false);
      top_cpp << make_include(nsh_impl.get_rel_path(), true) << std::endl;

      File top_hpp(ns, h_ns, false, false, false);
      top_hpp << make_include(nsh.get_rel_path(), true) << std::endl;
    }

    return nsh.get_rel_path();
  }
};

} // namespace

std::shared_ptr<NamespaceGenerator>
NamespaceGenerator::new_(GeneratorContext &ctx, const std::string &filename)
{
  return std::make_shared<NamespaceGeneratorImpl>(ctx, filename);
}
