#ifndef GENBASE_HPP
#define GENBASE_HPP

#include "common.hpp"
#include "genutils.hpp"
#include "repository.hpp"

#include <set>

struct GeneratorOptions
{
  // dir in which to generate
  std::string rootdir;
  // generate implementation classes
  bool classimpl;
  // generate fallback methods (for class implementation)
  bool classfull;
  // use dlopen/dlsym for generated call
  bool dl;
  // use expected<> return iso exception throwing
  bool expected;
  // generate const methods
  bool const_method;
  // generate top-level helpers in rootdir
  bool output_top;
  // only generate/overwrite output if changed
  bool only_changed;
  // min number of non-required function arguments
  // that triggers generation a CallArgs variant
  int call_args;
  // also generate collection signature for input collection of basic type
  bool basic_collection;
};

struct GeneratorContext
{
  GeneratorOptions &options;
  Repository &repo;
  const Matcher &match_ignore;
  const Matcher &match_sup;
  // generated during processing
  std::set<std::string> &suppressions;
};

class GeneratorBase
{
protected:
  GeneratorContext &ctx;
  std::string ns;
  const std::string indent = "  ";

public:
  GeneratorBase(GeneratorContext &_ctx, const std::string _ns);

  struct ArgTypeInfo : public TypeInfo
  {
    // c:type as parsed from type element
    // (no const info for array or filename arg)
    // (empty for void, and possibly property or signal)
    std::string ctype;

    // return CppType or reference CppType (if non-owning boxed transfer)
    std::string cppreftype(const std::string transfer) const
    {
      if ((flags & TYPE_BOXED) && transfer != TRANSFER_FULL)
        return cpptype + GI_SUFFIX_REF;
      // string case
      if ((flags & TYPE_CLASS) && (flags & TYPE_BASIC) &&
          transfer != TRANSFER_FULL)
        return cpptype + "_v";
      return cpptype;
    }
  };

  // the above along with info on contained element
  // (as provided typically within <type> element in parameter or so)
  // * first only relevant for array, lists and maps
  // * second only for maps
  // * first and second do not have ctype info
  // * in case of array, no cpptype info on primary
  // * in case of (GS)List etc, primary specifies the particular type
  struct ArgInfo : public ArgTypeInfo
  {
    // container element types
    ArgTypeInfo first, second;
    // array
    int length = -1;
    bool zeroterminated = false;
    int fixedsize = 0;
  };

  std::string qualify(const std::string &cpptype, int flags) const;

  void parse_typeinfo(const std::string &girname, TypeInfo &result) const;

  // if routput != nullptr, also place result in output
  // (possibly a partial one if exception is thrown)
  ArgInfo parse_arginfo(
      const pt::ptree &node, ArgInfo *routput = nullptr) const;

  static std::string make_ctype(
      const ArgInfo &info, const std::string &direction, bool callerallocates);

  // set of (ns, dep), where dep = [struct|class ]type
  using DepsSet = std::set<std::pair<std::string, std::string>>;
  void track_dependency(DepsSet &deps, const ArgInfo &info) const;

  static std::string make_wrap_format(const ArgInfo &info,
      const std::string &transfer, const std::string &outtype = {});

  static std::string get_transfer_parameter(
      const std::string &transfer, bool _type = false);

  bool check_suppression(const std::string &ns, const std::string &kind,
      const std::string &name) const;
};
#endif // GENBASE_HPP
