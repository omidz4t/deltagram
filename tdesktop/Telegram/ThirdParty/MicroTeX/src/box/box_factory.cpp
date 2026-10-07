#include "common.h"
#include "box/box_factory.h"
#include "atom/atom_basic.h"
#include "core/core.h"
#include "utils/utils.h"

#include <cmath>

using namespace std;
using namespace tex;

sptr<Box> DelimiterFactory::create(SymbolAtom& symbol, Environment& env, int size) {
  if (size > 4) return symbol.createBox(env);

  TeXFont& tf = *(env.getTeXFont());
  const TexStyle style = env.getStyle();
  Char c = tf.getChar(symbol.getName(), style);
  const float baseTotal = c.getHeight() + c.getDepth();

  // One counter for both the loop and the fallback test: with a shadowed
  // inner counter the test always saw 0, so the extension-assembly path was
  // taken whenever the ladder simply ran out early.
  int i = 1;
  for (; i <= size && tf.hasNextLarger(c); i++) c = tf.getNextLarger(c, style);

  // For extensible delimiters (vert, Vert, the arrows) the first next-larger
  // rung in the font tables is the repeatable segment glyph, which is
  // *smaller* than the display-style base char, so a plain ladder walk would
  // hand \big| a bar shorter than a bare |. Route those degenerate walks to
  // the height-targeted factory, like a ladder that ran out early.
  const bool walkedSmaller = (c.getHeight() + c.getDepth()) < baseTotal;
  if ((i <= size && !tf.hasNextLarger(c)) || walkedSmaller) {
    CharBox A(tf.getChar(L'A', "mathnormal", style));
    auto b = create(symbol.getName(), env, size * (A._height + A._depth));
    return b;
  }

  return sptrOf<CharBox>(c);
}

sptr<Box> DelimiterFactory::create(const string& symbol, Environment& env, float minHeight) {
  TeXFont& tf = *(env.getTeXFont());
  const TexStyle style = env.getStyle();
  Char c = tf.getChar(symbol, style);

  // start with smallest character
  float total = c.getHeight() + c.getDepth();

  // try larger versions of the same char until min-height has been reached
  while (total < minHeight && tf.hasNextLarger(c)) {
    c = tf.getNextLarger(c, style);
    total = c.getHeight() + c.getDepth();
  }
  // tall enough char found
  if (total >= minHeight) {
    /**if (total > minHeight) {
            auto cb = sptrOf<CharBox>(c);
            float scale = minHeight / total;
            return sptrOf<ScaleBox>(cb, scale);
        }*/
    return sptrOf<CharBox>(c);
  } else if (tf.isExtensionChar(c)) {
    // Owning from the start: the repeat loop below appends boxes, which is
    // bounded by the box budget, so it can throw -- and a raw ext or vBox
    // would leak (with the whole assembled delimiter) on that path.
    std::unique_ptr<Extension> ext(tf.getExtension(c, style));
    // No extension recipe after all, fall through to the tallest char.
    if (ext == nullptr) return sptrOf<CharBox>(c);

    // construct vertical box
    auto vBox = sptrOf<VBox>();

    // insert top part
    if (ext->hasTop()) {
      c = ext->getTop();
      vBox->add(sptrOf<CharBox>(c));
    }

    if (ext->hasMiddle()) {
      c = ext->getMiddle();
      vBox->add(sptrOf<CharBox>(c));
    }

    if (ext->hasBottom()) {
      c = ext->getBottom();
      vBox->add(sptrOf<CharBox>(c));
    }

    // insert repeatable part until tall enough
    if (ext->hasRepeat()) {
      c = ext->getRepeat();
      auto rep = sptrOf<CharBox>(c);
      // Each add() grows the box by exactly the repeat's height plus depth,
      // so a repeat that measures zero would keep the loop going forever.
      // The piece count is capped as well: minHeight is derived from the
      // enclosed content and from the global math sizes, so an absurd or
      // non-finite value must not turn this into an unbounded allocation
      // loop on whatever thread is laying the formula out.
      const int maxPieces = 4096;
      int pieces = 0;
      if (rep->_height + rep->_depth > 0) {
        while (vBox->_height + vBox->_depth <= minHeight && pieces++ < maxPieces) {
          if (ext->hasTop() && ext->hasBottom()) {
            vBox->add(1, rep);
            if (ext->hasMiddle()) {
              vBox->add(vBox->size() - 1, rep);
            }
          } else if (ext->hasBottom()) {
            vBox->add(0, rep);
          } else {
            vBox->add(rep);
          }
        }
      }
    }
    return vBox;
  }
  // no extensions, so return the tallest possible character
  return sptrOf<CharBox>(c);
}

namespace {

/**
 * An arrow shaft is built by repeating one glyph until it spans the requested
 * width, so both the width and the per-piece step come straight from the
 * formula: an out-of-range length parses to infinity, \scalebox multiplies a
 * box width by an arbitrary factor, and a font could in principle make the
 * kern wider than the glyph it follows. Any of those turns the repeat loop
 * into an unbounded allocation loop on whatever thread lays the formula out,
 * so the shaft is limited to a piece count no real formula reaches, and the
 * requested width is shrunk to what those pieces cover -- that also keeps the
 * scale factor of the trailing partial glyph sane.
 */
constexpr int kMaxArrowPieces = 4096;

float boundedArrowWidth(float width, float base, float piece) {
  if (!std::isfinite(base) || !std::isfinite(piece) || piece <= 0.f) {
    return base;
  }
  const float bounded = base + piece * kMaxArrowPieces;
  if (!std::isfinite(bounded)) {
    return base;
  }
  return (width < bounded) ? width : bounded;
}

}

sptr<Atom> XLeftRightArrowFactory::MINUS;
sptr<Atom> XLeftRightArrowFactory::LEFT;
sptr<Atom> XLeftRightArrowFactory::RIGHT;

sptr<Box> XLeftRightArrowFactory::create(Environment& env, float width) {
  // initialize
  if (MINUS == nullptr) {
    MINUS = SymbolAtom::get("minus");
    LEFT = SymbolAtom::get("leftarrow");
    RIGHT = SymbolAtom::get("rightarrow");
  }
  sptr<Box> left = LEFT->createBox(env);
  sptr<Box> right = RIGHT->createBox(env);
  float swidth = left->_width + right->_width;

  if (width < swidth) {
    auto hb = sptrOf<HBox>(left);
    hb->add(sptrOf<StrutBox>(-min(swidth - width, left->_width), 0.f, 0.f, 0.f));
    hb->add(right);
    return hb;
  }

  sptr<Box> minu = SmashedAtom(MINUS, "").createBox(env);
  sptr<Box> kern = SpaceAtom(UnitType::mu, -3.4f, 0.f, 0.f).createBox(env);

  float mwidth = minu->_width + kern->_width;
  swidth += 2 * kern->_width;
  width = boundedArrowWidth(width, swidth, mwidth);

  auto hb = sptrOf<HBox>();
  float w = 0.f;
  if (mwidth > 0.f) {
    for (w = 0; w < width - swidth - mwidth; w += mwidth) {
      hb->add(minu);
      hb->add(kern);
    }
  }

  hb->add(sptrOf<ScaleBox>(minu, (width - swidth - w) / minu->_width, 1.f));

  hb->add(0, kern);
  hb->add(0, left);
  hb->add(kern);
  hb->add(right);

  return hb;
}

sptr<Box> XLeftRightArrowFactory::create(bool left, Environment& env, float width) {
  // initialize
  if (MINUS == nullptr) {
    MINUS = SymbolAtom::get("minus");
    LEFT = SymbolAtom::get("leftarrow");
    RIGHT = SymbolAtom::get("rightarrow");
  }
  auto arr = left ? LEFT->createBox(env) : RIGHT->createBox(env);
  float h = arr->_height;
  float d = arr->_depth;

  float swidth = arr->_width;
  if (width <= swidth) {
    arr->_depth = d / 2;
    return arr;
  }

  sptr<Box> minu = SmashedAtom(MINUS, "").createBox(env);
  sptr<Box> kern = SpaceAtom(UnitType::mu, -4.f, 0, 0).createBox(env);
  float mwidth = minu->_width + kern->_width;
  swidth += kern->_width;
  width = boundedArrowWidth(width, swidth, mwidth);

  auto hb = sptrOf<HBox>();
  float w = 0.f;
  if (mwidth > 0.f) {
    for (w = 0; w < width - swidth - mwidth; w += mwidth) {
      hb->add(minu);
      hb->add(kern);
    }
  }

  float sf = (width - swidth - w) / minu->_width;

  hb->add(SpaceAtom(UnitType::mu, -2.f * sf, 0, 0).createBox(env));
  hb->add(ScaleAtom(MINUS, sf, 1).createBox(env));

  if (left) {
    hb->add(0, SpaceAtom(UnitType::mu, -3.5f, 0, 0).createBox(env));
    hb->add(0, arr);
  } else {
    hb->add(SpaceAtom(UnitType::mu, -2.f * sf - 2.f, 0, 0).createBox(env));
    hb->add(arr);
  }

  hb->_depth = d / 2;
  hb->_height = h;

  return hb;
}
