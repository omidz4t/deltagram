#include "core/formula.h"

#include "common.h"
#include "core/core.h"
#include "core/parser.h"
#include "fonts/alphabet.h"
#include "fonts/fonts.h"
// PROBE PATCH: tinyxml2-backed loaders removed.

using namespace std;
using namespace tex;

map<wstring, sptr<Formula>> Formula::_predefinedTeXFormulas;

map<UnicodeBlock, FontInfos*> Formula::_externalFontMap;

float Formula::PIXELS_PER_POINT = 1.f;

void Formula::_init_() {
#ifdef HAVE_LOG
  __dbg("%s\n", "init formula");
#endif  // HAVE_LOG
  // Register external alphabet
  DefaultTeXFont::registerAlphabet(new CyrillicRegistration());
  DefaultTeXFont::registerAlphabet(new GreekRegistration());
#ifdef HAVE_LOG
  __log << "elements in _symbolMappings:" << endl;
  for (auto i : _symbolMappings)
    __log << "\t" << i.first << "->" << i.second << endl;
  __log << "elements in _symbolTextMappings:" << endl;
  for (auto i : _symbolTextMappings)
    __log << "\t" << i.first << "->" << i.second << endl;
  __log << "elements in _symbolFormulaMappings:" << endl;
  for (auto i : _symbolFormulaMappings)
    __log << "\t" << i.first << "->" << i.second << endl;
#endif  // HAVE_LOG
}

Formula::Formula(
  const TeXParser& tp,
  const wstring& latex,
  const string& textStyle,
  bool preprocess, bool isMathMode
) : _parser(tp.isPartial(), latex, this, preprocess, isMathMode) {
  _textStyle = textStyle;
  _xmlMap = tp._formula->_xmlMap;
  if (tp.isPartial()) {
    try {
      _parser.parse();
    } catch (exception& e) {
      if (_root == nullptr) _root = sptrOf<EmptyAtom>();
    }
  } else {
    _parser.parse();
  }
}

Formula::Formula(const TeXParser& tp, const wstring& latex, bool preprocess)
  : _parser(tp.isPartial(), latex, this, preprocess) {
  _textStyle = "";
  _xmlMap = tp._formula->_xmlMap;
  if (tp.isPartial()) {
    try {
      _parser.parse();
    } catch (exception& e) {
      // Same fallback as the sibling constructors: callers use _root
      // unchecked (e.g. StyleAtom from \displaystyle), so a swallowed
      // parse failure must not leave it null.
      if (_root == nullptr) _root = sptrOf<EmptyAtom>();
    }
  } else {
    _parser.parse();
  }
}

Formula::Formula(const TeXParser& tp, const wstring& latex)
  : _parser(tp.isPartial(), latex, this) {
  _textStyle = "";
  _xmlMap = tp._formula->_xmlMap;
  if (tp.isPartial()) {
    try {
      _parser.parse();
    } catch (exception& e) {
      if (_root == nullptr) _root = sptrOf<EmptyAtom>();
    }
  } else {
    _parser.parse();
  }
}

Formula::Formula() : _parser(L"", this, false) {}

Formula::Formula(const wstring& latex) : _parser(latex, this) {
  _textStyle = "";
  _parser.parse();
}

Formula::Formula(const wstring& latex, bool preprocess) : _parser(latex, this, preprocess) {
  _textStyle = "";
  _parser.parse();
}

void Formula::setLaTeX(const wstring& latex) {
  _parser.reset(latex);
  if (!latex.empty()) _parser.parse();
}

Formula* Formula::add(const sptr<Atom>& a) {
  if (a == nullptr) return this;
  auto atom = dynamic_pointer_cast<MiddleAtom>(a);
  if (atom != nullptr) _middle.push_back(atom);
  if (_root == nullptr) {
    _root = a;
    return this;
  }
  auto* rm = dynamic_cast<RowAtom*>(_root.get());
  if (rm == nullptr) _root = sptrOf<RowAtom>(_root);
  rm = static_cast<RowAtom*>(_root.get());
  rm->add(a);
  auto* ta = dynamic_cast<TypedAtom*>(a.get());
  if (ta != nullptr) {
    AtomType rt = ta->rightType();
    if (rt == AtomType::binaryOperator || rt == AtomType::relation) {
      rm->add(sptrOf<BreakMarkAtom>());
    }
  }
  return this;
}

sptr<Box> Formula::createBox(Environment& style) {
  if (_root == nullptr) return sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f);
  return _root->createBox(style);
}
#ifdef DEBUG
#undef DEBUG
#endif
void Formula::setDEBUG(bool b) {
  Box::DEBUG = b;
}

sptr<Formula> Formula::get(const wstring& name) {
  auto it = _predefinedTeXFormulas.find(name);
  if (it == _predefinedTeXFormulas.end()) {
    auto i = _predefinedTeXFormulasAsString.find(name);
    if (i == _predefinedTeXFormulasAsString.end())
      throw ex_formula_not_found(wide2utf8(name));
    auto tf = sptrOf<Formula>(i->second);
    auto* ra = dynamic_cast<RowAtom*>(tf->_root.get());
    if (ra == nullptr) {
      _predefinedTeXFormulas[name] = tf;
    }
    return tf;
  }
  return it->second;
}

void Formula::setDPITarget(float dpi) {
  PIXELS_PER_POINT = dpi / 72.f;
}

bool Formula::isRegisteredBlock(const UnicodeBlock& block) {
  return _externalFontMap.find(block) != _externalFontMap.end();
}

FontInfos* Formula::getExternalFont(const UnicodeBlock& block) {
  auto it = _externalFontMap.find(block);
  FontInfos* infos = nullptr;
  if (it == _externalFontMap.end()) {
    infos = new FontInfos("SansSerif", "Serif");
    _externalFontMap[block] = infos;
  } else {
    infos = it->second;
  }
  return infos;
}

void Formula::addSymbolMappings(const string& file) {
  // PROBE PATCH: TeXFormulaSettingParser (tinyxml2-backed) removed. The
  // built-in symbol/formula tables registered at startup are authoritative.
  throw ex_invalid_state(
    "Formula::addSymbolMappings(file) is disabled in this MicroTeX build "
    "(XML resource loaders removed): " + file);
}

void Formula::_free_() {
  for (auto i : _externalFontMap) delete i.second;
}

/*************************************** ArrayFormula implementation ******************************/

ArrayFormula::ArrayFormula() : _row(0), _col(0) {
  _array.emplace_back();
}

void ArrayFormula::addCol() {
  _array[_row].push_back(_root);
  _root = nullptr;
  _col++;
}

void ArrayFormula::addCol(int n) {
  _array[_row].push_back(_root);
  for (int i = 1; i < n - 1; i++) {
    _array[_row].push_back(nullptr);
  }
  _root = nullptr;
  _col += n;
}

void ArrayFormula::insertAtomIntoCol(int col, const sptr<Atom>& atom) {
  _col++;
  for (size_t j = 0; j < _row; j++) {
    auto& row = _array[j];
    // Rows may be shorter than the insertion column: checkDimensions() leaves
    // rows starting with an empty or intertext cell unpadded (e.g. the empty
    // first row of \begin{array}{ccc@{x}}\\a&b&c\end{array} keeps one cell
    // while cols() is 3), so inserting at begin() + col would write past the
    // end of such a row. Pad with empty cells first, which also places the
    // @-separator atom in its intended column for those rows.
    if (row.size() < size_t(col)) row.resize(col, nullptr);
    row.insert(row.begin() + col, atom);
  }
}

void ArrayFormula::addRow() {
  addCol();
  _array.emplace_back();
  _row++;
  _col = 0;
}

void ArrayFormula::addRowSpecifier(const sptr<CellSpecifier>& spe) {
  auto it = _rowSpecifiers.find(_row);
  if (it == _rowSpecifiers.end()) {
    _rowSpecifiers[_row] = vector<sptr<CellSpecifier>>();
  }
  _rowSpecifiers[_row].push_back(spe);
}

void ArrayFormula::addCellSpecifier(const sptr<CellSpecifier>& spe) {
  string str = tostring(_row) + tostring(_col);
  auto it = _cellSpecifiers.find(str);
  if (it == _cellSpecifiers.end()) {
    _cellSpecifiers[str] = vector<sptr<CellSpecifier>>();
  }
  _cellSpecifiers[str].push_back(spe);
}

int ArrayFormula::rows() const {
  return _row;
}

int ArrayFormula::cols() const {
  return _col;
}

sptr<VRowAtom> ArrayFormula::getAsVRow() {
  auto vr = sptrOf<VRowAtom>();
  vr->setAddInterline(true);
  for (auto& c : _array) {
    for (auto& j : c) vr->append(j);
  }
  return vr;
}

void ArrayFormula::checkDimensions() {
  if (!_array.back().empty() || _root != nullptr) addRow();

  _row = _array.size() - 1;
  _col = _array[0].size();

  // Find the column count of the widest row
  for (size_t i = 1; i < _row; i++) {
    if (_array[i].size() > _col) _col = _array[i].size();
  }

  // The padding below materializes _row*_col cells, and nothing else bounds
  // that product on this path: the matching cap in MatrixAtom runs later, at
  // layout, and the top-level \\ path never reaches a MatrixAtom at all. A
  // formula within the input length limit can still pair thousands of
  // columns with thousands of rows, which is hundreds of megabytes of padded
  // empty cells here (and again in getAsVRow), so enforce the layout cap's
  // value before allocating. Real matrices are nowhere near it.
  constexpr int64_t kMaxArrayCells = 100000;
  if (int64_t(_row) * int64_t(_col) > kMaxArrayCells)
    throw ex_parse("Matrix is too large!");

  for (size_t i = 0; i < _row; i++) {
    size_t j = _array[i].size();
    if (j != _col && _array[i][0] != nullptr && _array[i][0]->_type != AtomType::interText) {
      // Fill the row with null atom
      vector<sptr<Atom>>& r = _array[i];
      for (; j < _col; j++) r.push_back(nullptr);
    }
  }
}
