#ifndef ATOM_H_INCLUDED
#define ATOM_H_INCLUDED

#undef DEBUG

#include <list>

#include "box/box.h"

namespace tex {

/**
 * An abstract superclass for all logical mathematical constructions that can be
 * a part of a Formula. All subclasses must implement the abstract
 * Atom#createBox(Environment) method that transforms this logical unit
 * into a concrete box (that can be painted). They also must define their type,
 * used for determining what glue to use between adjacent atoms in a
 * "row construction". That can be one single type by assigning one of the type
 * constants to the #_type field. But they can also be defined as having
 * two types: a "left type" and a "right type". This can be done by implementing
 * the methods Atom#leftType() and Atom#rightType(). The left type
 * will then be used for determining the glue between this atom and the previous
 * one (in a row, if any) and the right type for the glue between this atom and
 * the following one (in a row, if any).
 */
class Atom {
public:
  /** The type of the atom (default value: ordinary atom) */
  AtomType _type = AtomType::ordinary;
  /** The limits type of the atom (default value: nolimits) */
  LimitsType _limitsType = LimitsType::noLimits;
  /** The alignment type of the atom (default value: none) */
  Alignment _alignment = Alignment::none;

  /**
   * How many wrapper atoms deep this atom sits.
   *
   * Some atoms attach themselves to whatever atom precedes them -- scripts and
   * big-operator limits do -- so a flat run of them ("x^1^1^1", "\sum^1^1^1")
   * builds an atom chain as long as the run, without ever nesting the parser.
   * The parse depth limit therefore never sees it, while createBox() walks the
   * chain recursively and overflows the stack, which no caller can catch.
   * Wrappers that can chain call inheritWrapDepth() so the chain is bounded
   * wherever it is built, and across differing wrapper types.
   */
  int _wrapDepth = 0;

  Atom() = default;

  /** Adopt the base atom's chain depth, refusing to extend it too far. */
  void inheritWrapDepth(const sptr<Atom>& base);

  /**
   * Get the type of the leftermost child atom. Most atoms have no child
   * atoms, so the "left type" and the "right type" are the same: the atom's
   * type. This also is the default implementation. But Some atoms are
   * composed of child atoms put one after another in a horizontal row. These
   * atoms must override this method.
   *
   * @return the type of the leftermost child atom
   */
  virtual AtomType leftType() const { return _type; }

  /**
   * Get the type of the rightermost child atom. Most atoms have no child
   * atoms, so the "left type" and the "right type" are the same: the atom's
   * type. This also is the default implementation. But Some atoms are
   * composed of child atoms put one after another in a horizontal row. These
   * atoms must override this method.
   *
   * @return the type of the rightermost child atom
   */
  virtual AtomType rightType() const { return _type; }

  /**
   * Convert this atom into a Box, using properties set by "parent"
   * atoms, like the TeX style, the last used font, color settings, ...
   *
   * @param env the current environment settings
   *
   * @return the resulting box.
   */
  virtual sptr<Box> createBox(Environment& env) = 0;

  /** Shallow clone a atom from this atom. */
  virtual sptr<Atom> clone() const = 0;

  virtual ~Atom() = default;

#ifndef __decl_clone
#define __decl_clone(type) \
  virtual sptr<Atom> clone() const override { return sptr<Atom>(new type(*this)); }
#endif
};

/**
 * Take a copy of an atom before overriding one of its properties.
 *
 * Atoms are not always private to the formula holding them: SymbolAtom::get()
 * and Formula::get() hand out entries of process-wide caches that outlive
 * every formula. Writing a type, an alignment or a limits mode straight into
 * such an atom therefore changes how every later formula in the process
 * renders -- "x{+}y" turned the shared "+" into an ordinary atom for good, and
 * every formula parsed afterwards lost its binary-operator spacing. Since a
 * caller cannot tell a cached atom from a freshly built one, anything that
 * overrides a property works on its own copy instead.
 */
[[nodiscard]] inline sptr<Atom> privateCopy(const sptr<Atom>& atom) {
  return (atom == nullptr) ? nullptr : atom->clone();
}

}  // namespace tex

#endif  // ATOM_H_INCLUDED
