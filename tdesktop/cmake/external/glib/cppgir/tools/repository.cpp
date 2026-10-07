#include "repository.hpp"

#include "genutils.hpp"

#include <regex>
#include <set>
#include <unordered_map>

static std::set<std::string> basic_types{"gchar", "guchar", "gshort", "gushort",
    "gint", "guint", "glong", "gulong", "gssize", "gsize", "gintptr",
    "guintptr", "gpointer", "gconstpointer", "gboolean", "gint8", "gint16",
    "guint8", "guint16", "gint32", "guint32", "gint64", "guint64", "gfloat",
    "gdouble", "GType", "utf8", "filename", "gi::cstring", "gunichar",
    "dev_t", "gid_t", "pid_t", "socklen_t", "uid_t"};

class RepositoryPriv : public Repository
{
public:
  // holds info collected from GIRs indexed by entry's (qualified) name
  // (which should be unique within a namespace)
  std::unordered_map<key_type, mapped_type> index;
  // active ns
  std::string ns;
  // ODR check
  mutable std::unordered_map<std::string, std::string> type_index;
  // substitutes for missing c:type
  subst_c_types c_types;

  RepositoryPriv(subst_c_types m) : c_types(std::move(m))
  {
    // make basic types known
    for (auto &&girname : basic_types) {
      auto cpptype = girname;
      auto ctype = girname;
      int flags = 0;

      flags |= TYPE_BASIC;
      if (girname == "utf8" || girname == "filename" ||
          girname == "gi::cstring") {
        flags |= TYPE_CLASS;
        ctype = "char*";
        cpptype = "gi::cstring";
      } else {
        flags |= TYPE_VALUE;
        if (girname == "gboolean")
          cpptype = "bool";
      }
      auto argtype =
          (girname.find("pointer") != girname.npos) ? "void*" : ctype;
      index.emplace(
          girname, mapped_type{nullptr, std::make_unique<TypeInfo>(girname,
                                            cpptype, ctype, argtype, flags)});
    }
    // void case
    index.emplace(GIR_VOID,
        mapped_type{nullptr, std::make_unique<TypeInfo>(GIR_VOID, CPP_VOID,
                                 EMPTY, CPP_VOID, TYPE_BASIC)});

    // other special and pre-defined cases
    std::vector<std::tuple<std::string, std::string, std::string, int>> pre{
        {std::make_tuple("GLib.List", "GList", "GList", TYPE_LIST)},
        {std::make_tuple("GLib.SList", "GSList", "GSList", TYPE_LIST)},
        {std::make_tuple(
            "GLib.HashTable", "GHashTable", "GHashTable", TYPE_MAP)},
        {std::make_tuple("GObject.Value", "GObject::Value", "GValue",
            TYPE_CLASS | TYPE_BOXED)},
        {std::make_tuple(
            "GLib.Error", "GLib::Error", "GError", TYPE_CLASS | TYPE_BOXED)},
        // avoid namespace mishap
        {std::make_tuple("GObject.Object", "GObject::Object", "GObject",
            TYPE_CLASS | TYPE_OBJECT)},
        // pretend is like object
        {std::make_tuple("GObject.ParamSpec", "GObject::ParamSpec",
            "GParamSpec", TYPE_CLASS)}};
    for (auto &&e : pre) {
      // qualify argtype to avoid name conflicts
      auto ti = std::make_unique<TypeInfo>(std::get<0>(e), std::get<1>(e),
          std::get<2>(e), GI_SCOPE + std::get<2>(e) + "*",
          std::get<3>(e) | TYPE_PREDEFINED);
      auto girname = ti->girname;
      index.emplace(girname, mapped_type{nullptr, std::move(ti)});
    }
    auto ti =
        std::make_unique<TypeInfo>("GLib.DestroyNotify", "GLib::DestroyNotify",
            GDESTROYNOTIFY, GDESTROYNOTIFY, TYPE_CALLBACK | TYPE_PREDEFINED);
    auto girname = ti->girname;
    index.emplace(girname, mapped_type{nullptr, std::move(ti)});
  }
};

// C++ on the outside, C trick on the inside
static const RepositoryPriv &
get_self(const Repository *t)
{
  return *static_cast<const RepositoryPriv *>(t);
}
static RepositoryPriv &
get_self(Repository *t)
{
  return *static_cast<RepositoryPriv *>(t);
}

// set namespace used for unqualified girname
void
Repository::set_ns(const std::string _ns)
{
  auto &self = get_self(this);
  self.ns = _ns;
}

// qualify girname, optionally wrt relative base
std::string
Repository::qualify(const std::string &girname, const std::string &base) const
{
  auto &&self = get_self(this);
  if (girname.find('.') == girname.npos) {
    auto bns = self.ns;
    if (base.size()) {
      auto pos = base.find('.');
      if (pos == base.npos)
        return girname;
      bns = base.substr(0, pos);
    }
    return bns + '.' + girname;
  }
  return girname;
}

void
Repository::add(const key_type &girname, const mapped_type::tree_type &n)
{
  auto &&self = get_self(this);
  auto qualified = qualify(girname);
  auto it = self.index.find(qualified);
  bool registered = false;

  if (girname.empty()) {
    // should not make it here
    assert(false);
  } else if (it != self.index.end()) {
    auto &e = it->second;
    // merge in tree data for predefined
    if (e.info && (e.info->flags & TYPE_PREDEFINED)) {
      e.tree = std::make_unique<mapped_type::tree_type>(n);
    } else {
      throw std::runtime_error(
          fmt::format("duplicate name {} [{}]", girname, qualified));
    }
  } else {
    // examine node/type
    // NOTE not every entry/node represents a type (and therefore has !=
    // flags) consider e.g. a constant or function
    int flags = 0;
    auto &el = n.first;
    auto &node = n.second;
    auto ctype = get_attribute(node, AT_CTYPE, "");

    // base identification
    if (el == EL_RECORD) {
      flags |= TYPE_CLASS | TYPE_BOXED;
    } else if (el == EL_OBJECT || el == EL_INTERFACE) {
      flags |= TYPE_CLASS | TYPE_OBJECT;
    } else if (el == EL_ENUM || el == EL_FLAGS) {
      flags |= TYPE_ENUM | TYPE_VALUE;
    } else if (el == EL_ALIAS) {
      // adopt flags of typedef'ed one
      // need not be a primitive one (e.g. GtkAllocation = GdkRectangle)
      auto btype = node.get(EL_TYPE + '.' + PT_ATTR + '.' + AT_NAME, "");
      auto ti = lookup(btype);
      flags = ti && ti->info ? ti->info->flags : 0;
      // also consider as sort-of predefined (at least if we know about
      // it)
      if (flags)
        flags |= TYPE_TYPEDEF;
    } else if (el == EL_CALLBACK) {
      flags |= TYPE_CALLBACK;
    }
    if (flags) {
      // check if registered
      auto gettype = get_attribute(n.second, AT_GLIB_GET_TYPE, "");
      if (gettype.size())
        registered = true;
    }

    bool keep_node = false;
    bool keep_info = flags != 0;
    if (flags & TYPE_CLASS) {
      keep_node = true;
      // additional checks
      auto class_struct = get_attribute(node, AT_GLIB_IS_TYPE_STRUCT_FOR, "");
      if (class_struct.size()) {
        // is not a valid type, but keep some info around for later
        // lookup
        flags = 0;
        keep_info = true;
      } else if (flags & TYPE_BOXED) {
        keep_node = false;
      }
      // e.g. GParamSpec (several), variant
      auto fundamental = get_attribute<int>(node, AT_GLIB_FUNDAMENTAL, 0);
      // GVariant is marked this way
      auto gtype = get_attribute(node, AT_GLIB_GET_TYPE, "");
      if (fundamental || gtype == "intern") {
        flags = -1;
      }
    }

    {
      // filter some more
      auto disguised = get_attribute<int>(node, AT_DISGUISED, 0);
      // also never mind if it is designed opaque/disguised
      // lots of private stuff, but also possibly type structs
      // (some at least, in case of opaque struct)
      if (disguised)
        flags = -1;
      // cairo is notable foreign example
      // structs are typically opaque, with custom _create, _copy,
      // _destroy
      // TODO for now filter all, perhaps not so later
      // (and then only filter by ignore and allow custom declaration of
      // above functions in the boxed system)
      auto foreign = get_attribute<int>(node, AT_FOREIGN, 0);
      if (foreign && !registered)
        flags = -1;
    }

    // special fundamental case
    // partly prepared, mostly generated
    if (qualified == GIR_GVARIANT) {
      keep_node = false;
      keep_info = true;
      flags = TYPE_CLASS;
    }

    // always check
    auto movedto = get_attribute(node, AT_MOVED_TO, "");
    if (!movedto.empty())
      flags = -1;

    if (flags == -1) {
      logger(Log::DEBUG, "ignoring GIR {} {}", el, qualified);
    } else {
      logger(Log::LOG, "registering GIR {} {} {}", el, qualified, flags);

      // convert GIR qualification to namespace qualification
      static const std::regex re_qualify("\\.", std::regex::optimize);
      // enum cpptype is unreserve'd in type definition
      auto cpptype = std::regex_replace(
          (flags & TYPE_ENUM) ? qualify(unreserve(girname)) : qualified,
          re_qualify, "::");
      assert(!(flags & TYPE_CLASS) || is_qualified(cpptype));

      // in rare cases c:type is missing for a record/class
      // (e.g. GtkSnapshot = alias of Gdk type)
      // use an override substitute type
      if ((flags & TYPE_CLASS) && ctype.empty()) {
        auto sit = self.c_types.find(qualified);
        if (sit != self.c_types.end()) {
          ctype = sit->second;
          logger(Log::INFO, "{} using substitute c:type {}", qualified, ctype);
        }
      }

      // always top-level qualify ctype to avoid ns ambiguity
      if (ctype.size())
        ctype = GI_SCOPE + ctype;
      auto argtype = ctype;
      if ((flags & TYPE_CLASS) && argtype.size())
        argtype += GI_PTR;

      std::unique_ptr<mapped_type::tree_type> xmlinfo;
      // node only needed for class type
      // (only a small part of node is needed later on,
      // but let's simply keep all of it)
      if (keep_node)
        xmlinfo = std::make_unique<mapped_type::tree_type>(n);

      mapped_type entry = {
          std::move(xmlinfo), keep_info ? std::make_unique<TypeInfo>(qualified,
                                              cpptype, ctype, argtype, flags)
                                        : nullptr};
      it = std::get<0>(
          self.index.emplace(std::move(qualified), std::move(entry)));
    }
  }
}

void
Repository::discard(const key_type &girname)
{
  auto &&self = get_self(this);
  auto qualified = qualify(girname);
  logger(Log::LOG, "discarding girname {}", qualified);
  if (!self.index.erase(qualified))
    logger(Log::WARNING, "discarded unknown girname {}", qualified);
}

const Repository::mapped_type::tree_type &
Repository::tree(const std::string &girname) const
{
  auto &&self = get_self(this);
  auto &index = self.index;
  // only used by class types
  auto it = index.find(qualify(girname));
  if (it == index.end() || !it->second.tree)
    throw std::runtime_error("no node info for " + girname);
  return *it->second.tree;
}

const Repository::mapped_type *
Repository::lookup(const std::string &girname) const
{
  auto &&self = get_self(this);
  auto &index = self.index;
  // also consider non-qualified for basic types
  auto it = index.find(girname);
  if (it == index.end())
    it = index.find(qualify(girname));
  if (it != index.end())
    return &it->second;
  return nullptr;
}

std::string
Repository::check_odr(const std::string &cpptype, const std::string &ctype)
{
  if (!ctype.empty()) {
    auto &&self = get_self(this);
    auto ret = self.type_index.insert({ctype, cpptype});
    if (!ret.second && ret.first->second != cpptype) {
      return ret.first->second;
    }
  }
  return {};
}

std::shared_ptr<Repository>
Repository::new_(subst_c_types m)
{
  return std::make_shared<RepositoryPriv>(std::move(m));
}
