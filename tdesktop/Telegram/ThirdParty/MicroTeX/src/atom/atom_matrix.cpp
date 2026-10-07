#include <memory>

#include "atom/atom_impl.h"

using namespace std;
using namespace tex;

color MatrixAtom::LINE_COLOR = transparent;

map<wstring, wstring> MatrixAtom::_colspeReplacement;

SpaceAtom MatrixAtom::_hsep(UnitType::em, 1.f, 0.f, 0.f);
SpaceAtom MatrixAtom::_semihsep(UnitType::em, 0.5f, 0.f, 0.f);
SpaceAtom MatrixAtom::_vsep_in(UnitType::ex, 0.f, 1.f, 0.f);
SpaceAtom MatrixAtom::_vsep_ext_top(UnitType::ex, 0.f, 0.5f, 0.f);
SpaceAtom MatrixAtom::_vsep_ext_bot(UnitType::ex, 0.f, 0.5f, 0.f);
SpaceAtom MatrixAtom::_align(SpaceType::medMuSkip);

sptr<Box> MatrixAtom::_nullbox(new StrutBox(0.f, 0.f, 0.f, 0.f));

void MatrixAtom::defineColumnSpecifier(const wstring& rep, const wstring& spe) {
  _colspeReplacement[rep] = spe;
}

void MatrixAtom::resetState() {
  // There are no built-in column types, so clearing is enough; the line
  // color goes back to its default (transparent = use current foreground).
  _colspeReplacement.clear();
  LINE_COLOR = transparent;
}

void MatrixAtom::parsePositions(wstring opt, vector<Alignment>& lpos) {
  int len = opt.length();
  int pos = 0;
  wchar_t ch;
  sptr<Formula> tf;
  sptr<TeXParser> tp;
  // clear first
  lpos.clear();
  // Total work budget for the whole spec. A self-referential \newcolumntype
  // (e.g. \newcolumntype{A}{A} then \begin{array}{A}) re-inserts its own name
  // and rewinds, looping forever; nested or repeated expansions can also run
  // away. Each loop turn produces at most one column or one expansion step,
  // so a step cap bounds them all. Real specs need only a handful of turns.
  constexpr int kMaxColumnSpecSteps = 100000;
  // Bounding the number of turns is not enough on its own: a turn splices its
  // expansion into the spec and rewinds, and both the splice and the lookup
  // below cost time proportional to what the spec has grown to. A definition
  // that grows it every turn (\newcolumntype{A}{AA}) therefore pays a rising
  // price for each of the allowed turns and never finishes. Cap the expanded
  // length as well; it is two orders of magnitude past any real spec, and a
  // spec this long describes more columns than can be laid out anyway.
  constexpr int kMaxColumnSpecLength = 10000;
  // The user-defined column types are matched longest-first, so nothing longer
  // than the longest registered name can match. Without that bound the scan
  // built and hashed every remaining suffix of the spec for each unrecognised
  // character -- cubic in the spec length, which froze the parse for tens of
  // seconds on \begin{array}{QQQ...Q}. With no types registered it is skipped.
  size_t longestColumnType = 0;
  for (const auto& r : _colspeReplacement) {
    longestColumnType = max(longestColumnType, r.first.length());
  }
  int steps = 0;
  while (pos < len) {
    if (++steps > kMaxColumnSpecSteps || len > kMaxColumnSpecLength)
      throw ex_parse("Column specification is too complex!");
    ch = opt[pos];
    switch (ch) {
      case 'l':
        lpos.push_back(Alignment::left);
        break;
      case 'r':
        lpos.push_back(Alignment::right);
        break;
      case 'c':
        lpos.push_back(Alignment::center);
        break;
      case '|': {
        int nb = 1;
        while (++pos < len) {
          ch = opt[pos];
          if (ch != '|') {
            pos--;
            break;
          } else {
            nb++;
          }
        }
        _vlines[lpos.size()] = sptrOf<VlineAtom>(nb);
      }
        break;
      case '@': {
        pos++;
        tf = sptrOf<Formula>();
        tp = sptrOf<TeXParser>(_isPartial, opt.substr(pos), tf.get(), false);
        auto atom = tp->getArgument();
        // Keep columns same with the matrix
        if (lpos.size() > _matrix->cols()) lpos.resize(_matrix->cols());
        _matrix->insertAtomIntoCol(lpos.size(), atom);

        lpos.push_back(Alignment::none);
        pos += tp->getPos();
        pos--;
      }
        break;
      case '*': {
        pos++;
        tf = sptrOf<Formula>();
        tp = sptrOf<TeXParser>(_isPartial, opt.substr(pos), tf.get(), false);
        vector<wstring> args;
        tp->getOptsArgs(2, 0, args);
        pos += tp->getPos();
        int nrep = 0;
        valueof(args[1], nrep);
        // Reject an out-of-range column-spec repeat count: an unbounded user
        // value (e.g. \begin{array}{*{99999999}{c}}) would build a gigantic
        // spec string and one column per copy, allocating gigabytes and
        // stalling. Throwing (rather than silently clamping) avoids rendering
        // a different number of columns than requested.
        constexpr int kMaxColumnSpecRepeat = 1000;
        if (nrep < 0 || nrep > kMaxColumnSpecRepeat)
          throw ex_parse("Bad column-spec repeat count!");
        // Check the product before building it: the expanded length is only
        // examined at the top of the next loop turn, so building the repeat
        // first (\begin{array}{*{1000}{<30K chars>}}) transiently allocates
        // a few hundred MB before that check throws it away.
        if (len + (long long) nrep * (long long) args[2].length()
            > kMaxColumnSpecLength) {
          throw ex_parse("Column specification is too complex!");
        }
        wstring str;
        for (int j = 0; j < nrep; j++) str += args[2];
        opt.insert(pos, str);
        len = opt.length();
        pos--;
      }
        break;
      case '>': {
        pos++;
        tf = sptrOf<ArrayFormula>();
        tp = sptrOf<TeXParser>(_isPartial, opt.substr(pos), &(*tf), false);
        sptr<Atom> cs = tp->getArgument();
        _columnSpecifiers[lpos.size()] = cs;
        pos += tp->getPos();
        pos--;
      }
        break;
      case ' ':
      case '\t':
        break;
      default: {
        int spos = pos + int(min(size_t(len - pos), longestColumnType)) + 1;
        bool hasrep = false;
        while (--spos > pos) {
          // Charge the probes against the step budget too: each one builds
          // and hashes a substring, so a long registered column-type name
          // (up to the 16-char cap) would otherwise buy up to 16 probes of
          // up to 16 chars per spec character without the budget noticing.
          if (++steps > kMaxColumnSpecSteps)
            throw ex_parse("Column specification is too complex!");
          auto it = _colspeReplacement.find(opt.substr(pos, spos - pos));
          if (it != _colspeReplacement.end()) {
            hasrep = true;
            opt.insert(spos, it->second);
            len = opt.length();
            pos = spos - 1;
            break;
          }
        }
        if (!hasrep) lpos.push_back(Alignment::center);
      }
        break;
    }
    pos++;
  }

  for (size_t j = lpos.size(); j < _matrix->cols(); j++) lpos.push_back(Alignment::center);

  if (lpos.empty()) lpos.push_back(Alignment::center);
}

std::vector<float> MatrixAtom::getColumnSep(Environment& env, float width) {
  const int cols = _matrix->cols();
  std::vector<float> arr(cols + 1, 0.f);
  sptr<Box> Align, AlignSep, Hsep;
  float h, w = env.getTextWidth();
  int i = 0;

  if (_matType == MatrixType::aligned || _matType == MatrixType::alignedAt) w = POS_INF;

  switch (_matType) {
    case MatrixType::array: {
      // Array: (hsep_col/2 or 0) elem hsep_col elem hsep_col ... hsep_col elem (hsep_col/2 or 0)
      Hsep = _hsep.createBox(env);
      for (int i = 0; i < cols; i++) {
        if (_position[i] == Alignment::none) {
          arr[i] = arr[i + 1] = 0;
          i++;
        } else {
          arr[i] = Hsep->_width;
        }
      }
      if (_spaceAround) {
        const auto half = Hsep->_width / 2;
        if (_position.front() != Alignment::none) arr[0] = half;
        if (_position.back() != Alignment::none) arr[cols] = half;
      }
      return arr;
    }
    case MatrixType::matrix:
    case MatrixType::smallMatrix: {
      // Simple matrix: 0 elem hsep_col elem hsep_col ... hsep_col elem 0
      arr[0] = 0;
      arr[cols] = arr[0];
      Hsep = _hsep.createBox(env);
      for (i = 1; i < cols; i++) arr[i] = Hsep->_width;
      return arr;
    }
    case MatrixType::aligned:
    case MatrixType::align: {
      // Align env: hsep = (textwidth - matwidth) / (2n + 1)
      // Spaces: hsep eq_left \medskip eq_right hsep ... hsep elem hsep
      Align = _align.createBox(env);
      if (w != POS_INF) {
        h = max((w - width - cols / 2 * Align->_width) / floor((cols + 3) / 2.f), 0.f);
        AlignSep = sptrOf<StrutBox>(h, 0.f, 0.f, 0.f);
      } else {
        AlignSep = _hsep.createBox(env);
      }

      arr[cols] = AlignSep->_width;
      for (int i = 0; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = AlignSep->_width;
        else
          arr[i] = Align->_width;
      }
    }
      break;
    case MatrixType::alignedAt:
    case MatrixType::alignAt: {
      // Aignat env: hsep = (textwidth - matwdith) / 2
      // Spaces: hsep elem ... elem hsep
      if (w != POS_INF)
        h = max((w - width) / 2, 0.f);
      else
        h = 0;
      Align = _align.createBox(env);
      arr[0] = h;
      arr[cols] = arr[0];
      for (int i = 1; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = 0;
        else
          arr[i] = Align->_width;
      }
    }
      break;
    case MatrixType::flAlign: {
      // flalgin env : hsep = (textwidth - matwidth) / (2n + 1)
      // Spaces: hsep eq_left \medskip el_right hsep ... hsep elem hsep
      Align = _align.createBox(env);
      if (w != POS_INF) {
        h = max((w - width - (cols / 2) * Align->_width) / floor((cols - 1) / 2.f), 0.f);
        AlignSep = sptrOf<StrutBox>(h, 0.f, 0.f, 0.f);
      } else {
        AlignSep = _hsep.createBox(env);
      }

      arr[0] = 0;
      arr[cols] = arr[0];
      for (int i = 1; i < cols; i++) {
        if (i % 2 == 0)
          arr[i] = AlignSep->_width;
        else
          arr[i] = Align->_width;
      }
    }
      break;
  }

  if (w == POS_INF) {
    arr[0] = 0;
    arr[cols] = arr[0];
  }

  return arr;
}

void MatrixAtom::recalculateLine(
  const int rows,
  std::vector<std::vector<sptr<Box>>>& boxarr,
  vector<sptr<Atom>>& multiRows,
  float* height,
  float* depth,
  float drt,
  float vspace
) {
  const size_t s = multiRows.size();
  for (size_t i = 0; i < s; i++) {
    auto* m = dynamic_cast<MultiRowAtom*>(multiRows[i].get());
    if (m == nullptr) continue;
    const int r = m->_i;
    const int c = m->_j;
    int n = m->_n;
    int skipped = 0;
    float h = 0;
    if (n < 0) {
      // Across from bottom to top
      int j = r;
      // The span has to end on a row that holds cells. Deriving that row from
      // the loop counter alone lands on a rule row when the scan stops at one
      // (\begin{array}{cc}\hline\hline\multirow{-3}{*}{x}&b\end{array} breaks
      // out at row 0 and pointed at row 1, a rule). The swap below then left
      // a rule box over a cell holding the \multirow atom, and the row loop
      // in createBox reinterpreted that atom as an HlineAtom. Remember the
      // topmost row actually spanned instead; without rules in the way it is
      // the same row the counter produced.
      int top = r;
      for (; j >= 0 && j > r + n; j--) {
        if (boxarr[j][0]->_type == AtomType::hline) {
          if (j == 0) break;
          h += drt;
          n--;
        } else {
          skipped++;
          top = j;
          h += height[j] + depth[j] + vspace;
        }
      }
      m->_i = top;
      auto tmp = boxarr[r][c];
      boxarr[r][c] = boxarr[top][c];
      boxarr[top][c] = tmp;
    } else {
      // Across from top to bottom
      for (int j = r; j < r + n && j < rows; j++) {
        if (boxarr[j][0]->_type == AtomType::hline) {
          if (j == rows - 1) break;
          h += drt;
          n++;
        } else {
          skipped++;
          h += height[j] + depth[j] + vspace;
        }
      }
    }
    m->_n = abs(n);
    auto b = boxarr[m->_i][m->_j];
    const float bh = b->_height + b->_depth + vspace;
    if (h > bh) {
      b->_height = (h - bh + vspace) / 2.f;
    } else if (h < bh && skipped > 0) {
      const float ex = (bh - h) / skipped / 2.f;
      // Clamp the span end to the real row count. A multirow whose requested
      // span exceeds the rows below it (e.g. \multirow{20}{*}{x} in a
      // single-row matrix) keeps _n at the full span, so an unclamped
      // m->_i + m->_n walks off boxarr/height/depth. The top-to-bottom scan
      // above already clamps with `j < rows`; this loop must too.
      const int mr = min(m->_i + m->_n, rows);
      for (int j = m->_i; j < mr; j++) {
        if (boxarr[j][0]->_type != AtomType::hline) {
          height[j] += ex;
          depth[j] += ex;
        }
      }
      b->_height = height[m->_i];
      b->_depth = bh - b->_height - vspace;
    }
    boxarr[m->_i][m->_j]->_type = AtomType::none;
  }
}

int MatrixAtom::multicolumnSpan(const MulticolumnAtom* mca, int j) const {
  // A cell can ask for more columns than the matrix has left: the widths and
  // separators it sums live in arrays sized by the column count, so a span
  // reaching past the last column reads beyond them.
  return max(1, min(mca->skipped(), _matrix->cols() - j));
}

sptr<Box> MatrixAtom::generateMulticolumn(
  Environment& env,
  const sptr<Box>& b,
  const float* hsep,
  const float* colWidth,
  MulticolumnAtom* mca,
  int j
) {
  float w = 0;
  int k, n = multicolumnSpan(mca, j);
  for (k = j; k < j + n - 1; k++) {
    w += colWidth[k] + hsep[k + 1];
    auto it = _vlines.find(k + 1);
    if (it != _vlines.end()) w += it->second->getWidth(env);
  }
  w += colWidth[k];

  if (mca->isNeedWidth() && mca->colWidth() <= PREC) {
    mca->setColWidth(w);
    return mca->createBox(env);
  }

  if (b->_width >= w) return b;

  return sptrOf<HBox>(b, w, mca->align());
}

MatrixAtom::MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, const wstring& options, bool spaceAround) {
  _matrix = arr;
  _matType = MatrixType::array;
  _isPartial = isPartial;
  _spaceAround = spaceAround;
  parsePositions(wstring(options), _position);
}

MatrixAtom::MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, const wstring& options) {
  _matrix = arr;
  _matType = MatrixType::array;
  _isPartial = isPartial;
  _spaceAround = false;
  parsePositions(wstring(options), _position);
}

MatrixAtom::MatrixAtom(bool isPartial, const sptr<ArrayFormula>& arr, MatrixType type) {
  _matrix = arr;
  _matType = type;
  _isPartial = isPartial;
  _spaceAround = false;

  const int cols = arr->cols();
  if (type != MatrixType::matrix && type != MatrixType::smallMatrix) {
    _position.resize(cols);
    for (size_t i = 0; i < cols; i += 2) {
      _position[i] = Alignment::right;
      if (i + 1 < cols) _position[i + 1] = Alignment::left;
    }
  } else {
    _position.resize(cols);
    for (size_t i = 0; i < cols; i++) _position[i] = Alignment::center;
  }
}

void MatrixAtom::applyCell(WrapperBox& box, int i, int j) {
  // 1. apply column specifier
  const auto col = _columnSpecifiers.find(j);
  if (col != _columnSpecifiers.end()) {
    auto spe = col->second;
    RowAtom* p = nullptr;
    auto* r = dynamic_cast<RowAtom*>(spe.get());
    while (r != nullptr) {
      spe = r->getFirstAtom();
      p = r;
      r = dynamic_cast<RowAtom*>(spe.get());
    }
    if (p != nullptr) {
      for (size_t k = 0; k < p->size(); k++) {
        CellSpecifier* s = dynamic_cast<CellSpecifier*>(p->get(k).get());
        if (s != nullptr) {
          s->apply(box);
        }
      }
    }
  }
  // 2. apply row specifier
  const auto row = _matrix->_rowSpecifiers.find(i);
  if (row != _matrix->_rowSpecifiers.end()) {
    for (const auto& s : row->second) s->apply(box);
  }
  // 3. cell specifier
  const string key = tostring(i) + tostring(j);
  auto cell = _matrix->_cellSpecifiers.find(key);
  if (cell != _matrix->_cellSpecifiers.end()) {
    for (const auto& s : cell->second) s->apply(box);
  }
}

sptr<Box> MatrixAtom::createBox(Environment& e) {
  const int rows = _matrix->rows();
  const int cols = _matrix->cols();

  // Defense-in-depth: this allocates rows*cols box pointers and visits every
  // cell, so an array with an enormous total cell count (reachable even from
  // modest input -- one wide row plus many rows) would allocate and churn
  // unboundedly. Real matrices are tiny; reject absurd totals. Empty arrays
  // (0 cells) pass through and render as before.
  constexpr int64_t kMaxArrayCells = 100000;
  if (int64_t(rows) * int64_t(cols) > kMaxArrayCells)
    throw ex_parse("Matrix is too large!");

  // RAII containers, not raw arrays: a throw anywhere below (the box budget
  // is the reachable one) must not leak the half-built grid.
  std::vector<float> lineDepth(rows, 0.f);
  std::vector<float> lineHeight(rows, 0.f);
  std::vector<float> colWidth(cols, 0.f);
  std::vector<std::vector<sptr<Box>>> boxarr(rows, std::vector<sptr<Box>>(cols));

  float matW = 0;
  float drt = e.getTeXFont()->getDefaultRuleThickness(e.getStyle());

  // Assigning *(e.copy()) into e destroyed the object being copied halfway
  // through: copy() parks the only owning reference in e's own _copy member,
  // and the implicit assignment reaches _copy before the seven shared pointers
  // declared after it, so it dropped that reference and then read the freed
  // environment for the rest of the copy. It also left the caller's own
  // environment in script style with its text width and interline reset, which
  // no caller expects -- the smaller style belongs to this matrix alone.
  // Keep the smaller environment in a local owning pointer instead and build
  // every cell against that.
  sptr<Environment> smallEnv;
  if (_matType == MatrixType::smallMatrix) {
    smallEnv = e.copy();
    smallEnv->setStyle(TexStyle::script);
  }
  Environment& env = smallEnv == nullptr ? e : *smallEnv;

  // multi-column & multi-row atoms
  vector<sptr<Atom>> listMultiCol;
  vector<sptr<Atom>> listMultiRow;
  for (int i = 0; i < rows; i++) {
    lineDepth[i] = 0;
    lineHeight[i] = 0;
    const int size = _matrix->_array[i].size();
    for (int j = 0; j < cols; j++) {
      if (j >= size) {
        // If current row is not full-filled, fill the row with _nullbox
        for (int k = j; k < cols; k++) boxarr[i][k] = _nullbox;
        break;
      }

      sptr<Atom> atom = _matrix->_array[i][j];
      boxarr[i][j] = (atom == nullptr) ? _nullbox : atom->createBox(env);
      if (atom != nullptr && atom->_type == AtomType::interText) {
        boxarr[i][j]->_type = AtomType::interText;
      }

      auto* mra = (boxarr[i][j]->_type == AtomType::multiRow)
                  ? dynamic_cast<MultiRowAtom*>(atom.get())
                  : nullptr;
      auto* mca = (boxarr[i][j]->_type == AtomType::multiColumn)
                  ? dynamic_cast<MulticolumnAtom*>(atom.get())
                  : nullptr;

      if (mra == nullptr) {
        // Find the highest line (row)
        lineDepth[i] = max(boxarr[i][j]->_depth, lineDepth[i]);
        lineHeight[i] = max(boxarr[i][j]->_height, lineHeight[i]);
      } else {
        mra->setRowColumn(i, j);
        listMultiRow.push_back(atom);
      }

      if (mca == nullptr) {
        // Find the widest column
        colWidth[j] = max(boxarr[i][j]->_width, colWidth[j]);
      } else {
        mca->setRowColumn(i, j);
        listMultiCol.push_back(atom);
      }
    }
  }

  for (int j = 0; j < cols; j++) matW += colWidth[j];

  // The horizontal separator's width
  const auto Hsep = getColumnSep(env, matW);

  for (auto& i : listMultiCol) {
    auto* multi = (MulticolumnAtom*) i.get();
    const int c = multi->col(), r = multi->row();
    const int n = multicolumnSpan(multi, c);
    float w = 0;
    int j = 0;
    for (j = c; j < c + n - 1; j++) w += colWidth[j] + Hsep[j + 1];
    w += colWidth[j];
    if (boxarr[r][c]->_width > w) {
      // If the multi-column's width > the total width of the acrossed columns,
      // add an extra-space to each column
      matW += boxarr[r][c]->_width - w;
      const float extraW = (boxarr[r][c]->_width - w) / n;
      for (int k = c; k < c + n; k++) colWidth[k] += extraW;
    }
  }

  // Add separator's space to the matrix width
  for (int j = 0; j < cols + 1; j++) {
    matW += Hsep[j];
    auto it = _vlines.find(j);
    if (it != _vlines.end()) matW += it->second->getWidth(env);
  }

  auto Vsep = _vsep_in.createBox(env);
  // Recalculate the height of the row
  recalculateLine(rows, boxarr, listMultiRow, lineHeight.data(), lineDepth.data(), drt, Vsep->_height);

  auto vb = sptrOf<VBox>();
  float totalHeight = 0;
  float Vspace = Vsep->_height / 2;

  for (int i = 0; i < rows; i++) {
    auto hb = sptrOf<HBox>();
    const auto& cells = _matrix->_array[i];
    for (int j = 0; j < cols; j++) {
      // The box type alone does not tell which atom produced this cell.
      // recalculateLine() moves boxes between rows to lay out \multirow
      // spans while the atoms stay where they were parsed, and a row shorter
      // than the widest one has no atom at every column at all. Both cases
      // used to be treated as guarantees, so a moved multi-column or rule box
      // reinterpreted an unrelated atom -- \begin{array}{cc}\multicolumn{2}
      // {c}{y}\\\multirow{-2}{*}{x}&b\end{array} read a \multirow atom as a
      // MulticolumnAtom. Resolve the atom once, and fall back to the plain
      // cell path whenever it does not match the type the box claims.
      const auto cell = (j < (int) cells.size()) ? cells[j] : nullptr;
      auto type = boxarr[i][j]->_type;
      auto* mca = (type == AtomType::multiColumn)
                  ? dynamic_cast<MulticolumnAtom*>(cell.get())
                  : nullptr;
      auto* hla = (type == AtomType::hline)
                  ? dynamic_cast<HlineAtom*>(cell.get())
                  : nullptr;
      if ((type == AtomType::multiColumn && mca == nullptr)
          || (type == AtomType::hline && hla == nullptr)) {
        type = AtomType::none;
      }

      switch (type) {
        case AtomType::none:
        case AtomType::multiColumn: {
          if (j == 0) {
            auto it = _vlines.find(0);
            if (it != _vlines.end()) {
              auto vat = it->second;
              vat->_height = lineHeight[i] + lineDepth[i] + Vsep->_height;
              vat->_shift = lineDepth[i] + Vspace;
              auto vatBox = vat->createBox(env);
              hb->add(vatBox);
            }
          }

          bool isLastVline = true;

          sptr<WrapperBox> wb;
          int tj = j;
          float l = j == 0 ? Hsep[j] : Hsep[j] / 2;
          if (mca == nullptr) {
            wb = sptrOf<WrapperBox>(
              boxarr[i][j], colWidth[j], lineHeight[i], lineDepth[i], _position[j]  //
            );
          } else {
            auto b = generateMulticolumn(env, boxarr[i][j], Hsep.data(), colWidth.data(), mca, j);
            j += multicolumnSpan(mca, j) - 1;
            wb = sptrOf<WrapperBox>(b, b->_width, lineHeight[i], lineDepth[i], Alignment::left);
            isLastVline = mca->hasRightVline();
          }
          float r = j == cols - 1 ? Hsep[j + 1] : Hsep[j + 1] / 2;
          wb->addInsets(l, Vspace, r, Vspace);
          applyCell(*wb, i, j);
          boxarr[i][tj] = wb;
          hb->add(wb);

          auto it = _vlines.find(j + 1);
          if (isLastVline && it != _vlines.end()) {
            auto vat = it->second;
            vat->_height = lineHeight[i] + lineDepth[i] + Vsep->_height;
            vat->_shift = lineDepth[i] + Vspace;
            auto vatBox = vat->createBox(env);
            hb->add(vatBox);
          }
        }
          break;

        case AtomType::interText: {
          float f = env.getTextWidth();
          f = f == POS_INF ? colWidth[j] : f;
          hb = sptrOf<HBox>(boxarr[i][j], f, Alignment::left);
          j = cols;
        }
          break;

        case AtomType::hline: {
          hla->setColor(LINE_COLOR);
          hla->setWidth(matW);
          if (i >= 1) {
            const auto& above = _matrix->_array[i - 1];
            if (j < (int) above.size()
                && dynamic_cast<HlineAtom*>(above[j].get()) != nullptr) {
              hb->add(sptrOf<StrutBox>(0.f, 2 * drt, 0.f, 0.f));
            }
          }

          hb->add(hla->createBox(env));
          j = cols;
        }
          break;
      }
    }

    if (boxarr[i][0]->_type != AtomType::hline) {
      hb->_height = lineHeight[i] + Vspace;
      hb->_depth = lineDepth[i] + Vspace;
    }
    vb->add(hb);
  }

  totalHeight = vb->_height + vb->_depth;

  const float axis = env.getTeXFont()->getAxisHeight(env.getStyle());
  vb->_height = totalHeight / 2 + axis;
  vb->_depth = totalHeight / 2 - axis;

  return vb;
}

/*************************************** multicolumn atoms ****************************************/

Alignment MulticolumnAtom::parseAlign(const string& str) {
  int pos = 0;
  int len = str.length();
  Alignment align = Alignment::center;
  bool first = true;
  while (pos < len) {
    char c = str[pos];
    switch (c) {
      case 'l': {
        align = Alignment::left;
        first = false;
      }
        break;
      case 'r': {
        align = Alignment::right;
        first = false;
      }
        break;
      case 'c': {
        align = Alignment::center;
        first = false;
      }
        break;
      case '|': {
        if (first) {
          _beforeVlines = 1;
        } else {
          _afterVlines = 1;
        }
        while (++pos < len) {
          c = str[pos];
          if (c != '|') {
            pos--;
            break;
          } else {
            if (first)
              _beforeVlines++;
            else
              _afterVlines++;
          }
        }
      }
        break;
    }
    pos++;
  }
  return align;
}

sptr<Box> MulticolumnAtom::createBox(Environment& env) {
  sptr<Box> b = (
    _width == 0
    ? _cols->createBox(env)
    : sptrOf<HBox>(_cols->createBox(env), _width, _align)
  );
  b->_type = AtomType::multiColumn;
  return b;
}

sptr<Box> HdotsforAtom::createBox(float space, const sptr<Box>& b, Environment& env) {
  auto sb = sptrOf<StrutBox>(0.f, space, 0.f, 0.f);
  auto vb = sptrOf<VBox>();
  vb->add(sb);
  vb->add(b);
  vb->add(sb);
  vb->_type = AtomType::multiColumn;
  return vb;
}

sptr<Box> HdotsforAtom::createBox(Environment& env) {
  auto dot = _cols->createBox(env);
  float space = Glue::getSpace(SpaceType::thinMuSkip, env) * _coeff * 2;

  // If no width specified, create a box with one dot
  if (_width == 0) return createBox(space, dot, env);

  const float x = (_width - dot->_width) / (space + dot->_width);
  // Both sides of the division are built from formula-controlled lengths
  // (the spanned column width and the coefficient-scaled gap), so x can be
  // negative, NaN, or past int's range, where casting floor(x) to int is
  // undefined. The fill loop below is bounded by the box budget anyway, so
  // saturating at INT_MAX changes no layout that could complete.
  int count = 0;
  if (x > 0.f) count = (x >= (float) INT_MAX) ? INT_MAX : (int) floor(x);

  // Only one dot can be placed in
  if (count == 0) {
    auto b = sptrOf<HBox>(dot, _width, Alignment::center);
    return createBox(space, b, env);
  }

  // Adjust the space between
  space += (x - count) * space / count;
  auto sb = sptrOf<StrutBox>(space, 0.f, 0.f, 0.f);
  auto b = sptrOf<HBox>();
  for (int i = 0; i < count; i++) {
    b->add(dot);
    b->add(sb);
  }
  b->add(dot);

  auto hb = sptrOf<HBox>(b, _width, Alignment::center);
  return createBox(space, hb, env);
}

SpaceAtom MultlineAtom::_vsep_in(UnitType::ex, 0.f, 1.f, 0.f);

sptr<Box> MultlineAtom::createBox(Environment& env) {
  float tw = env.getTextWidth();
  if (tw == POS_INF || _lineType == MultiLineType::gathered)
    return MatrixAtom(_isPartial, _column, L"").createBox(env);

  const int rows = _column->rows();
  auto vb = sptrOf<VBox>();
  auto Vsep = _vsep_in.createBox(env);
  const bool gather = (_lineType == MultiLineType::gather);
  for (int i = 0; i < rows; i++) {
    // A line can carry no atom at all: \begin{gather}\\\end{gather} parses to
    // rows whose only cell is null, and every read of that cell below used to
    // dereference it. Row lengths are not guaranteed to be one either, so take
    // the cell only when it is really there and render an empty line if not.
    const auto& row = _column->_array[i];
    const auto atom = row.empty() ? nullptr : row[0];

    auto alignment = Alignment::center;
    if (i == 0) {
      alignment = gather ? Alignment::center : Alignment::left;
    } else if (i == rows - 1) {
      alignment = gather ? Alignment::center : Alignment::right;
    }
    if (atom != nullptr && atom->_alignment != Alignment::none) {
      alignment = atom->_alignment;
    }

    if (i > 0) vb->add(Vsep);
    auto b = (atom == nullptr)
             ? sptr<Box>(sptrOf<StrutBox>(0.f, 0.f, 0.f, 0.f))
             : atom->createBox(env);
    vb->add(sptrOf<HBox>(b, tw, alignment));
  }

  float h = vb->_height + vb->_depth;
  vb->_height = h / 2;
  vb->_depth = h / 2;

  return vb;
}
