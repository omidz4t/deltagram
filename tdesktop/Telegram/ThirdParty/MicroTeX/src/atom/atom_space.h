#ifndef LATEX_ATOM_SPACE_H
#define LATEX_ATOM_SPACE_H

#include "atom/atom.h"
#include "box/box_group.h"
#include "utils/utils.h"

namespace tex {

/**
 * An atom representing whitespace. The dimension values can be set using different
 * unit types.
 */
class SpaceAtom : public Atom {
private:
  static const std::pair<const char*, UnitType> _units[];
  static const i32 _unitsCount;
  static constexpr i32 _unitConversionsCount = static_cast<i32>(UnitType::x8) + 1;
  static const std::function<float(const Environment&)> _unitConversions[_unitConversionsCount];

  // whether a hard space should be represented
  bool _blankSpace = false;
  // thin-mu-skip, med-mu-skip, thick-mu-skip
  SpaceType _blankType{};
  // dimensions
  float _width = 0, _height = 0, _depth = 0;
  // units of the dimensions
  UnitType _wUnit{}, _hUnit{}, _dUnit{};

public:
  SpaceAtom() noexcept: _blankSpace(true) {}

  explicit SpaceAtom(SpaceType type) noexcept
    : _blankSpace(true), _blankType(type) {}

  SpaceAtom(UnitType unit, float width, float height, float depth) noexcept
    : _wUnit(unit), _hUnit(unit), _dUnit(unit), _width(width), _height(height), _depth(depth) {}

  SpaceAtom(UnitType wu, float w, UnitType hu, float h, UnitType du, float d) noexcept
    : _wUnit(wu), _hUnit(hu), _dUnit(du), _width(w), _height(h), _depth(d) {}

  static UnitType getUnit(const std::string& unit);

  /** Get the scale factor from the given unit and environment */
  inline static float getFactor(UnitType unit, const Environment& env) {
    // The table holds one conversion per named unit, but UnitType::none is -1
    // and a unit cast in from outside the enum can be any i8, so indexing by
    // the raw value reads before or past the table and then calls whatever
    // bytes happen to be there as a std::function -- the fault lands at that
    // call, far from the unit that caused it. \above, \abovewithdelims and
    // \kern already reject a missing dimension while parsing; this is the
    // backstop under them for every other route a unit takes into a SpaceAtom.
    // A unit that names no conversion contributes no space, which is what
    // \raisebox already does for none.
    const i32 index = static_cast<i8>(unit);
    if (index < 0 || index >= _unitConversionsCount) return 0.f;
    return _unitConversions[index](env);
  }

  inline static float getSize(UnitType unit, float size, const Environment& env) {
    return getFactor(unit, env) * size;
  }

  sptr<Box> createBox(Environment& env) override;

  /**
   * Get the unit and length from given string. The string must be in the format: a number
   * following with the unit (e.g. 10px, 1cm, 8.2em, ...) or (UnitType::pixel, 0) will be returned.
   */
  static std::pair<UnitType, float> getLength(const std::string& lgth);

  /**
   * Get the unit and length from given string. The string must be in the format: a number
   * following with the unit (e.g. 10px, 1cm, 8.2em, ...) or (UnitType::pixel, 0) will be returned.
   */
  static std::pair<UnitType, float> getLength(const std::wstring& lgth);

  __decl_clone(SpaceAtom)
};

}

#endif //LATEX_ATOM_SPACE_H
