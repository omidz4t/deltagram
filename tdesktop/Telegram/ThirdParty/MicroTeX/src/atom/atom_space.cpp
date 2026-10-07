#include "atom/atom_space.h"
#include "core/glue.h"
#include "core/core.h"

#include <cmath>

using namespace std;
using namespace tex;

inline UnitType SpaceAtom::getUnit(const std::string& unit) {
  auto i = binIndexOf(
    _unitsCount,
    [&](int i) { return strcmp(unit.c_str(), _units[i].first); }
  );
  if (i < 0) return UnitType::pixel;
  return _units[i].second;
}

sptr<Box> SpaceAtom::createBox(Environment& env) {
  if (!_blankSpace) {
    float w = _width * getFactor(_wUnit, env);
    float h = _height * getFactor(_hUnit, env);
    float d = _depth * getFactor(_dUnit, env);
    return sptrOf<StrutBox>(w, h, d, 0.f);
  }
  if (_blankType == SpaceType::none) return sptrOf<StrutBox>(env.getSpace(), 0.f, 0.f, 0.f);
  return Glue::get(_blankType, env);
}

pair<UnitType, float> SpaceAtom::getLength(const string& lgth) {
  if (lgth.empty()) return {UnitType::pixel, 0.f};

  size_t i = 0;
  for (; i < lgth.size() && !isAsciiAlpha(static_cast<unsigned char>(lgth[i])); i++);
  float f = 0;
  valueof(lgth.substr(0, i), f);
  // A literal too large for a float leaves infinity here, which then spreads
  // through every box built from this length: layout loops that advance by a
  // fraction of it never terminate, and the final cast of the render size to
  // an integer is undefined. Every explicit length in a formula -- \rule,
  // \hspace, \raisebox and the rest -- is parsed here, so clearing it once at
  // this point keeps the whole box tree finite.
  if (!std::isfinite(f)) f = 0.f;

  UnitType unit = UnitType::pixel;
  string x = lgth.substr(i);
  tolower(x);
  if (i != lgth.size()) unit = getUnit(x);

  return {unit, f};
}

pair<UnitType, float> SpaceAtom::getLength(const wstring& lgth) {
  const string s = wide2utf8(lgth);
  return getLength(s);
}
