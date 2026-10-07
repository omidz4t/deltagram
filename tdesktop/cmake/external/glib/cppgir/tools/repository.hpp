#ifndef REPOSITORY_HPP
#define REPOSITORY_HPP

#include "common.hpp"

#include <map>
#include <memory>
#include <string>

#include <boost/property_tree/ptree.hpp>
namespace pt = boost::property_tree;

// ptree helpers
template<typename T = std::string, typename... Args>
static T
get_attribute(const pt::ptree &node, const std::string &attr, Args... args)
{
  static auto prefix = PT_ATTR + '.';
  return node.get<T>(prefix + attr, args...);
}

inline std::string
get_name(const pt::ptree &node)
{
  return get_attribute(node, AT_NAME);
}

inline std::string
get_name(const pt::ptree &node, std::nothrow_t)
{
  return get_attribute(node, AT_NAME, "");
}

enum TYPE_TRAITS {
  // basic/fundamental glib defined type
  TYPE_BASIC = 1 << 0,
  // type passed by value (integral, enum, etc)
  TYPE_VALUE = 1 << 1,
  // enum, bitfield type
  TYPE_ENUM = 1 << 2,
  // class type (string, object, record)
  TYPE_CLASS = 1 << 3,
  // gobject
  TYPE_OBJECT = 1 << 4,
  // boxed
  TYPE_BOXED = 1 << 5,
  // callback
  TYPE_CALLBACK = 1 << 6,
  // containers
  TYPE_ARRAY = 1 << 7,
  TYPE_LIST = 1 << 8,
  TYPE_MAP = 1 << 9,
  TYPE_CONTAINER = TYPE_ARRAY | TYPE_LIST | TYPE_MAP,
  TYPE_VARARGS = 1 << 10,
  // predefined
  TYPE_PREDEFINED = 1 << 11,
  // typedef (no forward class declare)
  TYPE_TYPEDEF = 1 << 12
};

// some info on (argument) type
struct TypeInfo
{
  TypeInfo() = default;
  TypeInfo(const std::string &_gir, const std::string &_cpp,
      const std::string &_c, const std::string &_argtype, int _flags)
      : girname(_gir), cpptype(_cpp), dtype(_c), argtype(_argtype),
        flags(_flags), pdepth(flags & (TYPE_CLASS | TYPE_CONTAINER) ? 1 : 0)
  {}
  // always qualified (if not predefined glib type)
  std::string girname;
  // always qualified (see below) as it might be used in class context
  // (so to avoid lookup conflicts with parent classes in other ns,
  // e.g. injected-class-name)
  std::string cpptype;
  // c:type taken from class/record definition
  std::string dtype;
  // c type as used in argument (no cv qualification)
  std::string argtype;
  // combination of flags above
  int flags = 0;
  // number of pointer indirections
  int pdepth = 0;
};

class Repository
{
  Repository() = default;
  friend class RepositoryPriv;

  Repository(const Repository &other) = delete;
  Repository &operator=(const Repository &other) = delete;

public:
  typedef std::map<std::string, std::string> subst_c_types;
  typedef std::string key_type;
  struct mapped_type
  {
    typedef pt::ptree::value_type tree_type;
    std::unique_ptr<tree_type> tree;
    std::unique_ptr<TypeInfo> info;
  };

  static std::shared_ptr<Repository> new_(subst_c_types m);

  // set namespace used for unqualified girname
  void set_ns(const std::string _ns);

  // qualify girname, optionally wrt relative base
  std::string qualify(
      const std::string &girname, const std::string &base = "") const;

  void add(const key_type &girname, const mapped_type::tree_type &n);

  void discard(const key_type &girname);

  const mapped_type::tree_type &tree(const std::string &girname) const;

  const mapped_type *lookup(const std::string &girname) const;

  // check for duplicate definition for ctype
  // if ctype already claimed, returns non-empty claiming cpptype
  std::string check_odr(const std::string &cpptype, const std::string &ctype);
};

#endif // REPOSITORY_HPP
