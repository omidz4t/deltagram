#include "core/parser.h"

#include "atom/atom.h"
#include "atom/atom_basic.h"
#include "common.h"
#include "core/formula.h"
#include "core/macro.h"
#include "fonts/alphabet.h"
#include "fonts/fonts.h"
#include "graphic/graphic.h"

using namespace std;
using namespace tex;

namespace {
// Guard against stack overflow from pathologically nested input (deep braces,
// scripts, \frac/\sqrt nesting). Recursive parsing takes two routes, and both
// must be counted: brace groups via getArgument(), scripts via getScripts()
// and sub-formulas (\frac, \sqrt, ...) via nested Formula construction all
// re-enter TeXParser::parse(); brace-less command chains ("\sqrt\sqrt x")
// instead recurse through getCommandWithArgs() without touching parse() at
// all. Both sites take a guard against this one counter, so the limit bounds
// their combined depth -- and with it the depth of the atom tree that
// createBox() later recurses over.
//
// The limit has to leave room for the throw, not just for the descent, and
// that is what makes it much smaller than it looks like it could be. Reaching
// a given depth costs far less stack than unwinding from it: measured on
// Windows, a 1MB stack parses 249 nested groups happily but dies inside the
// throw the guard raises at 250, and a 512KB stack dies in the throw at 60.
// The old limit of 250 therefore never actually protected anything -- the
// process aborted, uncatchably, at the exact depth the guard was meant to
// reject, on "{" repeated 250 times or "x" followed by 250 "^{". 48 stays
// clear of the throw budget on a 512KB stack (the smallest a formula is
// likely to be parsed on -- secondary threads on macOS) and is still three
// times deeper than the deepest formula in the render corpus, whose most
// nested cases (\cfrac chains, stacked \sqrt, Maxwell's equations) sit under
// 16. Raising it again means re-measuring the throw, not just the descent.
// thread_local because formulas may be parsed on worker threads concurrently.
constexpr int kMaxParseDepth = 48;
thread_local int gParseDepth = 0;

// A macro or environment expansion splices its replacement back into _latex
// and rewinds the cursor onto it, so a self-referential definition expands
// again and again. Two bounds are needed, because either one alone leaves the
// other side unbounded:
//  - how much text an expansion may produce, or a definition that grows the
//    formula every round (\newcommand{\x}{\x\x}) runs out of memory first;
//  - the accumulated cost of the expansions, since each one re-copies and
//    re-scans the text around it and therefore costs a share of the current
//    formula length. A count limit alone does not bound that product:
//    \newenvironment{a}{\begin{a}...}{} rewrites the same 400 characters
//    forever, staying inside any count while every round rescans the whole
//    formula, and a 450-character message froze the parse for minutes.
// Real formulas are a few hundred characters and expand a handful of times,
// which is a millionth of the work limit.
constexpr int kMaxExpansions = 10000;
constexpr int kMaxExpandedLength = 64 * 1024;
constexpr int64_t kMaxExpansionWork = 32 * 1024 * 1024;
// thread_local because formulas may be parsed on worker threads concurrently.
// Deliberately not reset on throw: parsing swallows ex_parse in several places
// (partial mode, per-argument sub-formulas), so a self-restarting budget would
// not bound anything.
thread_local int64_t gExpansionWork = 0;

// Charged before an expansion runs, not after: the cost being bounded is the
// scanning and copying the expansion is about to do, and once the budget is
// spent every further call must fail before paying it again. Partial mode
// swallows the throw, so an expansion that only failed afterwards kept doing
// the full work on every turn.
void countExpansion(int length) {
  gExpansionWork += length;
  if (gExpansionWork > kMaxExpansionWork) {
    throw ex_parse("Formula expands too much!");
  }
}

struct ParseDepthGuard {
  ParseDepthGuard() {
    if (++gParseDepth > kMaxParseDepth) {
      --gParseDepth;
      throw ex_parse("Formula nesting is too deep!");
    }
  }
  ~ParseDepthGuard() { --gParseDepth; }
};
}  // namespace

namespace tex {
void resetExpansionWork() {
  gExpansionWork = 0;
}
}  // namespace tex

const wchar_t TeXParser::ESCAPE = '\\';
const wchar_t TeXParser::L_GROUP = '{';
const wchar_t TeXParser::R_GROUP = '}';
const wchar_t TeXParser::L_BRACK = '[';
const wchar_t TeXParser::R_BRACK = ']';
const wchar_t TeXParser::DOLLAR = '$';
const wchar_t TeXParser::DQUOTE = '\"';
const wchar_t TeXParser::PERCENT = '%';
const wchar_t TeXParser::SUB_SCRIPT = '_';
const wchar_t TeXParser::SUPER_SCRIPT = '^';
const wchar_t TeXParser::PRIME = '\'';
const wchar_t TeXParser::PRIME_UTF = 0x2019;
const wchar_t TeXParser::BACKPRIME = 0x2035;
const wchar_t TeXParser::DEGRE = 0x00B0;

const map<wchar_t, char> TeXParser::SUP_SCRIPT_MAP = {
  {0x2070, '0'},
  {0x00B9, '1'},
  {0x00B2, '2'},
  {0x00B3, '3'},
  {0x2074, '4'},
  {0x2075, '5'},
  {0x2076, '6'},
  {0x2077, '7'},
  {0x2078, '8'},
  {0x2079, '9'},
  {0x207A, '+'},
  {0x207B, '-'},
  {0x207C, '='},
  {0x207D, '('},
  {0x207E, ')'},
  {0x207F, 'n'},
};
const map<wchar_t, char> TeXParser::SUB_SCRIPT_MAP = {
  {0x2080, '0'},
  {0x2081, '1'},
  {0x2082, '2'},
  {0x2083, '3'},
  {0x2084, '4'},
  {0x2085, '5'},
  {0x2086, '6'},
  {0x2087, '7'},
  {0x2088, '8'},
  {0x2089, '9'},
  {0x208A, '+'},
  {0x208B, '-'},
  {0x208C, '='},
  {0x208D, '('},
  {0x208E, ')'},
};

const set<wstring> TeXParser::_unparsedContents = {
  L"dynamic",
  L"Text",
  L"Textit",
  L"Textbf",
  L"Textitbf",
  L"externalFont",
};

bool TeXParser::_isLoading = false;

void TeXParser::init(
  bool isPartial,
  const wstring& latex,
  Formula* formula,
  bool firstPass
) {
  _pos = _spos = _len = 0;
  _line = _col = 0;
  _group = 0;
  _atIsLetter = 0;
  _expansions = 0;
  _insertion = _arrayMode = _isMathMode = false;
  _isPartial = _hideUnknownChar = true;

  _formula = formula;
  _isMathMode = true;
  _isPartial = isPartial;
  if (!latex.empty()) {
    _latex = latex;
    _len = latex.length();
    _pos = 0;
    if (firstPass) preprocess();
  } else {
    _latex = L"";
    _pos = 0;
    _len = 0;
  }
  _arrayMode = formula->isArrayMode();
}

void TeXParser::reset(const wstring& latex) {
  _latex = latex;
  _len = latex.length();
  _formula->_root = nullptr;
  // Only \left...\right consumes this list, from its own local formula, so
  // a \middle written outside one is parked here and never taken. The shared
  // LaTeX::_formula is reused for every parse, so without clearing it those
  // atoms -- and the whole subtree each one holds -- accumulated for the
  // lifetime of the process.
  _formula->_middle.clear();
  _pos = 0;
  _spos = 0;
  _line = 0;
  _col = 0;
  _group = 0;
  _expansions = 0;
  _insertion = false;
  _atIsLetter = 0;
  _arrayMode = false;
  _isMathMode = true;
  preprocess();
}

sptr<Atom> TeXParser::popLastAtom() const {
  auto a = _formula->_root;
  auto* ra = dynamic_cast<RowAtom*>(a.get());
  if (ra != nullptr) return ra->popLastAtom();
  _formula->_root = nullptr;
  return a;
}

sptr<Atom> TeXParser::popFormulaAtom() const {
  auto a = _formula->_root;
  _formula->_root = nullptr;
  return a;
}

void TeXParser::addAtom(const sptr<Atom>& atom) const {
  _formula->add(atom);
}

void TeXParser::addRow() const {
  if (!_arrayMode) throw ex_parse("Can not add row in none-array mode!");
  // _formula may temporarily point at a plain Formula (e.g. a brace-group
  // argument in getArgument), a C-style downcast would read past the end
  // of the object.
  auto* arr = dynamic_cast<ArrayFormula*>(_formula);
  if (arr == nullptr) throw ex_parse("Can not add row in none-array mode!");
  arr->addRow();
}

wstring TeXParser::getDollarGroup(wchar_t openclose) {
  int spos = _pos;
  wchar_t ch;

  do {
    ch = _latex[_pos++];
    if (ch == ESCAPE) _pos++;
  } while (_pos < _len && ch != openclose);

  if (ch == openclose) return _latex.substr(spos, _pos - spos - 1);
  return _latex.substr(spos, _pos - spos);
}

wstring TeXParser::getGroup(wchar_t open, wchar_t close) {
  if (_pos == _len) return L"";

  int group, spos;
  wchar_t ch = _latex[_pos];

  if (_pos < _len && ch == open) {
    group = 1;
    spos = _pos;
    while (_pos < _len - 1 && group != 0) {
      _pos++;
      ch = _latex[_pos];
      if (ch == open)
        group++;
      else if (ch == close)
        group--;
      else if (ch == ESCAPE && _pos != _len - 1)
        _pos++;
    }

    _pos++;

    if (group != 0) return _latex.substr(spos + 1, _pos - spos - 1);
    return _latex.substr(spos + 1, _pos - spos - 2);
  }
  throw ex_parse("Missing '" + tostring((char) open) + "'!");
}

wstring TeXParser::getGroup(const wstring& open, const wstring& close) {
  int group = 1;
  int ol = open.length();
  int cl = close.length();
  bool lastO = isValidCharInCmd(open[ol - 1]);
  bool lastC = isValidCharInCmd(close[cl - 1]);
  int oc = 0;
  int cc = 0;
  int startC = 0;
  wchar_t prev = L'\0';
  wstring buf;

  while (_pos < _len && group != 0) {
    wchar_t c = _latex[_pos];
    wchar_t c1;

    if (prev != ESCAPE && c == ' ') {
      while (_pos < _len && _latex[_pos++] == ' ') buf.append(1, L' ');
      c = _latex[--_pos];
      if (isValidCharInCmd(prev) && isValidCharInCmd(c)) {
        oc = cc = 0;
      }
    }

    if (c == open[oc]) oc++; else oc = 0;

    if (c == close[cc]) {
      if (cc == 0) startC = _pos;
      cc++;
    } else {
      cc = 0;
    }

    if (_pos + 1 < _len) {
      c1 = _latex[_pos + 1];

      if (oc == ol) {
        if (!lastO || !isValidCharInCmd(c1)) group++;
        oc = 0;
      }

      if (cc == cl) {
        if (!lastC || !isValidCharInCmd(c1)) group--;
        cc = 0;
      }
    } else {
      if (oc == ol) {
        group++;
        oc = 0;
      }
      if (cc == cl) {
        group--;
        cc = 0;
      }
    }

    prev = c;
    buf.append(1, c);
    _pos++;
  }

  if (group != 0) {
    if (_isPartial) return buf;
    throw ex_parse("Parse string not closed correctly!");
  }

  return buf.substr(0, buf.length() - _pos + startC);
}

wstring TeXParser::getOverArgument() {
  if (_pos == _len) return L"";

  int ogroup = 1, spos;
  wchar_t ch = L'\0';

  spos = _pos;
  while (_pos < _len && ogroup != 0) {
    ch = _latex[_pos];
    switch (ch) {
      case L_GROUP:
        ogroup++;
        break;
      case '&':
        // if a & is encountered at the same level as \over we must break the argument
        if (ogroup == 1) ogroup--;
        break;
      case R_GROUP:
        ogroup--;
        break;
      case ESCAPE:
        _pos++;
        // if a \\ or \cr is encountered at the same level as \over
        // we must break the argument
        if (_pos < _len && _latex[_pos] == '\\' && ogroup == 1) {
          ogroup--;
          _pos--;
        } else if (
          _pos < _len - 1
          && _latex[_pos] == 'c'
          && _latex[_pos + 1] == 'r'
          && ogroup == 1) {
          ogroup--;
          _pos--;
        }
      default:
        break;
    }
    _pos++;
  }

  // end of string reached, bu not processed properly
  if (ogroup >= 2) throw ex_parse("Illegal end, missing '}'!");

  wstring str;
  if (ogroup == 0) {
    str = _latex.substr(spos, _pos - spos - 1);
  } else {
    str = _latex.substr(spos, _pos - spos);
    ch = '\0';
  }

  if (ch == '&' || ch == '\\' || ch == R_GROUP) _pos--;

  return str;
}

wstring TeXParser::getCommand() {
  int spos = ++_pos;
  wchar_t ch = L'\0';

  while (_pos < _len) {
    ch = _latex[_pos];
    if ((ch < 'a' || ch > 'z')
        && (ch < 'A' || ch > 'Z')
        && (_atIsLetter == 0 || ch != '@')
      )
      break;

    _pos++;
  }

  if (ch == L'\0') return L"";

  if (_pos == spos) _pos++;

  wstring com = _latex.substr(spos, _pos - spos);
  if (com == L"cr" && _pos < _len && _latex[_pos] == ' ') _pos++;

  return com;
}

void TeXParser::insert(int beg, int end, const wstring& formula) {
  _latex.replace(beg, end - beg, formula);
  _len = _latex.length();
  _pos = beg;
  _insertion = true;
}

wstring TeXParser::getCommandWithArgs(const wstring& command) {
  // A command whose argument is another command, with no braces between them
  // ("\sqrt\sqrt\sqrt x"), recurses getCommandWithArgs -> getOptsArgs ->
  // getArg -> getCommandWithArgs without ever re-entering parse(), so the
  // guard there does not see it. Share the same counter: the two recursions
  // interleave and it is the combined native depth that overflows the stack.
  ParseDepthGuard depthGuard;

  if (command == L"left") return getGroup(L"\\left", L"\\right");

  auto mac = MacroInfo::get(command);
  if (mac == nullptr) {
    return L"\\" + command;
  }
  int mac_opts = mac->_posOpts;

  // return as format: \cmd[opt][...]{arg}{...}

  vector<wstring> mac_args;
  getOptsArgs(mac->_argc, mac_opts, mac_args);
  wstring mac_arg(L"\\");
  mac_arg.append(command);
  for (int j = 0; j < mac->_posOpts; j++) {
    wstring arg_t = mac_args[mac->_argc + j + 1];
    if (!arg_t.empty()) {
      mac_arg.append(L"[").append(arg_t).append(L"]");
    }
  }

  for (int j = 0; j < mac->_argc; j++) {
    wstring arg_t = mac_args[j + 1];
    if (!arg_t.empty()) {
      mac_arg.append(L"{").append(arg_t).append(L"}");
    }
  }

  return mac_arg;
}

void TeXParser::skipWhiteSpace() {
  wchar_t c;
  while (_pos < _len) {
    c = _latex[_pos];
    if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
    if (c == '\n') {
      _line++;
      _col = _pos;
    }
    _pos++;
  }
}

wstring TeXParser::forwardBalancedGroup() {
  if (_group == 0) {
    const wstring& sub = _latex.substr(_pos);
    finish();
    return sub;
  }
  int closing = _group;
  auto i = _latex.length() - 1;
  for (; i >= _pos; i--) {
    auto ch = _latex[i];
    if (ch == R_GROUP) {
      closing--;
      if (closing == 0) break;
    }
  }
  if (closing != 0) {
    throw ex_parse("Found a closing '}' without an opening '{'!");
  }
  const wstring& sub = _latex.substr(_pos, i - _pos);
  _pos = i;
  return sub;
}

void TeXParser::getOptsArgs(int argc, int opts, Args& args) {
  // Every index below is computed from argc, and the option slots start at
  // argc + 1, so a negative count writes before the start of the vector and a
  // huge one asks for an allocation that cannot succeed. Callers take argc
  // from a MacroInfo, which a user-defined command populates from the formula
  // text, so this is the one place that has to hold the invariant for all of
  // them regardless of how the macro was defined.
  if (!isValidMacroArgc(argc)) {
    throw ex_parse("Bad number of arguments for a command!");
  }

  // A maximum of 10 options can be passed to a command,
  // the value will be added at the tail of the args if found any,
  // the last (maximum to 12th) value is reserved for returned value
  args.resize(argc + 10 + 1 + 1);

  auto getOpts = [&]() {
    int j = argc + 1;
    try {
      for (; j < argc + 11; j++) {
        skipWhiteSpace();
        args[j] = getGroup(L_BRACK, R_BRACK);
      }
    } catch (ex_parse& e) {
      args[j] = L"";
    }
  };

  auto getArg = [&](int i) { // NOLINT(misc-no-recursion)
    skipWhiteSpace();
    try {
      args[i] = getGroup(L_GROUP, R_GROUP);
    } catch (ex_parse& e) {
      if (_latex[_pos] != '\\') {
        args[i] = towstring(_latex[_pos]);
        _pos++;
      } else {
        args[i] = getCommandWithArgs(getCommand());
      }
    }
  };

  if (argc != 0) {
    // we get the options just after the command name
    if (opts == 1) getOpts();
    // we get the first argument
    getArg(1);
    // we get the options after the first argument
    if (opts == 2) getOpts();
    // we get the next arguments
    for (int i = 2; i <= argc; i++) {
      getArg(i);
    }
    if (_isMathMode) skipWhiteSpace();
  }
}

bool TeXParser::isValidName(const wstring& com) const {
  if (com.empty()) return false;
  if (com[0] != '\\') return false;

  wchar_t c = L'\0';
  int p = 1;
  int l = com.length();
  while (p < l) {
    c = com[p];
    if (!isAsciiAlpha(c) && (_atIsLetter == 0 || c != '@'))
      break;
    p++;
  }

  return isAsciiAlpha(c);
}

sptr<Atom> TeXParser::processEscape() {
  _spos = _pos;
  const wstring command = getCommand();

  if (command.length() == 0) return sptrOf<EmptyAtom>();

  auto mac = MacroInfo::get(command);
  if (mac != nullptr) {
    return processCommands(command, mac);
  }

  const string cmd = wide2utf8(command);
  try {
    return Formula::get(command)->_root;
  } catch (ex_formula_not_found& e) {
    try {
      return SymbolAtom::get(cmd);
    } catch (ex_symbol_not_found& ex) {}
  }

  // not a valid command or symbol or predefined Formula found
  if (!_isPartial) {
    throw ex_parse("Unknown symbol or command or predefined Formula: '" + cmd + "'");
  }
  // Show invalid command
  auto rm = sptrOf<RomanAtom>(Formula(L"\\backslash " + command)._root);
  return sptrOf<ColorAtom>(rm, TRANSPARENT, RED);
}

sptr<Atom> TeXParser::processCommands(const wstring& cmd, MacroInfo* mac) {
  int opts = mac->_posOpts;

  Args args;
  getOptsArgs(mac->_argc, opts, args);
  // we promise the first argument is the command name itself
  args[0] = cmd;

  if (NewCommandMacro::isMacro(cmd)) {
    // Same self-reference guard as inflateNewCmd: insert() rewinds the parse
    // position to the replacement, so a macro that expands to itself would
    // loop here forever once preprocess() has given up on it.
    if (++_expansions > kMaxExpansions) {
      throw ex_parse("Too many macro expansions!");
    }
    countExpansion(_len);
    // The last value in "args" is the replacement string
    auto ret = mac->invoke(*this, args);
    insert(_spos, _pos, args.back());
    if (_len > kMaxExpandedLength) {
      throw ex_parse("Formula expands too much!");
    }
    return ret;
  }

  return mac->invoke(*this, args);
}

sptr<Atom> TeXParser::getScripts(wchar_t first) {
  _pos++;
  // get the sub script (assume the first is the sub script)
  sptr<Atom> sub = getArgument();
  sptr<Atom> sup(nullptr);
  wchar_t second = '\0';

  if (_pos < _len) second = _latex[_pos];

  // 4 conditions:
  // 1. \cmd_{\sub}^{\super}
  // 2. \cmd^{\super}_{\sub}
  // 3. \cmd_{\sub}
  // 4. \cmd^{\super}
  if (first == SUPER_SCRIPT && second == SUPER_SCRIPT) {
    sup = sub;
    sub = nullptr;
  } else if (first == SUB_SCRIPT && second == SUPER_SCRIPT) {
    _pos++;
    sup = getArgument();
  } else if (first == SUPER_SCRIPT && second == SUB_SCRIPT) {
    _pos++;
    sup = sub;
    sub = getArgument();
  } else if (first == SUPER_SCRIPT && second != SUB_SCRIPT) {
    sup = sub;
    sub = nullptr;
  }

  sptr<Atom> atom;
  RowAtom* rm = nullptr;
  if (_formula->_root == nullptr) {
    // If there's no root exists, passing a null atom to ScriptsAtom as base is OK,
    // the ScriptsAtom will handle it
    return sptrOf<ScriptsAtom>(nullptr, sub, sup);
  } else if ((rm = dynamic_cast<RowAtom*>(_formula->_root.get()))) {
    atom = rm->popLastAtom();
  } else {
    atom = _formula->_root;
    _formula->_root = nullptr;
  }

  // Check if previous atom is CumulativeScriptsAtom
  auto* ca = dynamic_cast<CumulativeScriptsAtom*>(atom.get());
  if (ca != nullptr) {
    ca->addSubscript(sub);
    ca->addSuperscript(sup);
    return atom;
  }

  if (atom->rightType() == AtomType::bigOperator) {
    return sptrOf<BigOperatorAtom>(atom, sub, sup);
  }

  auto* del = dynamic_cast<OverUnderDelimiter*>(atom.get());
  if (del != nullptr) {
    if (del->isOver()) {
      if (sup != nullptr) {
        del->addScript(sup);
        return sptrOf<ScriptsAtom>(atom, sub, nullptr);
      }
    } else if (sub != nullptr) {
      del->addScript(sub);
      return sptrOf<ScriptsAtom>(atom, nullptr, sup);
    }
  }

  return sptrOf<ScriptsAtom>(atom, sub, sup);
}

sptr<Atom> TeXParser::getArgument() {
  // Looped rather than recursive. Expanding a user macro splices the
  // replacement back into _latex and rewinds the cursor onto it, so the
  // argument has to be read again from the top -- and a self-referential
  // definition (\newcommand{\a}{\a}) makes that happen over and over. Doing
  // it by calling getArgument() again grew the native stack once per
  // expansion, and nothing bounded that growth: the parse-depth guard is
  // taken only in parse() and getCommandWithArgs(), neither of which is on
  // this path, leaving only kMaxExpansions -- about three times more turns
  // than a 1MB stack survives. The overflow was a hardware fault, not an
  // exception, so the caller's catch could not contain it and the process
  // died on "\newcommand{\a}{\a}\frac{x^\a}{y}". The loop keeps the same
  // one-frame cost however many times the argument is re-read, while
  // kMaxExpansions still bounds the number of turns.
  for (;;) {
    skipWhiteSpace();
    wchar_t ch;
    if (_pos < _len) ch = _latex[_pos];
    else return sptrOf<EmptyAtom>();

    if (ch == L_GROUP) {
      Formula tf;
      Formula* tmp = _formula;
      _formula = &tf;
      // The group is parsed into a plain Formula, so row commands (\\, \cr)
      // inside it must not see the enclosing array mode: addRow would
      // downcast the temporary to ArrayFormula (e.g. \\int^{\\infty}, where
      // the first \\ wraps the rest in an ArrayFormula and the second is
      // parsed inside the superscript group).
      const bool arrayMode = _arrayMode;
      _arrayMode = false;
      _pos++;
      _group++;
      try {
        parse();
      } catch (...) {
        // Restore before unwinding: leaving _formula pointing at the dead
        // stack temporary poisons every later parse made with this parser
        // (the shared LaTeX::_formula one lives forever), turning the next
        // tp._formula dereference into a use-after-free.
        _formula = tmp;
        _arrayMode = arrayMode;
        throw;
      }
      _formula = tmp;
      _arrayMode = arrayMode;
      if (_formula->_root == nullptr) {
        auto* rm = new RowAtom();
        rm->add(tf._root);
        return sptr<Atom>(rm);
      }
      return tf._root;
    }

    if (ch == ESCAPE) {
      auto atom = processEscape();
      if (_insertion) {
        _insertion = false;
        continue;
      }
      return atom;
    }

    auto atom = convertCharacter(ch, true);
    _pos++;
    return atom;
  }
}

pair<UnitType, float> TeXParser::getLength() {
  if (_pos == _len) return make_pair(UnitType::none, -1.f);

  wchar_t ch = L'\0';

  skipWhiteSpace();
  const int start = _pos;
  while (_pos < _len && ch != ' ' && ch != ESCAPE) {
    ch = _latex[_pos++];
  }
  const int end = _pos;
  if (ch == '\\') _pos--;
  else skipWhiteSpace();

  return SpaceAtom::getLength(_latex.substr(start, end - start - 1));
}

bool TeXParser::replaceScript() {
  wchar_t ch = _latex[_pos];
  auto it = SUP_SCRIPT_MAP.find(ch);
  if (it != SUP_SCRIPT_MAP.end()) {
    wstring sup = wstring(L"\\mathcumsup{").append(1, (wchar_t) (it->second)).append(L"}");
    _latex.replace(_pos, 1, sup);
    _len = _latex.length();
    _pos += sup.size();
    return true;
  }
  it = SUB_SCRIPT_MAP.find(ch);
  if (it != SUB_SCRIPT_MAP.end()) {
    wstring sub = wstring(L"\\mathcumsub{").append(1, (wchar_t) (it->second)).append(L"}");
    _latex.replace(_pos, 1, sub);
    _len = _latex.length();
    _pos += sub.size();
    return true;
  }
  return false;
}

void TeXParser::preprocess(wstring& cmd, Args& args, int& pos) {
  if (cmd == L"newcommand" || cmd == L"renewcommand") {
    preprocessNewCmd(cmd, args, pos);
  } else if (cmd == L"newenvironment" || cmd == L"renewenvironment") {
    preprocessNewCmd(cmd, args, pos);
  } else if (NewCommandMacro::isMacro(cmd)) {
    inflateNewCmd(cmd, args, pos);
  } else if (cmd == L"begin") {
    inflateEnv(cmd, args, pos);
  } else if (cmd == L"makeatletter") {
    _atIsLetter++;
  } else if (cmd == L"makeatother") {
    _atIsLetter--;
  } else if (_unparsedContents.find(cmd) != _unparsedContents.end()) {
    getOptsArgs(1, 0, args);
  }
}

void TeXParser::preprocessNewCmd(wstring& cmd, Args& args, int& pos) {
  // The macro must exists
  auto mac = MacroInfo::get(cmd);
  getOptsArgs(mac->_argc, mac->_posOpts, args);
  mac->invoke(*this, args);
  _latex.erase(pos, _pos - pos);
  _len = _latex.length();
  _pos = pos;
}

void TeXParser::inflateNewCmd(wstring& cmd, Args& args, int& pos) {
  // A self-referential macro (e.g. \newcommand{\x}{\x}\x) re-inserts its own
  // name and rewinds, so preprocess() would inflate it forever. Bound the
  // total number of inflations per preprocess pass; real formulas use only a
  // handful. In partial mode this is swallowed like any other parse error.
  if (++_expansions > kMaxExpansions) {
    throw ex_parse("Too many macro expansions!");
  }
  countExpansion(_len);
  // The macro must exists
  auto mac = MacroInfo::get(cmd);
  getOptsArgs(mac->_argc, mac->_posOpts, args);
  args[0] = cmd;
  try {
    mac->invoke(*this, args);
    // The last element is the returned value (after inflated macro)
    _latex.replace(pos, _pos - pos, args.back());
  } catch (ex_parse& e) {
    if (!_isPartial) throw;
    pos += cmd.length() + 1;
  }
  _len = _latex.length();
  _pos = pos;
  if (_len > kMaxExpandedLength) {
    throw ex_parse("Formula expands too much!");
  }
}

void TeXParser::inflateEnv(wstring& cmd, Args& args, int& pos) {
  // An environment whose begin-definition opens the same environment
  // (\newenvironment{a}{\begin{a}...}{}) re-inserts \begin{a} and rewinds
  // exactly the way a self-referential macro does, so it needs the same two
  // bounds -- on the number of expansions and on the text they may produce.
  if (++_expansions > kMaxExpansions) {
    throw ex_parse("Too many macro expansions!");
  }
  countExpansion(_len);
  getOptsArgs(1, 0, args);
  wstring env = args[1] + L"@env";
  auto mac = MacroInfo::get(env);
  if (mac == nullptr) {
    throw ex_parse(
      "Unknown environment: "
      + wide2utf8(args[1])
      + " at position " + tostring(getLine())
      + ":" + tostring(getCol())
    );
  }
  vector<wstring> optargs;
  if (mac->_argc > 1) getOptsArgs(mac->_argc - 1, 0, optargs);
  wstring grp = getGroup(L"\\begin{" + args[1] + L"}", L"\\end{" + args[1] + L"}");
  wstring expr = L"{\\makeatletter \\" + args[1] + L"@env";
  for (int i = 1; i <= mac->_argc - 1; i++) expr += L"{" + optargs[i] + L"}";
  expr += L"{" + grp + L"}\\makeatother}";
  _latex.replace(pos, _pos - pos, expr);
  _len = _latex.length();
  _pos = pos;
  if (_len > kMaxExpandedLength) {
    throw ex_parse("Formula expands too much!");
  }
}

void TeXParser::preprocess() {
  if (_len == 0) return;

  wchar_t ch;
  int spos;
  vector<wstring> args;
  while (_pos < _len) {
    if (replaceScript()) continue;

    ch = _latex[_pos];
    switch (ch) {
      case ESCAPE: {
        spos = _pos;
        wstring cmd = getCommand();
        try {
          preprocess(cmd, args, spos);
        } catch (ex_parse& e) {
          if (!_isPartial) throw;
        }
        args.clear();
        break;
      }
      case PERCENT: {
        spos = _pos++;
        wchar_t chr;
        while (_pos < _len) {
          chr = _latex[_pos++];
          if (chr == '\r' || chr == '\n') break;
        }
        if (_pos < _len) _pos--;
        _latex.replace(spos, _pos - spos, L"");
        _len = _latex.length();
        _pos = spos;
        break;
      }
      case DEGRE: {
        _latex.replace(_pos, 1, L"^{\\circ}");
        _len = _latex.length();
        _pos++;
        break;
      }
      default:
        _pos++;
        break;
    }
  }
  _pos = 0;
  _len = _latex.length();
}

void TeXParser::addEmptyRootIfNeeded() {
  // Array mode is excluded because there the cells, not the root, carry the
  // content, and an added atom would become a stray extra cell.
  if (_formula->_root == nullptr && !_arrayMode) {
    _formula->add(sptrOf<EmptyAtom>());
  }
}

void TeXParser::parse() {
  ParseDepthGuard depthGuard;
  // Input that produces no atom at all must still leave a root behind: the
  // ~40 wrapper atoms built from macro arguments (TextStyleAtom, LapedAtom,
  // TypedAtom, ScaleAtom, RotateAtom, ...) dereference their base unchecked.
  // An empty argument was already covered here, but a blank one -- "{ }",
  // "{\t}", "{~}" -- parses successfully and simply adds nothing, so the
  // substitution has to happen after the loop as well, not just on this
  // early return.
  if (_len == 0) {
    addEmptyRootIfNeeded();
    return;
  }

  wchar_t ch;
  while (_pos < _len) {
    ch = _latex[_pos];

    switch (ch) {
      case '\n':
        _line++;
        _col = _pos;
      case '\t':
      case '\r':
        _pos++;
        break;
      case ' ': {
        _pos++;
        if (!_isMathMode) {  // we are in mbox
          _formula->add(sptrOf<SpaceAtom>());
          _formula->add(sptrOf<BreakMarkAtom>());
          while (_pos < _len) {
            ch = _latex[_pos];
            if (ch != ' ' || ch != '\t' || ch != '\r') break;
            _pos++;
          }
        }
      }
        break;
      case DOLLAR: {
        _pos++;
        if (!_isMathMode) {  // we are in mbox
          TexStyle style = TexStyle::text;
          bool doubleDollar = false;
          // Bounds-check: a trailing $ in text mode (e.g. \text{$$}) leaves
          // _pos at _len here, and getDollarGroup can push it one past _len,
          // so an unguarded _latex[_pos] reads out of range.
          if (_pos < _len && _latex[_pos] == DOLLAR) {
            style = TexStyle::display;
            doubleDollar = true;
            _pos++;
          }

          auto atom = sptrOf<MathAtom>(
            Formula(*this, getDollarGroup(DOLLAR), false)._root,
            style
          );
          _formula->add(atom);
          if (doubleDollar) {
            if (_pos < _len && _latex[_pos] == DOLLAR) _pos++;
          }
        }
      }
        break;
      case ESCAPE: {
        sptr<Atom> atom = processEscape();
        _formula->add(atom);
        auto* h = dynamic_cast<HlineAtom*>(atom.get());
        if (_arrayMode && h != nullptr) ((ArrayFormula*) _formula)->addRow();
        if (_insertion) _insertion = false;
      }
        break;
      case L_GROUP: {
        auto atom = getArgument();
        if (atom != nullptr && atom->_type != AtomType::ordinary) {
          atom = privateCopy(atom);
          atom->_type = AtomType::ordinary;
        }
        _formula->add(atom);
      }
        break;
      case R_GROUP: {
        _group--;
        _pos++;
        if (_group == -1) {
          throw ex_parse("Found a closing '}' without an opening '{'!");
        }
        // End of a group
        return;
      }
      case SUPER_SCRIPT: {
        _formula->add(getScripts(ch));
      }
        break;
      case SUB_SCRIPT: {
        if (_isMathMode) {
          _formula->add(getScripts(ch));
        } else {
          _formula->add(sptrOf<UnderScoreAtom>());
          _pos++;
        }
      }
        break;
      case '&': {
        if (!_arrayMode) {
          throw ex_parse("Character '&' is only available in array mode!");
        }
        ((ArrayFormula*) _formula)->addCol();
        _pos++;
      }
        break;
      case '~': {
        _formula->add(sptrOf<SpaceAtom>());
        _pos++;
      }
        break;
      case PRIME_UTF:
      case PRIME:
      case BACKPRIME: {
        // special case for ` and '
        const string symbol = ch == BACKPRIME ? "backprime" : "prime";
        if (_isMathMode) {
          auto atom = sptrOf<CumulativeScriptsAtom>(
            popLastAtom(),
            nullptr,
            SymbolAtom::get(symbol)
          );
          _formula->add(atom);
        } else {
          _formula->add(convertCharacter(ch, true));
        }
        _pos++;
      }
        break;
      case DQUOTE: {
        if (_isMathMode) {
          _formula->add(sptrOf<CumulativeScriptsAtom>(
            popLastAtom(), nullptr, SymbolAtom::get("prime"))
          );
          _formula->add(sptrOf<CumulativeScriptsAtom>(
            popLastAtom(), nullptr, SymbolAtom::get("prime"))
          );
        } else {
          _formula->add(convertCharacter(PRIME, true));
          _formula->add(convertCharacter(PRIME, true));
        }
        _pos++;
      }
        break;
      default: {
        _formula->add(convertCharacter(ch, false));
        _pos++;
      }
        break;
    }
  }

  addEmptyRootIfNeeded();
}

sptr<Atom> TeXParser::convertCharacter(wchar_t c, bool oneChar) {
  if (_isMathMode) {
    // the unicode Greek Letters in math mode are not drawn with the Greek font
    if (c >= 945 && c <= 969) {
      // Greek small letter
      return SymbolAtom::get(Formula::_symbolMappings[c]);
    } else if (c >= 913 && c <= 937) {
      // Greek capital letter
      wstring ltx = utf82wide(Formula::_symbolFormulaMappings[c]);
      return Formula(ltx)._root;
    }
  }

  c = tex::convertToRomanNumber(c);

  /*
   * None alphanumeric character
   */
  if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z')) {
    /*
       * Find from registered UNICODE-table
       */
    const UnicodeBlock& block = UnicodeBlock::of(c);
#ifdef HAVE_LOG
    int idx = indexOf(DefaultTeXFont::_loadedAlphabets, block);
    __log << "block of char: " << std::to_string(c) << " is " << idx << endl;
#endif  // HAVE_LOG
    bool exist = (indexOf(DefaultTeXFont::_loadedAlphabets, block) != -1);
    if (!_isLoading && !exist) {
      auto it = DefaultTeXFont::_registeredAlphabets.find(block);
      if (it != DefaultTeXFont::_registeredAlphabets.end())
        DefaultTeXFont::addAlphabet(DefaultTeXFont::_registeredAlphabets[block]);
    }

    auto sit = Formula::_symbolMappings.find(c);
    auto fit = Formula::_symbolFormulaMappings.find(c);

    /*
       * Character not in the symbol-mapping and not in the formula-mapping, find from
       * external font-mapping
       */
    if (sit == Formula::_symbolMappings.end() &&
        fit == Formula::_symbolFormulaMappings.end()) {
      FontInfos* fontInfos = nullptr;
      bool isLatin = UnicodeBlock::BASIC_LATIN == block;
      if ((isLatin && Formula::isRegisteredBlock(UnicodeBlock::BASIC_LATIN)) || !isLatin) {
        fontInfos = Formula::getExternalFont(block);
      }
      if (fontInfos != nullptr) {
        if (oneChar) return sptrOf<TextRenderingAtom>(towstring(c), fontInfos);
        int start = _pos++;
        int en = _len - 1;
        while (_pos < _len) {
          c = _latex[_pos];
          if (!block.contains(c)) {
            en = --_pos;
            break;
          }
          _pos++;
        }
        return sptrOf<TextRenderingAtom>(
          _latex.substr(start, en - start + 1), fontInfos);
      }

      if (!_isPartial)
        throw ex_parse("Unknown character : '" + tostring(c) + "'");
      else {
        if (_hideUnknownChar) return nullptr;
        sptr<Atom> rm(new RomanAtom(
          Formula(L"\\text{(unknown char " + towstring((int) c) + L")}")._root));
        return sptrOf<ColorAtom>(rm, TRANSPARENT, RED);
      }
    } else {
      /*
           * In text mode (with command \text{})
           */
      if (!_isMathMode) {
        auto it = Formula::_symbolTextMappings.find(c);
        if (it != Formula::_symbolTextMappings.end()) {
          // Copy before writing: SymbolAtom::get() hands out an entry of the
          // process-wide symbol cache, so stamping the unicode straight into
          // it changes that symbol for every formula parsed afterwards --
          // a \text{} holding U+FF08 left the shared "lbrack" atom, the one
          // every ordinary "(" resolves to, stamped for the life of the
          // process.
          // The lookup can still miss: a few mappings name glyphs that no
          // registered symbol provides (the Greek capitals map to the glyph
          // itself, e.g. "Α"), and ex_symbol_not_found is not an ex_parse, so
          // it is not swallowed by partial mode and killed the whole formula.
          // Fall through to the formula-mapping/character paths instead.
          try {
            auto atom = sptrOf<SymbolAtom>(*SymbolAtom::get(it->second));
            atom->setUnicode(c);
            return atom;
          } catch (ex_symbol_not_found&) {
          }
        }
      }
      auto it = Formula::_symbolFormulaMappings.find(c);
      if (it != Formula::_symbolFormulaMappings.end()) {
        wstring wstr = utf82wide(it->second);
        return Formula(wstr)._root;
      }

      if (sit != Formula::_symbolMappings.end()) {
        string symbolName = sit->second;
        try {
          return SymbolAtom::get(symbolName);
        } catch (ex_symbol_not_found& e) {
          throw ex_parse(
            "The character '" + tostring(c) +
            "' was mapped to an unknown symbol with the name '" + symbolName + "'!",
            e
          );
        }
      }
    }
  } else {
    /*
       * Alphanumeric character
       */
    FontInfos* infos = nullptr;
    auto it = Formula::_externalFontMap.find(UnicodeBlock::BASIC_LATIN);
    if (it != Formula::_externalFontMap.end()) {
      infos = it->second;
      if (oneChar) return sptrOf<TextRenderingAtom>(towstring(c), infos);

      int start = _pos++;
      int en = _len - 1;
      while (_pos < _len) {
        c = _latex[_pos];
        if ((c < '0' || c > '9') && (c < 'a' || c > 'z') && (c < 'A' || c > 'Z')) {
          en = --_pos;
          break;
        }
        _pos++;
      }
      return sptrOf<TextRenderingAtom>(_latex.substr(start, en - start + 1), infos);
    }
  }
  return sptrOf<CharAtom>(c, _formula->_textStyle, _isMathMode);
}
