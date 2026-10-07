#ifndef FUNCTION_HPP
#define FUNCTION_HPP

#include "genbase.hpp"

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

struct ElementFunction
{
  // type of function; method, etc
  std::string kind;
  // GIR name
  std::string name;
  // otherwise descriptive name (e.g. original C name)
  std::string c_id;
  // expression defining function
  std::string functionexp;
  bool throws{};
  // represents symbol in lib
  // (which can be linked to or dlsym'ed)
  bool lib_symbol{};
  std::string shadows;
};

// parameter data
constexpr static const int INDEX_DEFAULT = -10;
struct FunctionParameter
{
  std::string name;
  GeneratorBase::ArgInfo tinfo{};
  // deduced C type
  std::string ptype;
  bool instance{};
  std::string direction;
  std::string transfer{TRANSFER_NOTHING};
  // index
  int closure{INDEX_DEFAULT}, destroy{INDEX_DEFAULT};
  int callerallocates{};
  std::string scope;
  bool optional{};
  bool nullable{};
};
using Parameter = FunctionParameter;

// info defining function to construct declaration/definition
// (some parts not relevant for signal/callback)
struct FunctionDefinition
{
  // return (and optionally additional outputs)
  struct Output
  {
    // cpp type of output
    std::string type;
    // expression (value of output)
    std::string value;
  };

  struct ArgTrait
  {
    // transfer (original attribute)
    std::string transfer;
    // inout parameter
    bool inout{};
    // applicable/relevant arguments (indexed as usual; see below)
    // usually only 1 (to 1)
    // for callback; function, userdata[, destroy]
    // for sized arrays; data, size
    // (other containers only need 1 and handled as usual)
    std::vector<int> args;
    // custom type trait
    std::string custom{};
  };

  // GIR name (empty if not valid)
  std::string name;
  // statements preceding wrapped call (excluding final ;)
  std::vector<std::string> pre_call;
  //  idem, post call
  std::vector<std::string> post_call;
  // assembled outputs (first one is return value, if any, could be empty)
  std::vector<Output> cpp_outputs;
  // parts that make up the C call (...)
  // indexed by param number (instance = -1)
  std::map<int, std::string> c_call;
  // parts that make up the ( ... ) decl/def
  // (similarly indexed/sorted)
  std::map<int, std::string> cpp_decl;
  // callee; trait info of (callback) parameters
  // (lowest one is for return; always present)
  // (except if fallback virtual method)
  std::map<int, ArgTrait> arg_traits;
  // extra parameters in a callee (= cb) declaration not present in callforward
  // (to deal with cb sized array output0
  std::map<int, std::string> cpp_decl_extra;
  // function (expression) to call
  std::string c_callee;
  // format with single placeholder for call (constructed based on all above)
  std::string ret_format;
  // parts used for callforward generation of callback type
  // (callforward = C++ signature which then calls a C function)
  // (callback = C signature which then calls a C++ function)
  // typedef of C function to call
  std::string cf_ctype;
  // (derived) C signature (without instance parameter if virtual method)
  std::string c_sig;
};

std::string make_arg_traits(
    const std::map<int, FunctionDefinition::ArgTrait> &traits,
    const std::string &c_sig);

FunctionDefinition process_element_function(GeneratorContext &_ctx,
    const std::string _ns, const pt::ptree::value_type &entry,
    std::ostream &out, std::ostream &impl, const std::string &klass,
    const std::string &klasstype, GeneratorBase::DepsSet &deps,
    std::ostream *call_args, bool allow_deprecated);

FunctionDefinition process_element_function(GeneratorContext &_ctx,
    const std::string _ns, const ElementFunction &func,
    const std::vector<Parameter> &params, std::ostream &out, std::ostream &impl,
    const std::string &klass, const std::string &klasstype,
    GeneratorBase::DepsSet &deps);

#endif // FUNCTION_HPP
