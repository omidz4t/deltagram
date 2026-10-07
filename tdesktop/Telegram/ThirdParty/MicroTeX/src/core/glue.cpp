#include "core/core.h"
#include "glue.h"

using namespace std;
using namespace tex;

const Glue Glue::_glueTypes[]{
  {0, 0, 0},
  {3, 0, 0},
  {4, 4, 2},
  {5, 0, 5},
};

/*
 GLUE TABLE
 Page 181 in [The TeXBook]
 -------------------------------------------------------
       ORD   OP    BIN   REL   OPEN  CLOSE  PUNCT  INNER
 ORD    0     1    (2)   (3)    0     0      0     (1)
 OP     1     1     *    (3)    0     0      0     (1)
 BIN   (2)   (2)    *     *    (2)    *      *     (2)
 REL   (3)   (3)    *     0    (3)    0      0     (3)
 OPEN  (0)    0     *     0     0     0      0      0
 CLOSE (0)    1    (2)   (3)    0     0      0     (1)
 PUNCT (1)   (1)    *    (1)   (1)   (1)    (1)    (1)
 INNER (1)    1    (2)   (3)   (1)    0     (1)    (1)

 0: no space
 1: thin space
 2: medium space
 3: thick space

 The table entry is parenthesized if the space is to be inserted only in
 display and text styles, not in script and scriptscript styles.

 Some of the entries in the table are ‘*’; such cases never arise, because
 Bin atoms must be preceded and followed by atoms compatible with the
 nature of binary operations.
*/
const char Glue::_table[TYPE_COUNT][TYPE_COUNT][STYLE_COUNT]{
  {"0000", "1111", "2200", "3300", "0000", "0000", "0000", "1100"},
  {"1111", "1111", "0000", "3300", "0000", "0000", "0000", "1100"},
  {"2200", "2200", "0000", "0000", "2200", "0000", "0000", "2200"},
  {"3300", "3300", "0000", "0000", "3300", "0000", "0000", "3300"},
  {"0000", "0000", "0000", "0000", "0000", "0000", "0000", "0000"},
  {"0000", "1111", "2200", "3300", "0000", "0000", "0000", "1100"},
  {"1100", "1100", "0000", "1100", "1100", "1100", "1100", "1100"},
  {"1100", "1111", "2200", "3300", "1100", "0000", "1100", "1100"},
};

float Glue::getFactor(const Environment& env) {
  const auto& tf = env.getTeXFont();
  // use "quad" from a font marked as an "mu font"
  float quad = tf->getQuad(env.getStyle(), tf->getMuFontId());
  return quad / 18.f;
}

sptr<Box> Glue::createBox(const Environment& env) const {
  float factor = getFactor(env);
  return sptrOf<GlueBox>(_space * factor, _stretch * factor, _shrink * factor);
}

// Both lookups below are indexed by values that originate outside this file
// -- the style comes from the environment, which a formula can set (\genfrac
// picks it directly), and the table entry it selects then indexes _glueTypes.
// Clamping here keeps a bad value from reading past either array even if a
// caller lets one through.
int Glue::indexOf(AtomType ltype, AtomType rtype, const Environment& env) {
  // types > INNER are considered of type ORD for glue calculations, and so is
  // anything below ORD. Only clamping from above left AtomType::none (= -1)
  // to pass through, and static_cast<u8> then turns it into 255 -- an index
  // thousands of bytes past _table, the same shape as the unit-conversion
  // lookup that indexed at -1. No atom reports `none` today, but nothing
  // states that invariant and Box::_type defaults to it, so bound the index
  // where the lookup happens rather than relying on every caller.
  const auto clamp = [](AtomType t) {
    return (t > AtomType::inner || t < AtomType::ordinary) ? AtomType::ordinary : t;
  };
  AtomType l = clamp(ltype);
  AtomType r = clamp(rtype);
  const int style = static_cast<i8>(env.getStyle()) / 2;
  const int k = (style < 0) ? 0 : (style >= STYLE_COUNT ? STYLE_COUNT - 1 : style);
  return _table[static_cast<u8>(l)][static_cast<u8>(r)][k] - '0';
}

sptr<Box> Glue::get(AtomType ltype, AtomType rtype, const Environment& env) {
  const int i = indexOf(ltype, rtype, env);
  if (i < 0 || i >= GLUE_TYPE_COUNT) return sptrOf<GlueBox>(0.f, 0.f, 0.f);
  return _glueTypes[i].createBox(env);
}

const Glue& Glue::getGlue(SpaceType skipType) {
  const i8 i = static_cast<i8>(skipType);
  return _glueTypes[i < 0 ? -i : i];
}

sptr<Box> Glue::get(SpaceType skipType, const Environment& env) {
  const Glue& glue = getGlue(skipType);
  auto b = glue.createBox(env);
  if (static_cast<i8>(skipType) < 0) b->negWidth();
  return b;
}

float Glue::getSpace(AtomType ltype, AtomType rtype, const Environment& env) {
  const int i = indexOf(ltype, rtype, env);
  // Same guard the box-building overload above already has: the table entry
  // is data, so a bad one must not index _glueTypes.
  if (i < 0 || i >= GLUE_TYPE_COUNT) return 0.f;
  const Glue& glueType = _glueTypes[i];
  return glueType._space * glueType.getFactor(env);
}

float Glue::getSpace(SpaceType skipType, const Environment& env) {
  const Glue& glue = getGlue(skipType);
  const auto v = glue._space * glue.getFactor(env);
  return static_cast<i8>(skipType) < 0 ? -v : v;
}
