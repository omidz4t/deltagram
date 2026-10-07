#include "render.h"

#include "atom/atom.h"
#include "core/core.h"
#include "core/formula.h"

#include <climits>
#include <cmath>

using namespace std;
using namespace tex;

namespace {

// Casting a float that is NaN or outside the range of int is undefined, and
// so is adding two such sizes together. The box dimensions reaching here are
// built from formula-supplied lengths and scale factors, so both are
// reachable from a message. Saturating lets the caller see an honestly huge
// size and reject the formula, rather than a wrapped-around negative one that
// looks like a valid small size.
int rasterBound(float value) {
  const auto bound = std::ceil(value);
  if (!(bound > float(INT_MIN))) return INT_MIN;
  if (!(bound < float(INT_MAX))) return INT_MAX;
  return static_cast<int>(bound);
}

int rasterSum(int a, int b) {
  if (b > 0 && a > INT_MAX - b) return INT_MAX;
  if (b < 0 && a < INT_MIN - b) return INT_MIN;
  return a + b;
}

}

const color TeXRender::_defaultcolor = black;
float TeXRender::_defaultSize = -1;
float TeXRender::_magFactor = 0;

TeXRender::TeXRender(const sptr<Box>& box, float textSize, bool trueValues) {
  _box = box;
  if (_defaultSize != -1) _textSize = _defaultSize;
  if (_magFactor != 0) {
    _textSize = textSize * std::abs(_magFactor);
  } else {
    _textSize = textSize;
  }
  // rasterBound: textSize reaches here scaled by \magnification or
  // \DeclareMathSizes from the formula, so the cast can see a value outside
  // int range (same class the helper was added for).
  if (!trueValues) _insets += rasterBound(0.18f * textSize);
  if (Box::DEBUG) {
    const auto group = wrap(box);
    _box = group;
    BoxFilter filter = [](auto b) {
      return dynamic_cast<CharBox*>(b.get()) != nullptr;
    };
    buildDebug(nullptr, group, std::move(filter));
  }
}

sptr<BoxGroup> TeXRender::wrap(const sptr<Box>& box) {
  sptr<BoxGroup> parent;
  if (auto group = dynamic_pointer_cast<BoxGroup>(box); group != nullptr) {
    parent = group;
  } else {
    parent = sptrOf<HBox>(box);
  }
  return parent;
}

void TeXRender::buildDebug(
  const sptr<BoxGroup>& parent,
  const sptr<Box>& box,
  BoxFilter&& filter
) {
  if (parent != nullptr) {
    if (box->isSpace()) {
      parent->addOnly(box);
    } else if (filter(box)) {
      parent->addOnly(sptrOf<DebugBox>(box));
    } else {
      // placeholder to consume the space of the current box
      parent->addOnly(sptrOf<StrutBox>(box));
    }
  }
  if (auto group = dynamic_pointer_cast<BoxGroup>(box); group != nullptr) {
    const auto kern = sptrOf<StrutBox>(-group->_width, -group->_height, -group->_depth, -group->_shift);
    // snapshot of current children
    const auto children = group->descendants();
    group->addOnly(kern);
    for (const auto& child: children) {
      buildDebug(group, child, std::forward<BoxFilter>(filter));
    }
  } else if (auto decor = dynamic_pointer_cast<DecorBox>(box); decor != nullptr) {
    const auto g = wrap(decor->_base);
    decor->_base = g;
    buildDebug(nullptr, g, std::forward<BoxFilter>(filter));
  }
}

float TeXRender::getTextSize() const {
  return _textSize;
}

int TeXRender::getHeight() const {
  return rasterSum(
    rasterBound(_box->_height * _textSize + _insets.top),
    getDepth()
  );
}

int TeXRender::getDepth() const {
  return rasterSum(
    rasterBound(_box->_depth * _textSize + _insets.bottom),
    1
  );
}

int TeXRender::getWidth() const {
  return rasterSum(
    rasterBound(_box->_width * _textSize + _insets.left + _insets.right),
    1
  );
}

float TeXRender::getBaseline() const {
  // double: getHeight()/getDepth() saturate at the int extremes for absurd
  // formulas, so their difference does not necessarily fit in an int.
  const auto height = getHeight();
  return height ? float((double(height) - getDepth()) / double(height)) : 0.f;
}

void TeXRender::setTextSize(float textSize) {
  _textSize = textSize;
}

void TeXRender::setForeground(color fg) {
  _fg = fg;
}

Insets TeXRender::getInsets() {
  return _insets;
}

void TeXRender::setInsets(const Insets& insets, bool trueval) {
  _insets = insets;
  if (!trueval) _insets += rasterBound(0.18f * _textSize);
}

void TeXRender::setWidth(int width, Alignment align) {
  float diff = width - getWidth();
  // FIXME
  // only care if new width larger than old
  if (diff > 0) {
    _box = sptrOf<HBox>(_box, (float) width, align);
  }
}

void TeXRender::setHeight(int height, Alignment align) {
  float diff = height - getHeight();
  // FIXME
  // only care if new height larger than old
  if (diff > 0) {
    _box = sptrOf<VBox>(_box, diff, align);
  }
}

void TeXRender::draw(Graphics2D& g2, int x, int y) {
  const color old = g2.getColor();
  // Restored on every exit, including a throw out of the box tree: the
  // scaled transform must not leak into the caller's painter.
  struct G2Restore {
    Graphics2D& g2;
    color old;
    ~G2Restore() {
      g2.reset();
      g2.setColor(old);
    }
  } restore{g2, old};

  g2.scale(_textSize, _textSize);
  if (!isTransparent(_fg)) {
    g2.setColor(_fg);
  } else {
    g2.setColor(_defaultcolor);
  }

  // draw formula box
  _box->draw(g2, (x + _insets.left) / _textSize, (y + _insets.top) / _textSize + _box->_height);
}

DefaultTeXFont* TeXRenderBuilder::createFont(float size, int type) {
  DefaultTeXFont* tf = new DefaultTeXFont(size);
  if (type == 0) tf->setSs(false);
  if ((type & ROMAN) != 0) tf->setRoman(true);
  if ((type & TYPEWRITER) != 0) tf->setTt(true);
  if ((type & SANSSERIF) != 0) tf->setSs(true);
  if ((type & ITALIC) != 0) tf->setIt(true);
  if ((type & BOLD) != 0) tf->setBold(true);
  return tf;
}

TeXRender* TeXRenderBuilder::build(Formula& f) {
  return build(f._root);
}

TeXRender* TeXRenderBuilder::build(const sptr<Atom>& fc) {
  sptr<Atom> f = fc;
  if (f == nullptr) f = sptrOf<EmptyAtom>();
  if (_textSize == -1) {
    throw ex_invalid_state("A size is required, call function setSize before build.");
  }

  DefaultTeXFont* font = (
    _type == -1
    ? new DefaultTeXFont(_textSize)
    : createFont(_textSize, _type)
  );
  sptr<TeXFont> tf(font);
  // Owning from the start: createBox below throws (the box budget is the
  // reachable case), and a raw env would leak on that path.
  std::unique_ptr<Environment> env;
  if (_widthUnit != UnitType::none && _textWidth != 0) {
    env = std::make_unique<Environment>(_style, tf, _widthUnit, _textWidth);
  } else {
    env = std::make_unique<Environment>(_style, tf);
  }

  if (_lineSpaceUnit != UnitType::none) {
    env->setInterline(_lineSpaceUnit, _lineSpace);
  }

  auto box = f->createBox(*env);
  TeXRender* render;
  if (_widthUnit != UnitType::none && _textWidth != 0) {
    HBox* hb;
    if (_lineSpaceUnit != UnitType::none && _lineSpace != 0) {
      float space = _lineSpace * SpaceAtom::getFactor(_lineSpaceUnit, *env);
      auto split = BoxSplitter::split(box, env->getTextWidth(), space);
      hb = new HBox(split, _isMaxWidth ? split->_width : env->getTextWidth(), _align);
    } else {
      hb = new HBox(box, _isMaxWidth ? box->_width : env->getTextWidth(), _align);
    }
    render = new TeXRender(sptr<Box>(hb), _textSize, _trueValues);
  } else {
    render = new TeXRender(box, _textSize, _trueValues);
  }

  if (!isTransparent(_fg)) render->setForeground(_fg);

  return render;
}
