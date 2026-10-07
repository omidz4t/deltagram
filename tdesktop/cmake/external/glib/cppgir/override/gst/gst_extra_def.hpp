#ifndef _GI_GST_GST_EXTRA_DEF_HPP_
#define _GI_GST_GST_EXTRA_DEF_HPP_

#include <ios>
#include <iostream>
#include <iterator>
#include <limits>

namespace gi
{
namespace repository
{
namespace Gst
{
// connection helpers
namespace internal
{
class PadProbeConnection : public detail::connection_impl
{
public:
  // using connection_impl::connection_impl;
  PadProbeConnection(gulong id, detail::connection_status s, Pad pad)
      : connection_impl(id, s), pad_(pad)
  {}

  void disconnect() { pad_.remove_probe(id_); }

private:
  Pad pad_;
};

template<typename T, typename LimitT = T>
struct Range
{
  T min;
  T max;

  explicit constexpr Range(T _min = std::numeric_limits<LimitT>::min(),
      T _max = std::numeric_limits<LimitT>::max())
      : min(_min), max(_max)
  {}
  constexpr bool operator==(const Range &other)
  {
    return min == other.min && max == other.max;
  }
  constexpr bool operator!=(const Range &other) { return !(*this == other); }
};

template<typename T>
struct StepRange : public Range<T>
{
  T step;

  explicit constexpr StepRange(T _min, T _max, T _step = 1)
      : Range<T>(_min, _max), step(_step)
  {}
  constexpr bool operator==(const StepRange &other)
  {
    return Range<T>::operator==(*this, other) && step == other.step;
  }
  constexpr bool operator!=(const StepRange &other)
  {
    return !(*this == other);
  }
};

class ios_flags_saver
{
  std::ostream &out;
  std::ios old;

public:
  ios_flags_saver(std::ostream &_out) : out(_out), old(nullptr)
  {
    old.copyfmt(out);
  }
  ~ios_flags_saver() { out.copyfmt(old); }
};

template<typename T, typename U>
inline std::ostream &
operator<<(std::ostream &out, const Range<T, U> &v)
{
  return out << "Range(" << v.min << ", " << v.max << ')';
}

template<typename T>
inline std::ostream &
operator<<(std::ostream &out, const StepRange<T> &v)
{
  return out << "Range(" << v.min << ", " << v.max << ", " << v.step << ')';
}

} // namespace internal

using PadProbeConnection = detail::connection<internal::PadProbeConnection>;
using PadProbeScopedConnection = detail::scoped_connection<PadProbeConnection>;

// make Rank a more convenient numeric
GI_ENUM_NUMERIC(Rank)

// iterator helper
template<typename T>
class IteratorAdapter
{
  Iterator it_;

public:
  IteratorAdapter(Iterator _it) : it_(std::move(_it)) {}

  class iterator
  {
  private:
    Iterator_Ref it_;
    GObject::Value val_;
    T value_{};
    IteratorResult result_{IteratorResult::DONE_};

  public:
    // traits
    typedef T difference_type;
    typedef T value_type;
    typedef T *pointer;
    typedef T &reference;
    typedef std::input_iterator_tag category;

    iterator() {}
    iterator(Iterator_Ref _it) : it_(_it) { ++(*this); }
    // only prefix required
    iterator &operator++()
    {
      if (it_) {
        val_.unset();
        result_ = it_.next(val_);
        value_ = val_.get_value<T>();
        // clear iterator when done to ensure end() comparison
        if (result_ == IteratorResult::DONE_)
          it_ = nullptr;
      }
      return *this;
    }
    T &operator*()
    {
      if (result_ != IteratorResult::OK_ && result_ != IteratorResult::DONE_)
        detail::try_throw(std::runtime_error(
            "GstIterator error result " + std::to_string((int)result_)));
      return value_;
    }
    void operator->() { return &value_; }
    bool operator==(const iterator &other) const
    {
      return other.it_ == it_ && other.result_ == result_;
    }
    bool operator!=(const iterator &other) const { return !(*this == other); }
  };

  iterator begin() const { return iterator(it_); }
  iterator end() const { return iterator(); }
};

// GstValue helpers

struct Bitmask
{
  guint64 mask;
  constexpr Bitmask(guint64 _mask = 0) : mask(_mask) {}
  constexpr operator guint64() const { return mask; }
};

inline std::ostream &
operator<<(std::ostream &out, const Bitmask &v)
{
  internal::ios_flags_saver s(out);
  return out << std::showbase << std::hex << "Bitmask(" << (guint64)v << ')';
}

struct FlagSet
{
  guint flags;
  guint mask;
  explicit constexpr FlagSet(guint _flags, guint _mask)
      : flags(_flags), mask(_mask)
  {}
  constexpr bool operator==(const FlagSet &other)
  {
    return flags == other.flags && mask == other.mask;
  }
  constexpr bool operator!=(const FlagSet &other) { return !(*this == other); }
};

inline std::ostream &
operator<<(std::ostream &out, const FlagSet &v)
{
  internal::ios_flags_saver s(out);
  return out << std::showbase << std::hex << "FlagSet(" << v.flags << " , "
             << v.mask << ')';
}

struct Fraction
{
  gint num;
  gint den;

  constexpr Fraction(gint _num = 0, gint _den = 1) : num(_num), den(_den) {}

  constexpr bool operator==(const Fraction &other)
  {
    return num == other.num && den == other.den;
  }
  constexpr bool operator!=(const Fraction &other) { return !(*this == other); }

  Fraction operator*(const Fraction &other)
  {
    Fraction result;
    if (gst_util_fraction_multiply(
            num, den, other.num, other.den, &result.num, &result.den)) {
      return result;
    } else {
      detail::try_throw(std::overflow_error("multiplying Fraction"));
    }
  }
  Fraction operator/(const Fraction &other)
  {
    return *this * Fraction(other.den, other.num);
  }
  Fraction &operator*=(const Fraction &other)
  {
    *this = *this * other;
    return *this;
  }
  Fraction &operator/=(const Fraction &other)
  {
    *this = *this / other;
    return *this;
  }

  Fraction operator+(const Fraction &other)
  {
    Fraction result;
    if (gst_util_fraction_add(
            num, den, other.num, other.den, &result.num, &result.den)) {
      return result;
    } else {
      detail::try_throw(std::overflow_error("adding Fraction"));
    }
  }
  Fraction operator-(const Fraction &other)
  {
    return *this * Fraction(-other.num, other.den);
  }
  Fraction &operator+=(const Fraction &other)
  {
    *this = *this + other;
    return *this;
  }
  Fraction &operator-=(const Fraction &other)
  {
    *this = *this - other;
    return *this;
  }
};

inline std::ostream &
operator<<(std::ostream &out, const Fraction &v)
{
  return out << "Fraction(" << v.num << ", " << v.den << ')';
}

using DoubleRange = internal::Range<double>;
using IntRange = internal::StepRange<int>;
using Int64Range = internal::StepRange<gint64>;
using FractionRange = internal::Range<Fraction, int>;

} // namespace Gst

template<>
struct declare_gtype_of<Gst::Bitmask>
{
  static GType get_type() { return GST_TYPE_BITMASK; }
  static Gst::Bitmask get_value(const GValue *val)
  {
    return Gst::Bitmask(gst_value_get_bitmask(val));
  }
  static void set_value(GValue *val, const Gst::Bitmask &r)
  {
    gst_value_set_bitmask(val, r);
  }
};

template<>
struct declare_gtype_of<Gst::FlagSet>
{
  static GType get_type() { return GST_TYPE_FLAG_SET; }
  static Gst::FlagSet get_value(const GValue *val)
  {
    return Gst::FlagSet(
        gst_value_get_flagset_flags(val), gst_value_get_flagset_mask(val));
  }
  static void set_value(GValue *val, const Gst::FlagSet &r)
  {
    gst_value_set_flagset(val, r.flags, r.mask);
  }
};

template<>
struct declare_gtype_of<Gst::Fraction>
{
  static GType get_type() { return GST_TYPE_FRACTION; }
  static Gst::Fraction get_value(const GValue *val)
  {
    return Gst::Fraction(gst_value_get_fraction_numerator(val),
        gst_value_get_fraction_denominator(val));
  }
  static void set_value(GValue *val, const Gst::Fraction &f)
  {
    gst_value_set_fraction(val, f.num, f.den);
  }
};

template<>
struct declare_gtype_of<Gst::FractionRange>
{
  static GType get_type() { return GST_TYPE_FRACTION_RANGE; }
  static Gst::FractionRange get_value(const GValue *val)
  {
    const GValue *vmin = gst_value_get_fraction_range_min(val);
    const GValue *vmax = gst_value_get_fraction_range_max(val);
    return Gst::FractionRange(
        Gst::Fraction(gst_value_get_fraction_numerator(vmin),
            gst_value_get_fraction_denominator(vmin)),
        Gst::Fraction(gst_value_get_fraction_numerator(vmax),
            gst_value_get_fraction_denominator(vmax)));
  }
  static void set_value(GValue *val, const Gst::FractionRange &f)
  {
    gst_value_set_fraction_range_full(
        val, f.min.num, f.min.den, f.max.num, f.max.den);
  }
};

template<>
struct declare_gtype_of<Gst::DoubleRange>
{
  static GType get_type() { return GST_TYPE_DOUBLE_RANGE; }
  static Gst::DoubleRange get_value(const GValue *val)
  {
    return Gst::DoubleRange(gst_value_get_double_range_min(val),
        gst_value_get_double_range_max(val));
  }
  static void set_value(GValue *val, const Gst::DoubleRange &r)
  {
    gst_value_set_double_range(val, r.min, r.max);
  }
};

template<>
struct declare_gtype_of<Gst::IntRange>
{
  static GType get_type() { return GST_TYPE_INT_RANGE; }
  static Gst::IntRange get_value(const GValue *val)
  {
    return Gst::IntRange(gst_value_get_int_range_min(val),
        gst_value_get_int_range_max(val), gst_value_get_int_range_step(val));
  }
  static void set_value(GValue *val, const Gst::IntRange &r)
  {
    gst_value_set_int_range_step(val, r.min, r.max, r.step);
  }
};

template<>
struct declare_gtype_of<Gst::Int64Range>
{
  static GType get_type() { return GST_TYPE_INT64_RANGE; }
  static Gst::Int64Range get_value(const GValue *val)
  {
    return Gst::Int64Range(gst_value_get_int64_range_min(val),
        gst_value_get_int64_range_max(val),
        gst_value_get_int64_range_step(val));
  }
  static void set_value(GValue *val, const Gst::Int64Range &r)
  {
    gst_value_set_int64_range_step(val, r.min, r.max, r.step);
  }
};

} // namespace repository

inline repository::Gst::PadProbeConnection
make_connection(gulong id, const repository::GLib::SourceFunc &func,
    repository::Gst::Pad pad)
{
  return repository::Gst::PadProbeConnection(id, func.connection(), pad);
}

} // namespace gi

#endif
