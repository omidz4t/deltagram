#ifndef MACRO_H_INCLUDED
#define MACRO_H_INCLUDED

#include "atom/atom.h"
#include "common.h"

#include <map>
#include <set>
#include <string>

namespace tex {

/**
 * Upper bound on the number of arguments a macro may declare, matching the
 * limit LaTeX itself imposes on \newcommand (the widest built-in here takes
 * 6). TeXParser::getOptsArgs sizes its argument vector from this count and
 * indexes that vector relative to it, so a count that is negative or wildly
 * large turns into out-of-bounds accesses and unbounded allocations. The
 * count reaches MacroInfo straight from the formula text, so it has to be
 * rejected before it gets there.
 */
constexpr int kMaxMacroArgs = 9;

inline bool isValidMacroArgc(int argc) {
  return argc >= 0 && argc <= kMaxMacroArgs;
}

class TeXParser;

class Macro {
public:
  virtual void execute(TeXParser& tp, std::vector<std::wstring>& args) = 0;

  virtual ~Macro() = default;
};

class NewCommandMacro : public Macro {
protected:
  static std::map<std::wstring, std::wstring> _codes;
  static std::map<std::wstring, std::wstring> _replacements;
  static Macro* _instance;

  // Snapshot of the built-in macro state, taken right after _init_ via
  // _captureBuiltins(). User formulas may be untrusted, so _reset() rolls
  // _codes/_replacements/_commands back to this snapshot before each render,
  // and checkNew/checkRenew refuse to redefine any built-in -- otherwise a
  // single message could redefine \frac (or any command) for every later
  // formula in the session.
  static std::map<std::wstring, std::wstring> _baselineCodes;
  static std::map<std::wstring, std::wstring> _baselineReplacements;
  static std::set<std::wstring> _builtinCommands;

  static void checkNew(const std::wstring& name);

  static void checkRenew(const std::wstring& name);

public:
  /**
   * If notify a fatal error when defining a new command but it has been
   * defined already or redefine a command but it has not been defined,
   * default is true.
   */
  static bool _errIfConflict;

  void execute(TeXParser& tp, std::vector<std::wstring>& args) override;

  static void addNewCommand(
    const std::wstring& name,
    const std::wstring& code,
    int argc
  );

  static void addNewCommand(
    const std::wstring& name,
    const std::wstring& code,
    int argc,
    const std::wstring& def
  );

  static void addRenewCommand(
    const std::wstring& name,
    const std::wstring& code,
    int argc
  );

  static void addRenewCommand(
    const std::wstring& name,
    const std::wstring& code,
    int argc,
    const std::wstring& def
  );

  static bool isMacro(const std::wstring& name);

  static void _init_();

  // Record the current macro state as the built-in baseline. Call once after
  // _init_ has registered all predefined commands/environments.
  static void _captureBuiltins();

  // Roll back to the built-in baseline, dropping anything a formula defined.
  static void _reset();

  static void _free_();

  ~NewCommandMacro() override = default;
};

class NewEnvironmentMacro : public NewCommandMacro {
public:
  static void addNewEnvironment(
    const std::wstring& name,
    const std::wstring& begDef,
    const std::wstring& endDef,
    int argc
  );

  static void addRenewEnvironment(
    const std::wstring& name,
    const std::wstring& begDef,
    const std::wstring& endDef,
    int argc
  );
};

class MacroInfo {
public:
  static std::map<std::wstring, MacroInfo*> _commands;

  /** Add a macro, replace it if the macro is exists. */
  static void add(const std::wstring& name, MacroInfo* mac);

  /** Get the macro info from given name, return nullptr if not found. */
  static MacroInfo* get(const std::wstring& name);

  // Number of arguments
  const int _argc;
  // Options' position, can be  0, 1 and 2
  // 0 represents this macro has no options
  // 1 represents the options appear after the command name, e.g.:
  //      \sqrt[3]{2}
  // 2 represents the options appear after the first argument, e.g.:
  //      \scalebox{0.5}[2]{\LaTeX}
  const int _posOpts;

  MacroInfo() : _argc(0), _posOpts(0) {}

  MacroInfo(int argc, int posOpts) : _argc(argc), _posOpts(posOpts) {}

  explicit MacroInfo(int argc) : _argc(argc), _posOpts(0) {}

  virtual sptr<Atom> invoke(
    TeXParser& tp,
    std::vector<std::wstring>& args) {
    return nullptr;
  }

  virtual ~MacroInfo() = default;

  static void _free_();
};

class InflationMacroInfo : public MacroInfo {
private:
  // The actual macro to execute
  Macro* const _macro;

public:
  InflationMacroInfo(Macro* macro, int argc)
    : _macro(macro), MacroInfo(argc) {}

  InflationMacroInfo(Macro* macro, int argc, int posOpts)
    : _macro(macro), MacroInfo(argc, posOpts) {}

  sptr<Atom> invoke(
    TeXParser& tp,
    std::vector<std::wstring>& args
  ) override {
    _macro->execute(tp, args);
    return nullptr;
  }
};

typedef sptr<Atom> (* MacroDelegate)(
  TeXParser& tp,
  std::vector<std::wstring>& args
);

class PreDefMacro : public MacroInfo {
private:
  MacroDelegate _delegate;

public:
  PreDefMacro() = delete;

  PreDefMacro(int argc, int posOpts, MacroDelegate delegate)
    : MacroInfo(argc, posOpts), _delegate(delegate) {}

  PreDefMacro(int argc, MacroDelegate delegate)
    : MacroInfo(argc), _delegate(delegate) {}

  sptr<Atom> invoke(
    TeXParser& tp,
    std::vector<std::wstring>& args
  ) override;
};

}  // namespace tex

#endif  // MACRO_H_INCLUDED
