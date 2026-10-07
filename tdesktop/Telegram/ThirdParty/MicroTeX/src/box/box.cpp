#include "box/box.h"
#include "fonts/fonts.h"

using namespace tex;
using namespace std;

namespace tex {
namespace {

// Measured over a ~6000-formula corpus of real LaTeX: median 34 boxes, 99th
// percentile 606, largest 1845. Runaway formulas are nowhere near that -- a
// chain of 40 brace-less \binom builds 195k boxes and takes seconds. 100k
// leaves more than fifty times the headroom of the largest real formula while
// still cutting the runaway cases off early. Anything approaching this limit
// renders far larger than a caller will accept anyway.
constexpr int kMaxBoxBudget = 100000;
thread_local int gBoxBudgetUsed = 0;

}  // namespace

void resetBoxBudget() {
  gBoxBudgetUsed = 0;
}

int usedBoxBudget() {
  return gBoxBudgetUsed;
}

void countBoxAllocation() {
  // Deliberately not reset here: parsing swallows ex_parse in several places
  // (partial mode, per-argument sub-formulas), and a formula that kept
  // restarting its own budget would not be bounded at all. The count stays
  // spent until the next parse resets it, so once a formula is over budget
  // every further allocation keeps failing.
  if (++gBoxBudgetUsed > kMaxBoxBudget) {
    throw ex_parse("Formula is too large to lay out!");
  }
}

}  // namespace tex

bool Box::DEBUG = false;

void Box::copyMetrics(const sptr<Box>& box) {
  _width = box->_width;
  _height = box->_height;
  _depth = box->_depth;
  _shift = box->_shift;
}

int Box::lastFontId() {
  return TeXFont::NO_FONT;
}

int BoxGroup::lastFontId() {
  int id = TeXFont::NO_FONT;
  for (int i = _children.size() - 1; i >= 0 && id == TeXFont::NO_FONT; i--) {
    id = _children[i]->lastFontId();
  }
  return id;
}

int DecorBox::lastFontId() {
  return _base->lastFontId();
}
