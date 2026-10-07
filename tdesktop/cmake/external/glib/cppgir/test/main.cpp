#include <array>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

// always enable assert
#undef NDEBUG
#include "assert.h"

#define GI_CLASS_IMPL_PRAGMA
#define GI_TEST 1
#include "gi.hpp"

#include "test/test_boxed.h"
#include "test/test_object.h"

using namespace gi;
namespace GLib = gi::repository::GLib;
namespace GObject_ = gi::repository::GObject;

#define BOXED_COPY 0
#if BOXED_COPY
namespace gi
{
namespace repository
{
GI_ENABLE_BOXED_COPY(GBExample)
}
} // namespace gi
#endif

// test examples;
// GBoxed
class GBoxedExampleBase
    : public detail::GBoxedWrapperBase<GBoxedExampleBase, GBExample>
{
  typedef detail::GBoxedWrapperBase<GBoxedExampleBase, GBExample> super_type;

public:
  static GType get_type_() { return GI_CPP_TYPE_BOXED_EXAMPLE; }
  GBoxedExampleBase(std::nullptr_t = nullptr) : super_type() {}
};

class GBoxedExampleRef;

class GBoxedExample : public detail::GBoxedWrapper<GBoxedExample, GBExample,
                          GBoxedExampleBase, GBoxedExampleRef>
{
  typedef detail::GBoxedWrapper<GBoxedExample, GBExample, GBoxedExampleBase,
      GBoxedExampleRef>
      super_type;
  using super_type::super_type;
};

class GBoxedExampleRef : public detail::GBoxedRefWrapper<GBoxedExample,
                             GBExample, GBoxedExampleBase>
{
  typedef detail::GBoxedRefWrapper<GBoxedExample, GBExample, GBoxedExampleBase>
      super_type;
  using super_type::super_type;
};

// plain C
class CBoxedExampleBase
    : public detail::CBoxedWrapperBase<CBoxedExampleBase, CBExample>
{
  typedef detail::CBoxedWrapperBase<CBoxedExampleBase, CBExample> super_type;

public:
  CBoxedExampleBase(std::nullptr_t = nullptr) : super_type() {}
};

class CBoxedExampleRef;

class CBoxedExample : public detail::CBoxedWrapper<CBoxedExample, CBExample,
                          CBoxedExampleBase, CBoxedExampleRef>
{
  typedef detail::CBoxedWrapper<CBoxedExample, CBExample, CBoxedExampleBase,
      CBoxedExampleRef>
      super_type;
  using super_type::super_type;
};

class CBoxedExampleRef : public detail::CBoxedRefWrapper<CBoxedExample,
                             CBExample, CBoxedExampleBase>
{
  typedef detail::CBoxedRefWrapper<CBoxedExample, CBExample, CBoxedExampleBase>
      super_type;
  using super_type::super_type;
};

enum class CppEnum { VALUE_0 = ENUM_VALUE_0, VALUE_1 = ENUM_VALUE_1 };

enum class CppFlags { VALUE_0 = FLAG_VALUE_0, VALUE_1 = FLAG_VALUE_1 };

GI_FLAG_OPERATORS(CppFlags)

namespace gi
{
namespace repository
{
template<>
struct declare_cpptype_of<GBExample>
{
  typedef GBoxedExample type;
};

template<>
struct declare_cpptype_of<CBExample>
{
  typedef CBoxedExample type;
};

template<>
struct declare_ctype_of<CppEnum>
{
  typedef CEnum type;
};

template<>
struct declare_cpptype_of<CEnum>
{
  typedef CppEnum type;
};

template<>
struct declare_gtype_of<CppEnum>
{
  static GType get_type() { return GI_CPP_TYPE_ENUM; }
};

template<>
struct is_enumeration<CppEnum> : public std::true_type
{};

template<>
struct declare_ctype_of<CppFlags>
{
  typedef CFlags type;
};

template<>
struct declare_cpptype_of<CFlags>
{
  typedef CppFlags type;
};

template<>
struct declare_gtype_of<CppFlags>
{
  static GType get_type() { return GI_CPP_TYPE_FLAGS; }
};

template<>
struct is_bitfield<CppFlags> : public std::true_type
{};

} // namespace repository

} // namespace gi

#define DECLARE_PROPERTY(pname, ptype) \
  property_proxy<ptype> property_##pname() \
  { \
    return property_proxy<ptype>(*this, #pname); \
  } \
  const property_proxy<ptype> property_##pname() const \
  { \
    return property_proxy<ptype>(*this, #pname); \
  }
#define DECLARE_SIGNAL(name, sig) \
  signal_proxy<sig> signal_##name() { return signal_proxy<sig>(*this, #name); }

// simulate override construction
class Derived;

typedef GICppExample CDerived;

namespace base
{
class DerivedBase : public GObject_::Object
{
  typedef Object super_type;

public:
  typedef Derived self;
  typedef CDerived BaseObjectType;

  DerivedBase(std::nullptr_t = nullptr) : super_type() {}
  BaseObjectType *gobj_() { return (BaseObjectType *)super_type::gobj_(); }
  const BaseObjectType *gobj_() const
  {
    return (const BaseObjectType *)super_type::gobj_();
  }
  BaseObjectType *gobj_copy_() const
  {
    return (BaseObjectType *)super_type::gobj_copy_();
  }

  static GType get_type_() { return GI_CPP_TYPE_EXAMPLE; }

  DECLARE_PROPERTY(number, int)
  DECLARE_PROPERTY(fnumber, double)
  DECLARE_PROPERTY(data, std::string)
  DECLARE_PROPERTY(present, gboolean)
  DECLARE_PROPERTY(object, Object)
  DECLARE_PROPERTY(choice, CppEnum)
  DECLARE_PROPERTY(flags, CppFlags)
  DECLARE_PROPERTY(error, GLib::Error)

  DECLARE_SIGNAL(
      to_int, int(Derived, Object, bool, gboolean, const std::string &))
  DECLARE_SIGNAL(to_string, std::string(Derived, int, gint64))
  DECLARE_SIGNAL(to_void, void(Derived, double, CppEnum, CppFlags))
  DECLARE_SIGNAL(to_output_int,
      void(Derived, unsigned &,
          gi::Collection<::GPtrArray, char *, gi::transfer_none_t>,
          gi::Collection<::GSList, char *, gi::transfer_none_t>))
};

} // namespace base

class Derived : public base::DerivedBase
{
  typedef base::DerivedBase super;
  using super::super;
};

namespace gi
{
namespace repository
{
template<>
struct declare_cpptype_of<CDerived>
{
  typedef Derived type;
};

} // namespace repository

} // namespace gi

void
test_trait()
{
  static_assert(traits::is_basic<int>::value, "");
  static_assert(traits::is_basic<float>::value, "");
  static_assert(traits::is_basic<void *>::value, "");
  static_assert(!traits::is_basic<char *>::value, "");

  static_assert(traits::is_boxed<GBoxedExample>::value, "");
  static_assert(traits::is_object<Derived>::value, "");
  static_assert(!traits::is_object<int>::value, "");

  static_assert(traits::is_reftype<GBoxedExampleRef>::value, "");
  static_assert(!traits::is_reftype<GBoxedExample>::value, "");
  static_assert(!traits::is_reftype<int>::value, "");

  static_assert(traits::has_ctype_member<Derived>::value, "");
  static_assert(!traits::has_ctype_member<int>::value, "");

  static_assert(
      std::is_same<traits::ctype<Derived>::type, CDerived *>::value, "");
  static_assert(
      std::is_same<traits::ctype<const Derived>::type, const CDerived *>::value,
      "");

  static_assert(
      std::is_same<traits::cpptype<CDerived *>::type, Derived>::value, "");
  static_assert(std::is_same<traits::cpptype<const CDerived *>::type,
                    const Derived>::value,
      "");

  static_assert(std::is_same<traits::ctype<CppEnum>::type, CEnum>::value, "");
  static_assert(std::is_same<traits::cpptype<CEnum>::type, CppEnum>::value, "");

  static_assert(
      std::is_same<traits::ctype<CBoxedExample>::type, CBExample *>::value, "");
  static_assert(
      std::is_same<traits::cpptype<CBExample>::type, CBoxedExample>::value, "");
  static_assert(std::is_same<traits::reftype<CBoxedExample>::type,
                    CBoxedExampleRef>::value,
      "");

  static_assert(
      std::is_same<traits::ctype<GBoxedExample>::type, GBExample *>::value, "");
  static_assert(
      std::is_same<traits::cpptype<GBExample>::type, GBoxedExample>::value, "");
  static_assert(std::is_same<traits::reftype<GBoxedExample>::type,
                    GBoxedExampleRef>::value,
      "");
  static_assert(
      std::is_same<traits::cpptype<GBExample *, transfer_none_t>::type,
          GBoxedExampleRef>::value,
      "");

  static_assert(
      std::is_same<traits::ctype<GObject_::Object>::type, _GObject *>::value,
      "");
  static_assert(
      std::is_same<traits::cpptype<_GObject>::type, GObject_::Object>::value,
      "");

  static_assert(std::is_same<traits::cpptype<bool>::type, bool>::value, "");

  static_assert(traits::gtype<int>::value, "");
  static_assert(traits::gtype<std::string>::value, "");

  static_assert(traits::gvalue<int>::value, "");
  static_assert(traits::gvalue<CppEnum>::value, "");

  static_assert(traits::is_enumeration<CppEnum>::value, "");
  static_assert(!traits::is_bitfield<CppEnum>::value, "");
  static_assert(traits::is_bitfield<CppFlags>::value, "");

  static_assert(
      std::is_pointer<GLib::DestroyNotify::cfunction_type>::value, "");

  static_assert(!detail::allocator<int>::value, "");
  static_assert(detail::allocator<GObject_::Value>::value, "");
  static_assert(detail::allocator<GBoxedExample>::value, "");

  static_assert(traits::is_type_complete<GBExample>::value, "");
  struct BlackBox;
  static_assert(!traits::is_type_complete<BlackBox>::value, "");

  static_assert(
      std::is_copy_constructible<GBoxedExample>::value == BOXED_COPY, "");
  static_assert(
      std::is_copy_assignable<GBoxedExample>::value == BOXED_COPY, "");
  static_assert(std::is_move_constructible<GBoxedExample>::value, "");
  static_assert(std::is_move_assignable<GBoxedExample>::value, "");

  static_assert(std::is_copy_constructible<GBoxedExampleRef>::value, "");
  static_assert(std::is_copy_assignable<GBoxedExampleRef>::value, "");

  static_assert(
      std::is_copy_constructible<CBoxedExample>::value == BOXED_COPY, "");
  static_assert(
      std::is_copy_assignable<CBoxedExample>::value == BOXED_COPY, "");
  static_assert(std::is_move_constructible<CBoxedExample>::value, "");
  static_assert(std::is_move_assignable<CBoxedExample>::value, "");

  static_assert(std::is_copy_constructible<CBoxedExampleRef>::value, "");
  static_assert(std::is_copy_assignable<CBoxedExampleRef>::value, "");

  // internal
  static_assert(detail::_traits::is_basic_argument<int>::value, "");
  static_assert(!detail::_traits::is_basic_argument<int &>::value, "");
  static_assert(!detail::_traits::is_basic_argument<std::string>::value, "");
  static_assert(
      detail::_traits::is_basic_argument<char *const *const>::value, "");

  static_assert(
      detail::_traits::is_const_lvalue<const std::string &>::value, "");
  static_assert(!detail::_traits::is_const_lvalue<GBoxedExample &>::value, "");
}

// test helper
template<typename T>
int
refcount(T *obj)
{
  return (((GObject *)(obj))->ref_count);
}

void
test_wrap()
{
  { // plain boxed
    CBExample *ex = gi_cpp_cbexample_new();
    auto cb = wrap(ex, transfer_full);
    assert(cb.gobj_() == ex);
    auto cb2 = wrap(ex, transfer_none);
    assert(cb2 == cb);
    assert(cb.gobj_() == cb2.gobj_());
    auto cb3 = cb2;
    assert(cb3 == cb2);
    assert(cb2.gobj_() == cb3.gobj_());

    auto cbn = CBoxedExample::new_();
    assert(cbn.gobj_() != nullptr);
    assert(cbn.gobj_()->data == 0);
    // a peek
    auto ex2 = unwrap(cbn, transfer_none);
    assert(ex2 == cbn.gobj_());
    // a fresh copy
    auto ex3 = unwrap(std::move(cbn), transfer_full);
    assert(ex3 != cbn.gobj_());
    gi_cpp_cbexample_free(ex3);

    cbn = CBoxedExample();
    assert(cbn == nullptr);
    assert(cbn.gobj_() == nullptr);
    assert(!cbn);
    auto ex4 = unwrap(std::move(cbn), transfer_full);
    assert(ex4 == nullptr);

    // various basic operations
    CBoxedExample cbm(std::move(cbn));
    cbm = std::move(cbn);
    assert(!cbn);
    std::swap(cbm, cbn);
    assert(!cbm);

    // compile check
    CBoxedExample f = CBoxedExample::new_();
    f = nullptr;

    // ref
    CBoxedExampleRef r(f);
    assert(r.gobj_() == f.gobj_());
    r = nullptr;
    assert(r.gobj_() == nullptr);
    r = f;
    assert(r.gobj_() == f.gobj_());
    // ref wrap
    auto r2 = wrap(ex, transfer_none);
    static_assert(std::is_same<decltype(r2), CBoxedExampleRef>::value, "");
    r = r2;
    assert(r.gobj_() == ex);
    auto ro = unwrap(r, transfer_none);
    auto ri = unwrap(r, transfer_none);
    assert(ri == ro);
  }

  { // similarly so gboxed
    GBExample *exc = gi_cpp_gbexample_new();
    GBoxedExample w1 = wrap(exc, transfer_full);
    GBoxedExample w2 = std::move(w1);
    assert(!w1);
    assert(w2.gobj_() == exc);
    GBoxedExample w3 = w2.copy_();
    assert(w3);
    assert(w3 != w2);
    assert(w3.gobj_() != w2.gobj_());
    w3 = nullptr;
    // ref
    GBoxedExampleRef r(w2);
    assert(r.gobj_() == w2.gobj_());
    r = nullptr;
    assert(r.gobj_() == nullptr);
    r = w2;
    assert(r.gobj_() == w2.gobj_());
    // ref wrap
    r = wrap(exc, transfer_none);
    assert(r.gobj_() == exc);
    auto ro = unwrap(r, transfer_none);
    auto ri = unwrap(r, transfer_none);
    assert(ri == ro);
    auto rc = r.copy_();
    assert(rc != r);

#if BOXED_COPY
    {
      GBoxedExample ww{w3};
      ww = w3;
      r = w3;
      ww = r;
      ww = std::move(r);
    }
#endif
  }

  { // string
    const gchar *str{};
    std::string s1 = wrap(str, transfer_none);
    assert(s1.empty());
    str = "TEST";
    std::string s2 = wrap(str, transfer_none);
    assert(s2 == str);
    auto *cs = unwrap(s2, transfer_none);
    assert(g_strcmp0(cs, str) == 0);
    // now something we should be able to free
    cs = unwrap(s2, transfer_full);
    g_free((gpointer)cs);
    // unwrap empty optional
    const std::string &emptytest = "";
    auto cempty = unwrap(emptytest, transfer_none);
    assert(*cempty == 0);
    auto ocempty = unwrap(
        static_cast<const detail::optional_string &>(emptytest), transfer_none);
    assert(ocempty == nullptr);
  }

  { // some more plain cases
    wrap(5);
    unwrap(5, transfer_none);

    const double ex = 5.0;
    auto w = wrap(ex);
    auto ex2 = unwrap(w);
    assert(ex == ex2);

    CEnum ec{};
    CppEnum ecpp{};
    ecpp = wrap(ec);
    unwrap(ecpp, transfer_none);
  }

  { // object
    CDerived *ob = gi_cpp_example_new();
    assert(refcount(ob) == 1);
    auto wo = wrap(ob, transfer_none);
    assert(refcount(ob) == 2);
    assert(wo.gobj_() == ob);
    {
      auto wo2 = wo;
      assert(wo2 == wo);
      assert(wo2.gobj_() == ob);
      assert(refcount(ob) == 3);
      // cast to own type should work fine
      auto wo3 = object_cast<Derived>(wo2);
      assert(wo3);
      assert(refcount(ob) == 4);
    }
    assert(refcount(ob) == 2);
    auto ob2 = unwrap(wo, transfer_none);
    assert(ob2 == ob);
    assert(refcount(ob) == 2);
    ob2 = unwrap(wo, transfer_full);
    assert(ob2 == ob);
    assert(refcount(ob) == 3);
    g_object_unref(ob2);
    ob2 = nullptr;
    assert(refcount(ob) == 2);

    // no change for full transfer
    auto wo2 = wrap(ob, transfer_full);
    assert(wo2.gobj_() == ob);
    assert(refcount(ob) == 2);
    // compensate
    g_object_ref(ob);
    wo2 = Derived();
    assert(wo2.gobj_() == nullptr);
    assert(refcount(ob) == 2);
    wo2 = std::move(wo);
    assert(wo.gobj_() == nullptr);
    assert(wo2.gobj_() == ob);
    wo2 = wo;
    assert(refcount(ob) == 1);
    g_object_unref(ob);

    Derived wo3(wo2);
    if (wo3)
      wo3 = nullptr;
  }

  {
    // object creation
    Derived ob = GObject_::Object::new_<Derived>();
    ob = GObject_::Object::new_<Derived>(
        NAME_NUMBER, (int)5, NAME_PRESENT, true);
    assert(ob.property_number().get() == 5);
    assert(ob.property_present().get() == true);

    Derived ob2 = GObject_::Object::new_<Derived>();
    std::swap(ob2, ob);
  }

  {
    // paramspec
    auto pspec = GObject_::ParamSpec::new_<int>("p", "p", "p", 0, 10);
    // is otherwise similar to property and is tested there further
  }

  { // wrap_to
    int x = 5;
    auto y = wrap_to<int>(x, transfer_none);
    assert(x == y);
    wrap_to<std::string>("x", transfer_none);
  }

  { // unwrap_maybe_float
    CDerived *ob = gi_cpp_example_new();
    assert(refcount(ob) == 1);
    auto wo = wrap(ob, transfer_full);
    assert(refcount(ob) == 1);
    CDerived *uwo = detail::unwrap_maybe_float(std::move(wo), transfer_none);
    assert(uwo == ob);
    assert(!wo);
    assert(refcount(ob) == 1);
    assert(g_object_is_floating(ob));
    wo = wrap(ob, transfer_none);
    assert(refcount(ob) == 1);
    assert(!g_object_is_floating(ob));
    uwo = detail::unwrap_maybe_float(std::move(wo), transfer_full);
    assert(!wo);
    assert(uwo == ob);
    assert(refcount(ob) == 1);
    wo = wrap(ob, transfer_full);
    assert(refcount(ob) == 1);
    // wrapper will clean up

    GBExample *exc = gi_cpp_gbexample_new();
    GBoxedExample w1 = wrap(exc, transfer_full);
    GBExample *uw1 = detail::unwrap_maybe_float(std::move(w1), transfer_full);
    assert(!w1);
    assert(uw1 == exc);
    w1 = wrap(exc, transfer_full);
    // wrapper will clean up
  }

  { // compile checks on cs_ptr helper
    using Object = gi::repository::GObject::Object;
    auto g = [](Object) {};
    auto f = [&g](const gi::cs_ptr<Object> &ob) {
      ob->handler_block(0);
      Object obx = ob;
      auto y = ob;
      auto x = [y]() { y->handler_block(1); };
      g(ob);
    };
    Object ob{};
    if (false)
      f(ob);
  }
}

void
test_helpers()
{
  constexpr int VALUE = 5;
  int value = VALUE * 3;
  using nmtype = std::unique_ptr<int>;
  struct Args
  {
    gi::required<nmtype> ptr;
    gi::required<int> v;
    int ov{};
    int &rv;
  };

  auto rf = [&value](nmtype ptr, int v, int ov, int &rv) {
    assert(ptr && *ptr == VALUE);
    assert(v == VALUE * 2);
    assert(ov == 0);
    assert(rv == value);
  };

  auto f = [&rf](Args args) {
    rf(std::move(args.ptr), std::move(args.v), std::move(args.ov), args.rv);
  };

  f({.ptr = std::make_unique<int>(VALUE), .v = VALUE * 2, .rv = value});
}

using gi::detail::Collection;
using gi::detail::CollectionHolder;

namespace collection_helpers
{
struct LVHandler
{
  template<typename T>
  T &f(T &t)
  {
    return t;
  }
};

struct RVHandler
{
  template<typename T>
  T &&f(T &t)
  {
    return std::move(t);
  }
};

template<typename ListType, typename T, typename Transfer>
using CollectionProxy =
    Collection<ListType, typename traits::ctype<T>::type, Transfer>;

template<typename ListType, typename T, typename Transfer>
using CollectionHolderProxy =
    CollectionHolder<ListType, typename traits::ctype<T>::type, Transfer>;

std::string
get_value(const std::string &x)
{
  return x;
}

std::string
get_value(const gi::cstring &x)
{
  return x;
}

template<typename T>
auto
get_value(const T &x) -> decltype(unwrap(x, transfer_none))
{
  return unwrap(const_cast<T &>(x), transfer_none);
}

template<template<typename, typename, typename> class CT = CollectionProxy,
    typename CppType, typename Transfer, typename LVRHandler>
void
check_collection(std::vector<CppType> &&v, const Transfer &t, LVRHandler h)
{
  // collect size
  const auto VS = v.size();
  // and (internal) value for later check
  using VT = decltype(get_value(v.front()));
  std::vector<VT> baseline;
  auto get_values = [&v] {
    decltype(baseline) out;
    for (auto &val : v)
      out.push_back(get_value(val));
    return out;
  };
  baseline = get_values();
  auto do_check = [&get_values, &baseline]() {
    auto current = get_values();
    assert(current == baseline);
  };
  auto check_list_it = [&baseline](auto &list) {
    decltype(baseline) current;
    int cnt = 0;
    for (auto &e : list) {
      // e should be const & iff Transfer none
      using el_type = typename std::remove_reference<decltype(e)>::type;
      static_assert(std::is_const<el_type>::value ==
                        std::is_same<Transfer, transfer_none_t>::value,
          "");
      assert(e);
      ++cnt;
      current.push_back(get_value(e));
    }
    assert(cnt == int(baseline.size()));
    assert(current == baseline);
    cnt = 0;
    current.clear();
    const auto &clist = list;
    for (auto &e : clist) {
      // e should always be const & here
      using el_type = typename std::remove_reference<decltype(e)>::type;
      static_assert(std::is_const<el_type>::value, "");
      assert(e);
      ++cnt;
      current.push_back(get_value(e));
    }
    assert(cnt == int(baseline.size()));
    assert(current == baseline);
    // assign from non-const to const
    auto cb = clist.begin();
    cb = list.begin();
    assert(!cnt || *cb);
  };

  // various trips through variations
  CT<::GList, CppType, Transfer> l{h.f(v)};
  static constexpr bool is_sub = std::is_same<decltype(l),
      CollectionHolderProxy<::GList, CppType, Transfer>>::value;
  static_assert(is_sub || (sizeof(l) == sizeof(::GList *)), "");
  check_list_it(l);
  v = std::move(l);
  assert(!l);
  assert(v.size() == VS);
  do_check();
  l = h.f(v);
  v = std::move(l);
  assert(!l);
  assert(v.size() == VS);
  do_check();
  CT<::GSList, CppType, Transfer> sl{h.f(v)};
  static_assert(is_sub || (sizeof(l) == sizeof(::GSList *)), "");
  check_list_it(sl);
  v = std::move(sl);
  assert(!sl);
  assert(v.size() == VS);
  do_check();
  sl = h.f(v);
  v = std::move(sl);
  assert(!sl);
  assert(v.size() == VS);
  do_check();
  CT<::GPtrArray, CppType, Transfer> pa{h.f(v)};
  static_assert(is_sub || (sizeof(l) == sizeof(::GPtrArray *)), "");
  check_list_it(pa);
  v = std::move(pa);
  assert(!pa);
  assert(v.size() == VS);
  do_check();
  pa = h.f(v);
  v = std::move(pa);
  assert(!pa);
  assert(v.size() == VS);
  do_check();
  CT<DSpan, CppType, Transfer> da{h.f(v)};
  check_list_it(da);
  v = std::move(da);
  assert(!da);
  assert(v.size() == VS);
  do_check();
  da = h.f(v);
  v = std::move(da);
  assert(!da);
  assert(v.size() == VS);
  do_check();
  CT<ZTSpan, CppType, Transfer> za{h.f(v)};
  check_list_it(za);
  v = std::move(za);
  assert(!za);
  za = h.f(v);
  v = std::move(za);
  assert(!za);
  assert(v.size() == VS);
  do_check();
  l = h.f(v);
  // temporarily use a separate sv
  // (so v can keep its elements alive, e.g. std::string case)
  std::vector<CppType> sv;
  sv.resize(VS);
  std::move(l).move_to(sv.data());
  assert(!l);
  v = std::move(sv);
  sv.clear();
  do_check();
  // in case of holder; l has ownership here (so none wrap/unwrap below ok)
  l = h.f(v);
  auto lu = unwrap(std::move(l), t);
  assert(!l || !t.value);
  // last part; always need to wrap to a basic wrapper
  using WrapTypeL = CollectionProxy<::GList, CppType, Transfer>;
  auto ln = wrap_to<WrapTypeL>(lu, t);
  assert(lu == ln.gobj_());
  v = std::move(ln);
  do_check();
  pa = h.f(v);
  auto pau = unwrap(std::move(pa), t);
  assert(!pa || !t.value);
  // ref'ed case, so it can always take ownership
  using WrapTypeA = CollectionProxy<::GPtrArray, CppType, Transfer>;
  auto pan = wrap_to<WrapTypeA>(pau, t);
  assert(pau == pan.gobj_());
}

template<template<typename, typename, typename> class CT = CollectionProxy,
    typename CppKey, typename CppType, typename Transfer, typename LVRHandler>
void
check_collection(std::map<CppKey, CppType> &&v, const Transfer &t, LVRHandler h)
{
  const auto VS = v.size();
  using VT = decltype(get_value(v.begin()->second));
  std::map<CppKey, VT> baseline;
  auto get_values = [](auto &c) {
    decltype(baseline) out;
    for (auto &val : c)
      out[val.first] = get_value(val.second);
    return out;
  };
  baseline = get_values(v);
  auto do_check = [&get_values, &baseline](auto &c) {
    auto current = get_values(c);
    assert(current == baseline);
  };
  auto check_map_it = [&baseline](auto &list) {
    decltype(baseline) current;
    int cnt = 0;
    for (auto &e : list) {
      assert(e.first);
      assert(e.second);
      ++cnt;
      current[get_value(e.first)] = get_value(e.second);
    }
    assert(cnt == int(baseline.size()));
    assert(current == baseline);
    cnt = 0;
    current.clear();
    const auto &clist = list;
    for (auto &e : clist) {
      assert(e.first);
      assert(e.second);
      ++cnt;
      current[get_value(e.first)] = get_value(e.second);
    }
    assert(cnt == int(baseline.size()));
    assert(current == baseline);
  };

  CT<::GHashTable, std::pair<CppKey, CppType>, Transfer> l{h.f(v)};
  static constexpr bool is_sub = std::is_same<decltype(l),
      CollectionHolderProxy<::GHashTable, std::pair<CppKey, CppType>,
          Transfer>>::value;
  static_assert(is_sub || (sizeof(l) == sizeof(::GHashTable *)), "");
  check_map_it(l);
  v = std::move(l);
  assert(!l);
  assert(v.size() == VS);
  do_check(v);
  l = h.f(v);
  v = std::move(l);
  assert(!l);
  assert(v.size() == VS);
  do_check(v);
  l = h.f(v);
  std::unordered_map<std::string, CppType> w;
  w = std::move(l);
  assert(!l);
  assert(w.size() == VS);
  do_check(w);
  // in case of holder; l has ownership here (so none wrap/unwrap below ok)
  l = h.f(w);
  auto lu = unwrap(std::move(l), t);
  assert(!l || !t.value);
  // last part; always need to wrap to a basic wrapper
  using WrapType =
      CollectionProxy<::GHashTable, std::pair<CppKey, CppType>, Transfer>;
  auto ln = wrap_to<WrapType>(lu, t);
  assert(lu == ln.gobj_());
}

template<typename ListType, typename Transfer>
static void
check_collection_null()

{ // check nullptr cases
  // type not relevant in these cases
  using BT = CDerived *;
  std::vector<Derived> bv;
  Transfer t;
  {
    auto ln =
        wrap_to<Collection<ListType, BT, Transfer>>((ListType *)(nullptr), t);
    assert(!ln);
    ln = nullptr;
    assert(!ln);
    bv = std::move(ln);
    assert(bv.empty());
  }
}

} // namespace collection_helpers

void
test_collection()
{
  using namespace collection_helpers;

  auto make_object = [] { return wrap(gi_cpp_example_new(), transfer_full); };

  auto make_box = []() -> GBoxedExample {
    return wrap(gi_cpp_gbexample_new(), transfer_full);
  };

  { // init list construction
    CollectionProxy<::GPtrArray, Derived, transfer_full_t> coll_pa{
        make_object(), make_object()};
    assert(coll_pa.size() == 2);
    CollectionProxy<gi::DSpan, int, transfer_full_t> coll_da{2, 3, 4};
    assert(coll_da.size() == 3);
#if 0
    // will not work, since not copyable
    CollectionProxy<gi::ZTSpan, GBoxedExample, transfer_full_t> coll_za{
        make_box()};
    assert(coll_za.size() == 1);
    // but neither will the following
    std::vector<std::unique_ptr<int>> x{std::make_unique<int>(5)};
#endif
  }

  // full
  { // object
    std::vector<Derived> v{make_object(), make_object()};
    check_collection(std::move(v), transfer_full, LVHandler());
    // map
    std::map<std::string, Derived> m{
        {"x", make_object()}, {"y", make_object()}};
    check_collection(std::move(m), transfer_full, LVHandler());
  }
  { // non-copyable boxed
    std::vector<GBoxedExample> v;
    v.push_back(make_box());
    v.push_back(make_box());
    check_collection(std::move(v), transfer_full, RVHandler());
    // map
    std::map<std::string, GBoxedExample> m;
    m["x"] = make_box();
    m["y"] = make_box();
    check_collection(std::move(m), transfer_full, RVHandler());
  }
  { // string
    std::vector<std::string> v{"foo", "bar"};
    check_collection(std::move(v), transfer_full, LVHandler());
    // map
    std::map<std::string, std::string> m{{"x", "foo"}, {"y", "bar"}};
    check_collection(std::move(m), transfer_full, LVHandler());
  }
  { // cstring
    std::vector<gi::cstring> v{"foo", "bar"};
    check_collection(std::move(v), transfer_full, LVHandler());
    // map
    std::map<gi::cstring, gi::cstring> m{{"x", "foo"}, {"y", "bar"}};
    check_collection(std::move(m), transfer_full, LVHandler());
  }
  { // plain
    std::vector<int> v{2, 3};
    auto w = v;
    Collection<gi::DSpan, int, transfer_full_t> da{v.data(), v.size()};
    v = std::move(da);
    assert(!da);
    assert(v == w);
    da = v;
    std::move(da).move_to(v.data());
    assert(v == w);
  }

  // container
  { // object
    std::vector<Derived> v{make_object(), make_object()};
    // hold item ownership as they are moved about
    auto t = v;
    check_collection(std::move(v), transfer_container, LVHandler());
    // map
    std::map<std::string, Derived> m{
        {"x", make_object()}, {"y", make_object()}};
    auto tm = m;
    check_collection(std::move(m), transfer_container, LVHandler());
  }
  { // copyable ref boxed
    std::vector<GBoxedExample> v;
    v.push_back(make_box());
    v.push_back(make_box());
    std::vector<GBoxedExampleRef> w{v.begin(), v.end()};
    check_collection(std::move(w), transfer_container, LVHandler());
    // map
    std::map<std::string, GBoxedExampleRef> m{
        {"x", v.front()}, {"y", v.back()}};
    check_collection(std::move(m), transfer_container, LVHandler());
  }
  { // string
    std::vector<std::string> v{"foo", "bar"};
    check_collection(std::move(v), transfer_container, LVHandler());
    // map
    std::map<std::string, std::string> m{{"x", "foo"}, {"y", "bar"}};
    check_collection(std::move(m), transfer_container, LVHandler());
  }
  { // cstring
    std::vector<gi::cstring> v{"foo", "bar"};
    std::vector<gi::cstring_v> w{v.begin(), v.end()};
    check_collection(std::move(w), transfer_container, LVHandler());
    // map
    std::map<gi::cstring, gi::cstring_v> m{{"x", v.front()}, {"y", v.back()}};
    check_collection(std::move(m), transfer_container, LVHandler());
  }
  { // plain
    std::vector<CppEnum> v{CppEnum::VALUE_0, CppEnum::VALUE_1};
    auto w = v;
    CollectionProxy<gi::DSpan, CppEnum, transfer_container_t> da{
        v.data(), v.size()};
    v = std::move(da);
    assert(!da);
    assert(v == w);
    da = v;
    std::move(da).move_to(v.data());
    assert(v == w);
  }

  // none
  {
    static_assert(!std::is_copy_constructible<
                      CollectionHolder<::GList, CDerived *>>::value,
        "");
    static_assert(std::is_move_constructible<
                      CollectionHolder<::GList, CDerived *>>::value,
        "");
  }
  // parameter helper should behave much as a dynamic container transfer
  { // object
    std::vector<Derived> v{make_object(), make_object()};
    // hold item ownership as they are moved about
    auto t = v;
    check_collection<CollectionHolderProxy>(
        std::move(v), transfer_none, LVHandler());
    // map
    std::map<std::string, Derived> m{
        {"x", make_object()}, {"y", make_object()}};
    auto tm = m;
    check_collection<CollectionHolderProxy>(
        std::move(m), transfer_none, LVHandler());
  }
  { // copyable ref boxed
    std::vector<GBoxedExample> v;
    v.push_back(make_box());
    v.push_back(make_box());
    std::vector<GBoxedExampleRef> w{v.begin(), v.end()};
    check_collection<CollectionHolderProxy>(
        std::move(w), transfer_none, LVHandler());
    // map
    std::map<std::string, GBoxedExampleRef> m{
        {"x", v.front()}, {"y", v.back()}};
    check_collection<CollectionHolderProxy>(
        std::move(m), transfer_none, LVHandler());
  }
  { // string
    std::vector<std::string> v{"foo", "bar"};
    check_collection<CollectionHolderProxy>(
        std::move(v), transfer_none, LVHandler());
    // map
    std::map<std::string, std::string> m{{"x", "foo"}, {"y", "bar"}};
    check_collection<CollectionHolderProxy>(
        std::move(m), transfer_none, LVHandler());
  }
  { // cstring
    std::vector<gi::cstring> v{"foo", "bar"};
    std::vector<gi::cstring_v> w{v.begin(), v.end()};
    check_collection<CollectionHolderProxy>(
        std::move(w), transfer_none, LVHandler());
    // map
    std::map<gi::cstring, gi::cstring_v> m{{"x", v.front()}, {"y", v.back()}};
    check_collection<CollectionHolderProxy>(
        std::move(m), transfer_none, LVHandler());
  }

  // some other array cases
  {
    std::array<const char *, 2> sa{{"x", "y"}};
    Collection<gi::DSpan, char *, transfer_none_t> sc;
    sc = wrap_to<decltype(sc)>(sa.data(), sa.size(), transfer_none);
    assert(sc.gobj_() == sa.data());
    auto usa = unwrap(sc, transfer_none);
    assert(usa == sa.data());
  }
  { // case that might arise trying to pass main's(argv, argc) to a parameter
    std::array<const char *, 2> sa{{"x", "y"}};
    CollectionParameter<gi::DSpan, char *, transfer_none_t> sc{
        (char **)sa.data(), sa.size()};
    assert(sc.gobj_() == sa.data());
    auto usa = unwrap(sc, transfer_none);
    assert(usa == sa.data());
  }
  {
    std::array<int, 2> a{3, 4};
    CollectionHolderProxy<gi::DSpan, int, transfer_none_t> ac{
        a.data(), a.size()};
    // optimization applies
    assert(ac.gobj_() == a.data());
    assert(ac._size() == a.size());
    auto rc = ac.gobj_();
    auto uac = unwrap(std::move(ac), transfer_none);
    assert(rc == uac);
    for (int i : {0, 1})
      assert(uac[i] == a[i]);
    ac = wrap_to<decltype(ac)>(uac, a.size(), transfer_none);
    assert(ac.gobj_() == uac);
  }
  {
    std::array<int, 2> a{5, 6};
    // allocate new array
    Collection<gi::DSpan, int, transfer_full_t> ac{a.data(), a.size()};
    assert(ac.gobj_() != a.data());
    assert(ac._size() == a.size());
    auto rc = ac.gobj_();
    auto uac = unwrap(std::move(ac), transfer_full);
    // now owned by uac above
    assert(!ac);
    assert(rc == uac);
    for (int i : {0, 1})
      assert(uac[i] == a[i]);
    // transfer full to ac again
    ac = wrap_to<decltype(ac)>(uac, a.size(), transfer_full);
    assert(ac.gobj_() == uac);
  }

  { // check GHashTable transfer_none hack
    using VT = std::pair<char *, char *>;
    using TT = gi::CollectionParameter<::GHashTable, VT, transfer_none_t>;
    static_assert(
        std::is_same<TT,
            gi::detail::Collection<::GHashTable, VT, transfer_full_t>>::value,
        "");
    // should work for refcnt case
    TT t;
    unwrap(t, transfer_none);
    // also check null handling
    assert(!t);
    t = nullptr;
    assert(!t);
    std::map<std::string, std::string> m = std::move(t);
    assert(m.empty());
  }

  { // nullptr handling
    check_collection_null<::GList, transfer_full_t>();
    check_collection_null<::GList, transfer_none_t>();
    check_collection_null<::GSList, transfer_full_t>();
    check_collection_null<::GSList, transfer_none_t>();
    check_collection_null<::GPtrArray, transfer_full_t>();
    check_collection_null<::GPtrArray, transfer_none_t>();
    // array cases
    using BT = int;
    std::vector<int> bv;
    {
      auto ln = wrap_to<Collection<ZTSpan, BT, transfer_full_t>>(
          (BT *)(nullptr), transfer_full);
      assert(!ln);
      ln = nullptr;
      assert(!ln);
      bv = std::move(ln);
      assert(bv.empty());
    }
    {
      auto ln = wrap_to<Collection<DSpan, BT, transfer_none_t>>(
          (BT *)(nullptr), transfer_none);
      assert(!ln);
      ln = nullptr;
      assert(!ln);
      bv = std::move(ln);
      assert(bv.empty());
    }
  }

  { // transition to other transfer types
    auto check_equal = [](auto &l1, auto &l2) {
      assert(!l1.empty());
      assert(!l2.empty());
      assert(l1.front());
      assert(l2.front());
      auto it1 = l1.begin();
      auto it2 = l2.begin();
      for (; it1 != l1.end() && it2 != l2.end(); ++it1, ++it2) {
        assert(*it1 == *it2);
      }
    };
    CollectionProxy<::GList, GBoxedExample, transfer_full_t> lf;
    lf.push_back(make_box());
    lf.push_back(make_box());
    assert(!lf.empty());
    assert(++lf.begin() != lf.end());
    assert(++(++lf.begin()) == lf.end());
    // generic container creation
    CollectionProxy<::GList, GBoxedExample, transfer_container_t> lc{lf};
    check_equal(lf, lc);
    // special case to none
    CollectionProxy<::GList, GBoxedExample, transfer_none_t> ln{lf};
    check_equal(lf, ln);
    // also this way
    lc = lf;
    check_equal(lf, lc);
    ln = lc;
    check_equal(ln, lc);
  }

  auto uw = [](const auto &obj) { return unwrap(obj, transfer_none); };
  auto check_list = [&uw](auto &lf, auto factory) {
    lf.push_back(factory());
    assert(!lf.empty());
    auto p = uw(lf.front());
    lf.push_back(factory());
    assert(uw(lf.front()) == p);
    assert(std::next(lf.begin()) != lf.end());
    assert(std::next(lf.begin(), 2) == lf.end());
    lf.push_front(factory());
    assert(uw(lf.front()) != p);
    lf.pop_front();
    assert(uw(lf.front()) == p);
    lf.pop_back();
    assert(uw(lf.front()) == p);
    lf.pop_back();
    assert(lf.empty());
    lf.push_front(factory());
    assert(!lf.empty());
    lf.pop_front();
    assert(lf.empty());
    lf.push_front(factory());
    assert(!lf.empty());
    lf.clear();
    assert(lf.empty());
    auto it = lf.insert(lf.begin(), factory());
    p = uw(lf.front());
    assert(it == lf.begin());
    it = lf.insert(lf.begin(), factory());
    assert(it == lf.begin());
    assert(uw(lf.front()) != p);
    lf.insert(lf.end(), factory());
    assert(uw(*std::next(lf.begin())) == p);
    it = std::next(lf.begin());
    while (it != lf.end())
      it = lf.erase(it);
    assert(std::next(lf.begin()) == lf.end());
    it = lf.erase(lf.begin());
    assert(it == lf.end());
    assert(lf.empty());
    lf.push_front(factory());
    typename std::remove_reference<decltype(lf)>::type lfo;
    lf.swap(lfo);
    assert(lf.empty());
    assert(!lfo.empty());
    lfo.pop_front();
    assert(lfo.empty());
  };
  auto check_array = [&uw](auto &l, auto factory) {
    l.clear();
    l.push_back(factory());
    auto p = uw(l.front());
    assert(p);
    assert(l.data()[0] == l[0]);
    assert(l.data()[0] == l.at(0));
    assert(l.data()[0] == l.front());
    assert(l.at(0));
    l.resize(10);
    assert(l.size() == 10);
    assert(!l.back());
    l.resize(1);
    assert(l.size() == 1);
    assert(l.back() == l.front());
    assert(uw(l.back()) == p);
  };
  { // check common operations on lists
    CollectionProxy<::GList, GBoxedExample, transfer_full_t> lf;
    check_list(lf, make_box);
    // special list part
    lf.clear();
    lf.push_back(make_box());
    lf.push_back(make_box());
    auto p = lf.front().gobj_();
    lf.reverse();
    assert(lf.front().gobj_() != p);
    lf.reverse();
    assert(lf.front().gobj_() == p);
    // ptrarray
    CollectionProxy<::GPtrArray, GBoxedExample, transfer_full_t> pa;
    check_list(pa, make_box);
    check_array(pa, make_box);
    // dyn array
    CollectionProxy<gi::DSpan, GBoxedExample, transfer_full_t> da;
    check_list(da, make_box);
    check_array(da, make_box);
    // zt array
    CollectionProxy<gi::ZTSpan, GBoxedExample, transfer_full_t> za;
    check_list(za, make_box);
    check_array(za, make_box);
    // check ZT
    za.clear();
    za.push_back(make_box());
    za.push_back(make_box());
    assert(*za.end() == nullptr);
    p = za.front().gobj_();
    // auto d = za.data();
    auto cap = za.capacity();
    za.reserve(5 * cap);
    assert(za.capacity() >= 5 * cap);
    // NOTE new alloc might match old alloc
    // assert(d != za.data());
    assert(za.front().gobj_() == p);
    // plain array
    CollectionProxy<gi::DSpan, int, transfer_full_t> pda;
    int gen = 1;
    auto make_int = [&gen]() { return ++gen; };
    check_list(pda, make_int);
    check_array(pda, make_int);
  }
  { // map operations
    Collection<::GHashTable, std::pair<char *, GBExample *>, transfer_full_t>
        mf;
    assert(mf.empty());
    const char *KEY1 = "key1";
    const char *KEY2 = "key2";
    auto v1 = make_box();
    auto v2 = make_box();
    auto p1 = uw(v1);
    auto p2 = uw(v2);
    auto ret = mf.replace(KEY1, std::move(v1));
    assert(ret);
    assert(mf.size() == 1);
    ret = mf.replace(KEY2, std::move(v2));
    assert(ret);
    assert(mf.size() == 2);
    assert(std::next(std::next(mf.begin())) == mf.end());
    assert(mf.find("blah") == mf.end());
    assert(!mf.lookup("blah"));
    auto it = mf.find(KEY1);
    assert(it != mf.end());
    assert(uw(it->second) == p1);
    it = mf.find(KEY2);
    assert(it != mf.end());
    assert(uw(it->second) == p2);
    // transfer to variant container
    // generic to container
    Collection<::GHashTable, std::pair<char *, GBExample *>,
        transfer_container_t>
        mc{mf};
    assert(mc.size() == mf.size());
    mc.clear();
    assert(mc.empty());
    mc = mf;
    assert(mc.size() == mf.size());
    assert(uw(mc.lookup(KEY2)) == p2);
    // always down to none
    Collection<::GHashTable, std::pair<char *, GBExample *>, transfer_none_t>
        mn{mf};
    assert(mn.size() == mf.size());
    assert(uw(mn.lookup(KEY2)) == p2);
    mn = mc;
    assert(mn.size() == mf.size());
    assert(uw(mn.lookup(KEY1)) == p1);
  }
}

namespace string_helpers
{
struct MyView
{
  const char *c_str() const { return nullptr; }
  size_t size() const { return 0; }
};

struct MyViewCustom
{};

using cstr = detail::cstring;
using cstrv = detail::cstring_v;

template<typename StringType>
void
check_string()
{
  constexpr bool is_view = std::is_same<cstrv, StringType>::value;
  // construct
  const char *STR = "ab";
  StringType cs(STR);
  assert((cs.data() == STR) == is_view);
  std::string s("cd");
  StringType tcs(s);
  cs = s;
  assert((cs.data() == s.data()) == is_view);
  cstrv csv(cs);
  assert(csv.data() == cs.data());
  tcs = std::move(cs);
  assert(tcs.data() == csv.data());
  cs = std::move(tcs);
  assert(cs.data() == csv.data());
  // ops
  for (auto &c : cs)
    assert(c);
  assert(cs.size() == s.size());
  assert(cs.length() == cs.size());
  assert(!cs.empty());
  assert(cs.at(0) == s.at(0));
  assert(cs[0] == s[0]);
  assert(cs.front() == s.front());
  assert(cs.compare(cs) == 0);
  assert(cs.find(s) == 0);
  assert(cs.find(s, 1) == cs.npos);
  assert(cs.rfind(s) == 0);
  assert(cs.rfind(s, 1) == cs.npos);

  // assign/construct to string
  std::string ns{cs};
  assert(ns == s);
  ns = cs;
  assert(ns == s);
  assert(ns == cs);
  assert(cs == cs);
  ns = "xy";
  assert(cs <= ns);
  assert(cs < ns);
  assert(ns > cs);
  assert(ns >= cs);
  assert(ns != cs);

  // construct using convert
  {
    MyViewCustom cv;
    StringType cs(cv);
    assert(!cs);
  }
  cs.swap(tcs);
  assert(tcs);
  cs = nullptr;
  assert(!cs);

  assert(!s.empty());
  {
    std::map<StringType, int> m;
    cs = s;
    m[cs] = 5;
    assert(m[cs] == 5);
  }
  {
    std::unordered_map<StringType, int> m;
    cs = s;
    m[cs] = 5;
    assert(m[cs] == 5);
  }

#if __cplusplus >= 201703L
  // C++17 specifics
  cs = std::nullopt;
  std::optional<std::string> os;
  cs = os;
  assert(!cs);
  os = s;
  cs = os;
  assert(cs == s);
  os = std::nullopt;
  os = cs;
  assert(os && os.value() == s);
  cs = nullptr;
  assert(!cs);
  os = cs.opt_();
  assert(!os);

  cs = STR;
  assert(cs.find_first_of("b") == 1);
  assert(cs.find_first_of('b') == 1);
#endif
}

} // namespace string_helpers

namespace gi
{
namespace convert
{
template<typename Transfer>
struct converter<string_helpers::MyViewCustom, detail::cstr<Transfer>>
    : public converter_base<string_helpers::MyViewCustom,
          detail::cstr<Transfer>>
{
  static detail::cstr<Transfer> convert(const string_helpers::MyViewCustom &v)
  {
    (void)v;
    return nullptr;
  }
};
} // namespace convert
} // namespace gi

void
test_string()
{
  using namespace string_helpers;
  { // generic
    check_string<cstr>();
    check_string<cstrv>();
  }

  std::string s("xy");
  { // string only
    cstr cs(s, 1, 1);
    {
      MyView cv;
      cstr cs(cv);
      assert(!cs);
    }
    {
      auto *s = (char *)g_malloc0(5);
      cstr cs(s, transfer_full);
      assert(cs.data() == s);
    }
    // ops
    cs = s;
    cs = cs + s;
    assert(cs == s + s);
    assert(cs.size() == 4);
    cs = s;
    cs = s + cs;
    assert(cs == s + s);
    cs.pop_back();
    assert(cs.size() == 3);
    cs.push_back(s.back());
    assert(cs.size() == 4);
    assert(cs == s + s);
    cs = cs.substr(0, 2);
    assert(cs == s);
    cs += s;
    assert(cs == s + s);
    cs.assign(2, 'x');
    assert(cs == "xx");
    cs = 'y' + cs;
    assert(cs == "yxx");
    cs = cs + 'y';
    assert(cs == "yxxy");
    cs.clear();
    assert(!cs);
    cs = s;
    auto sp = unwrap(std::move(cs), transfer_full);
    assert(s == sp);
    assert(sp);
    assert(!cs);
    cs = cstr{sp, transfer_full};
    assert(cs.data() == sp);
    sp = unwrap(std::move(cs), transfer_full);
    assert(!cs);
    cs = wrap(sp, transfer_full);
    assert(cs.data() == sp);

    // C++17 string view
#if __cplusplus >= 201703L
    std::string_view sv(s);
    cs = sv;
    assert(cs.data() != sv.data());
    assert(sv == cs);
    assert(cs == sv);
    sv = "zz";
    assert(cs <= sv);
    assert(cs < sv);
    assert(sv > cs);
    assert(sv >= cs);
    assert(sv != cs);
#endif
  }

  { // view only
    cstrv cv(s);
    assert(cv.data() == s.data());
    cv.remove_prefix(1);
    assert(cv.size() == s.size() - 1);
    assert(cv == s.substr(1));
    const cstrv ccv(s);
    std::string ns(ccv);
  }

  { // combination
    cstr cs("blah");
    cstrv cv(cs);
    assert(cv.data() == cs.data());
    cv = cs;
    assert(cv.data() == cs.data());
    cs = cv;
    assert(cs.data() != cv.data());
  }
}

void
test_exception()
{
  static_assert(traits::is_gboxed<GLib::Error>::value, "");

  GQuark domain = g_quark_from_string("test-domain");
  const char *msg = "exception_test";
  const int code = 42;
  GError *err = g_error_new_literal(domain, code, msg);
  auto w = wrap(err, transfer_full);
  assert(w.matches(domain, code));
  auto what = w.what();
  assert(strstr(what, msg) != NULL);

  auto wr = wrap(err, transfer_none);
  assert(wr.matches(domain, code));

  GLib::Error e{std::move(w)};
  assert(!w);
  assert(e.matches(domain, code));

  check_error(nullptr);
  err = g_error_new_literal(domain, code, msg);
  detail::make_unexpected(err);
  bool value = true;
  auto r = make_result<bool>(value, nullptr);
  static_assert(detail::is_result<decltype(r)>::value, "");
  static_assert(!detail::is_result<bool>::value, "");
  // make sure we have bool result
  auto s = expect(std::move(r));
  static_assert(std::is_same<bool, decltype(s)>::value, "");
  auto &e2 = expect(e);
  assert(&e2 == &e);
}

void
test_enumflag()
{
  const char *name = "EnumValue1";
  const char *nick = "v1";
  auto v = CppEnum::VALUE_1;

  auto w = value_info(v);
  auto w1 = EnumValue<CppEnum>::get_by_name(name);
  auto w2 = EnumValue<CppEnum>::get_by_nick(nick);
  assert(w == w1);
  assert(w1 == w2);
  assert(w1.value_name() == name);
  assert(w1.value_nick() == nick);

  // convenient operations
  auto f = CppFlags::VALUE_0 | CppFlags::VALUE_1;
  f = f & CppFlags::VALUE_1;
  f = ~f;
  f ^= CppFlags::VALUE_0;
}

typedef int(CCallback)(int, float);
typedef detail::callback<int(int, float), transfer_none_t,
    std::tuple<transfer_none_t, transfer_none_t>>
    CppCallback;

void
test_callback()
{
  { // argument selection helper
    auto f = [](int a, int b) { return a - b; };
    using Y = detail::args_index<1, 0>;
    auto rr = detail::apply_with_args<Y>(f, 7, 3, 5);
    assert(rr == -4);
  }
  {
    const char *t = "";
    auto f = [](const char *s) -> decltype(auto) { return *s; };
    static_assert(std::is_reference<decltype(f(t))>::value, "");
    using Y = detail::args_index<0>;
    auto &rr = detail::apply_with_args<Y>(f, t);
    assert(&rr == t);
  }
  {
    using T = typename detail::arg_traits<transfer_none_t>::transfer_type;
    static_assert(std::is_same<transfer_none_t, T>::value, "");
    static_assert(detail::is_simple_cb<std::tuple<transfer_none_t>>::value, "");
    using TC = std::tuple<
        detail::arg_info<transfer_full_t, false, void, detail::args_index<0>>,
        detail::arg_info<transfer_full_t, false, void, detail::args_index<1>>>;
    static_assert(!detail::is_simple_cb<TC>::value, "");
  }

  { // exception helpers
    detail::report_exception<true>(std::runtime_error("fail"), 5, nullptr);
    detail::report_exception(
        std::runtime_error("fail"), 5, (::GError **)(nullptr));
    GError *error = nullptr;
    detail::report_exception(std::runtime_error("fail"), 5, &error);
    assert(error);
    g_error_free(error);
    error = nullptr;
  }

  int calls = 0;

  auto l = [&](int a, float b) {
    ++calls;
    return a * b;
  };
  detail::transform_callback_wrapper<int(int, float)>::with_transfer<false,
      transfer_full_t, transfer_none_t, transfer_none_t>
      x{l};
  x.wrapper(1, 2, &x);
  assert(calls == 1);
  x.take_data(std::make_shared<int>(0));

  auto m = [&](int a, CBoxedExample /*b*/, int &x, int *y) {
    x = 1;
    *y = 2;
    ++calls;
    return a;
  };
  detail::transform_callback_wrapper<void(
      int, CBoxedExample, int &, int *)>::with_transfer<false, transfer_full_t,
      transfer_full_t, transfer_full_t, transfer_none_t, transfer_none_t>
      y{m};
  int p = 0, q = 0;
  y.wrapper(1, gi_cpp_cbexample_new(), &p, &q, &y);
  assert(calls == 2);
  assert(p == 1);
  assert(q == 2);

  auto w = wrap(gi_cpp_example_new(), transfer_full);
  auto w2 = wrap(gi_cpp_example_new(), transfer_full);
  char str[] = "blah";
  auto n = [&](GBoxedExample /*b*/, Derived &ob, CppEnum *e, gi::cstring_v &s,
               gpointer &vb, GLib::Error * /*error*/) {
    ++calls;
    if (e)
      *e = CppEnum::VALUE_1;
    ob = w2;
    s = str;
    vb = str;
    return w;
  };
  detail::transform_callback_wrapper<Derived(GBoxedExample, Derived &,
      CppEnum *, gi::cstring_v &, gpointer &, GLib::Error *)>::
      with_transfer<false, transfer_full_t, transfer_full_t, transfer_full_t,
          transfer_none_t, transfer_none_t, transfer_none_t, transfer_full_t>
          z{n};
  CDerived *ob{};
  CEnum e{};
  char *s{};
  gpointer vb{};
  auto r = z.wrapper(gi_cpp_gbexample_new(), &ob, &e, &s, &vb, nullptr, &z);
  assert(calls == 3);
  assert(r == w.gobj_());
  assert(refcount(r) == 2);
  g_object_unref(r);
  assert(e == CEnum::ENUM_VALUE_1);
  assert(s == str);
  assert(vb == s);
  assert(ob == w2.gobj_());
  assert(refcount(ob) == 2);
  g_object_unref(ob);

  // extended case
  {
    using CLocalCallback_CF_CType = int (*)(int, gpointer);
    struct CLocalCallback_CF_Trait
    {
      using handler_cb_type = CLocalCallback_CF_CType;
      static auto handler(int x, handler_cb_type cb, gpointer ud)
      {
        return cb(x, ud);
      };
    };
    using CppLocalCallback = detail::callback<int(int), transfer_none_t,
        std::tuple<transfer_none_t>>;
    int xx = 0;
    auto nx = [&](GBoxedExample /*b*/, Derived &ob, CppEnum *e, int x,
                  CppLocalCallback cb, GLib::Error * /*error*/) {
      ++calls;
      xx = x;
      ob = w2;
      if (e)
        *e = CppEnum::VALUE_1;
      auto r = cb(x);
      // supplied cb below is identity function
      assert(r == x);
      return w;
    };
    detail::transform_callback_wrapper<Derived(GBoxedExample, Derived &,
                                           CppEnum *, int, CppLocalCallback,
                                           GLib::Error *),
        CDerived *(GBExample *, CDerived **, CEnum *, int,
            CLocalCallback_CF_CType, gpointer,
            ::GError **)>::with_transfer<false, transfer_none_t,
        detail::arg_info<transfer_full_t, false, void, detail::args_index<0>>,
        detail::arg_info<transfer_none_t, false, void, detail::args_index<1>>,
        detail::arg_info<transfer_full_t, false, void, detail::args_index<2>>,
        detail::arg_info<transfer_full_t, false, void, detail::args_index<3>>,
        detail::arg_info<transfer_full_t, false, CLocalCallback_CF_Trait,
            detail::args_index<4, 5>>,
        detail::arg_info<transfer_full_t, false, void, detail::args_index<6>>>
        zx{nx};
    CDerived *obx{};
    CEnum ex{};
    auto cb = [](int v, gpointer ud) {
      if (ud)
        *(int *)(ud) = v;
      return v;
    };
    int oi = 0;
    auto r = zx.wrapper(
        gi_cpp_gbexample_new(), &obx, &ex, 5, cb, (gpointer)&oi, nullptr, &zx);
    // cpp callback should have been called
    assert(calls == 4);
    assert(xx == 5);
    assert(r == w.gobj_());
    assert(obx != nullptr);
    assert(obx == w2.gobj_());
    assert(e == CEnum::ENUM_VALUE_1);
    // which in turn should have called the supplied callback
    assert(oi == 5);
    //
    // destroy_notify parameter
    bool called = false;
    int INTVAL = 7;
    auto ndx = [&](CppLocalCallback cb) {
      called = true;
      int x = INTVAL;
      auto r = cb(x);
      // supplied cb below is identity function
      assert(r == x);
      return w;
    };
    detail::transform_callback_wrapper<void(CppLocalCallback),
        void(CLocalCallback_CF_CType, gpointer,
            GDestroyNotify)>::with_transfer<false, transfer_none_t,
        detail::arg_info<transfer_full_t, false, CLocalCallback_CF_Trait,
            detail::args_index<0, 1, 2>>>
        zdx{ndx};
    auto dn = [](gpointer ud) { *(int *)ud = 0; };
    zdx.wrapper(cb, (gpointer)&oi, dn, &zdx);
    assert(called);
    // destroy notify called last
    assert(oi == 0);
    //
    // void return case, and sized array out
    char *sx{};
    gpointer vbx{};
    const int V = 56;
    int *a_data{};
    int a_size = 0;
    using TestColType = gi::Collection<gi::DSpan, int, transfer_full_t>;
    auto nnx = [&](gi::cstring_v &s, gpointer &vb, CppLocalCallback cb,
                   TestColType &d) {
      ++calls;
      s = str;
      vb = str;
      auto r = cb(V);
      // supplied cb below is identity function
      assert(r == V);
      d.push_back(4);
    };
    detail::transform_callback_wrapper<void(gi::cstring_v & s, gpointer & vb,
                                           CppLocalCallback, TestColType &),
        void(char **, gpointer *, CLocalCallback_CF_CType, gpointer, int **,
            int *)>::with_transfer<false, transfer_full_t,
        detail::arg_info<transfer_none_t, false, void, detail::args_index<0>>,
        detail::arg_info<transfer_none_t, false, void, detail::args_index<1>>,
        detail::arg_info<transfer_full_t, false, CLocalCallback_CF_Trait,
            detail::args_index<2, 3>>,
        detail::arg_info<transfer_full_t, false, void,
            detail::args_index<4, 5>>>
        zzx{nnx};
    zzx.wrapper(&sx, &vbx, cb, (gpointer)&oi, &a_data, &a_size, &zzx);
    assert(calls == 5);
    assert(sx == str);
    assert(vbx == str);
    // cpp callback should have been called
    assert(oi == V);
    // handle returned array
    assert(a_size == 1);
    assert(*a_data == 4);
    g_free(a_data);
  }

  { // compilation checks
    const CppCallback cppcb(l);
    auto uw{unwrap(cppcb, gi::scope_async)};
    delete uw;
    auto uw2{unwrap(cppcb, gi::scope_notified)};
    delete uw2;
  }
}

void
test_value()
{
  using GObject_::Value;
  static_assert(traits::gtype<Value>::value, "");
  assert(g_type_is_a(GI_CPP_TYPE_ENUM, G_TYPE_ENUM));
  assert(g_type_is_a(GI_CPP_TYPE_FLAGS, G_TYPE_FLAGS));

  // detail helper
  {
    detail::Value v(5);
    auto vs = detail::transform_value<std::string>(&v);
    assert(vs == "5");
    detail::Value v2("ab");
    auto w = detail::get_value<std::string>(&v2);
    assert(w == "ab");
  }

  // main Value
  {
    Value v;
    v.init(G_TYPE_STRING);
    v.set_value("ab");
    auto w = v.get_value<std::string>();
    assert(w == "ab");
  }
  {
    Value v(0);
    auto w = v.get_value<int>();
    assert(w == 0);
    v.set_value(5);
    w = v.get_value<int>();
    assert(w == 5);
  }
  {
    Value v{std::string()};
    auto w = v.get_value<std::string>();
    assert(w.empty());
  }
  {
    Value v((double)1.0);
    auto w = v.get_value<double>();
    assert(w == 1.0);
  }
  {
    Value v('a');
    auto w = v.get_value<char>();
    assert(w == 'a');
  }
  {
    CDerived *ob = gi_cpp_example_new();
    auto wob = wrap(ob, transfer_none);
    Value v(wob);
    assert(refcount(ob) == 3);
    auto w2 = v.get_value<Derived>();
    assert(w2 == wob);
    assert(refcount(ob) == 4);
    auto wr = wrap(v.gobj_(), transfer_none);
    assert(wr == v);
    assert(refcount(ob) == 4);
    g_object_unref(ob);
    // others clean up by magic
  }
  {
    Value v(GBoxedExample{});
    auto w = v.get_value<GBoxedExample>();
    assert(w.gobj_() == nullptr);
    auto wr = v.get_value<GBoxedExampleRef>();
    assert(wr.gobj_() == nullptr);
  }
  {
    Value v(CppEnum::VALUE_0);
    auto w = v.get_value<CppEnum>();
    assert(w == CppEnum::VALUE_0);
    Value v1(v.copy_());
    assert(v != v1);
  }
  {
    Value v(CppFlags::VALUE_1);
    auto w = v.get_value<CppFlags>();
    assert(w == CppFlags::VALUE_1);
    Value v1(v.copy_());
    assert(v != v1);
  }
  { // test function; auto conversion
    auto tf = [](Value v, GType t) { assert(G_VALUE_TYPE(v.gobj_()) == t); };
    tf(5, G_TYPE_INT);
    tf("ab", G_TYPE_STRING);
    tf(GBoxedExample(), GI_CPP_TYPE_BOXED_EXAMPLE);
  }
  { // wrapping
    GValue *v = (GValue *)1;
    auto w = wrap(v, transfer_none);
    auto v1 = unwrap(w, transfer_none);
    assert(v1 = v);
  }
}

void
test_property()
{
  CDerived *ob = gi_cpp_example_new();
  // take ownership
  Derived w = wrap(ob, transfer_full);

  // manual
  w.set_property(NAME_NUMBER, 5);
  assert(w.get_property<int>(NAME_NUMBER) == 5);
  w.set_property(NAME_PRESENT, true);
  assert(w.get_property<bool>(NAME_PRESENT) == true);
  const char *str = "value";
  w.set_property(NAME_DATA, str);
  assert(w.get_property<std::string>(NAME_DATA) == str);
  w.set_property(NAME_OBJECT, w);
  auto w2 = w.get_property<Derived>(NAME_OBJECT);
  assert(w2 == w);
  assert(refcount(ob) == 3);
  // remove cycle ref held within ob
  w.set_property(NAME_OBJECT, Derived());
  assert(refcount(ob) == 2);
  w.set_property(NAME_ENUM, CppEnum::VALUE_1);
  assert(w.get_property<CppEnum>(NAME_ENUM) == CppEnum::VALUE_1);

  // multiple props
  double fnum = 5.2;
  w.set_properties(NAME_NUMBER, 10, NAME_FNUMBER, fnum, NAME_PRESENT, FALSE);
  assert(w.get_property<int>(NAME_NUMBER) == 10);
  assert(w.get_property<double>(NAME_FNUMBER) == fnum);
  assert(w.get_property<bool>(NAME_PRESENT) == false);

  // generic value
#ifdef GI_GOBJECT_PROPERTY_VALUE
  w.get_property(NAME_NUMBER);
#endif

  // via proxy
  fnum = 6.2;
  Derived w3 = wrap(gi_cpp_example_new(), transfer_full);
  w.property_number().set(7);
  w.property_fnumber().set(fnum);
  w.property_data().set(str);
  w.property_object().set(w3);
  w.property_present().set(true);
  w.property_choice().set(CppEnum::VALUE_0);
  w.property_flags().set(CppFlags::VALUE_0);
  // boxed property
  GQuark domain = g_quark_from_string("test-domain");
  auto error = GLib::Error::new_literal(domain, 1, "msg");
  w.property_error().set(error.copy());
  w.property_error().set(std::move(error));

  const Derived cw = w;
  assert(cw.property_number().get() == 7);
  assert(cw.property_fnumber().get() == fnum);
  assert(cw.property_data().get() == str);
  assert(cw.property_object().get() == w3);
  assert(cw.property_data().get() == str);
  assert(cw.property_choice().get() == CppEnum::VALUE_0);
  assert(cw.property_flags().get() == CppFlags::VALUE_0);

  // property queries
  auto pspec = cw.find_property(NAME_NUMBER);
  assert(pspec);
  assert(pspec.get_name() == NAME_NUMBER);

  auto pspecs = cw.list_properties();
  assert(pspecs.size() == PROP_LAST);
}

void
test_signal()
{
  // example values
  double v_d = 2.7;
  int v_i = 4;
  std::string v_s = "values";
  bool v_b = true;
  CppEnum v_e = CppEnum::VALUE_1;
  CppFlags v_f = CppFlags::VALUE_1;
  Derived v_o = wrap(gi_cpp_example_new(), transfer_full);

  // object to signal on
  CDerived *ob = gi_cpp_example_new();
  // take ownership
  Derived w = wrap(ob, transfer_full);

  // lambda callbacks
  int recv = 0;
  int ret = 7;
  auto l1 = [&](Derived src, GObject_::Object o, bool b, bool c,
                const std::string &s) -> int {
    assert(src == w);
    assert(o == v_o);
    assert(s == v_s);
    assert(b == v_b);
    assert(c == !b);
    ++recv;
    return ret;
  };
  w.signal_to_int().connect(l1);
  auto r = w.signal_to_int().emit(v_o, v_b, !v_b, v_s);
  assert(recv == 1);
  assert(r == ret);

  // another signal
  auto l2 = [&](Derived src, int i, gint64 ll) -> std::string {
    assert(src == w);
    ++recv;
    return std::to_string(i + ll);
  };
  w.signal_to_string().connect(l2);
  gint64 ll = 4;
  auto sr = w.signal_to_string().emit(v_i, ll);
  assert(recv == 2);
  assert(std::stoi(sr) == v_i + ll);

  // and another
  auto l3 = w.signal_to_void().slot(
      [&](Derived src, double d, CppEnum e, CppFlags f) {
        assert(src == w);
        assert(d == v_d);
        assert(e == v_e);
        assert(f == v_f);
        ++recv;
      });
  auto id = w.signal_to_void().connect(l3);
  w.signal_to_void().emit(v_d, v_e, v_f);
  assert(recv == 3);

  auto conn = make_connection(id, l3, w);
  assert(conn.connected());
  {
    // safe to disconnect twice (or attempt so)
    GObject_::SignalScopedConnection sconn(conn), sconn2(conn);
    assert(sconn.connected());
    assert(sconn2.connected());
  }

  assert(!conn.connected());
  w.signal_to_void().emit(v_d, v_e, v_f);
  assert(recv == 3);

  // signal collection and output argument
  auto l4 = [&](Derived src, unsigned &o, auto pa, auto la) {
    assert(src == w);
    o = pa.size() + std::distance(la.begin(), la.end());
  };
  gi::Collection<::GPtrArray, char *, gi::transfer_full_t> pa;
  gi::Collection<::GSList, char *, gi::transfer_full_t> la;
  pa.push_back("blah");
  la = pa;
  assert(!la.empty());
  la.push_back("foo");
  w.signal_to_output_int().connect(l4);
  unsigned result = 0;
  w.signal_to_output_int().emit(result, pa, nullptr);
  assert(result == pa.size());
  w.signal_to_output_int().emit(result, nullptr, la);
  assert(result == 2);

  // assert exception helper
  auto assert_exc = [](const std::function<void()> &func) {
    bool exc = false;
    try {
      func();
    } catch (std::exception &) {
      exc = true;
    }
    assert(exc);
  };

  // check connect check
  assert_exc(
      [&]() { w.connect<std::string(Derived, int, gint64)>("to_void", l2); });
  // check property value conversion
  assert_exc([&]() { w.set_property<std::string>(NAME_OBJECT, "blah"); });
}

// ExampleInterface

class ExampleInterface : public gi::InterfaceBase
{
public:
  typedef GICppExampleItf BaseObjectType;
  static GType get_type_() G_GNUC_CONST
  {
    return gi_cpp_example_interface_get_type();
  }
};

class ExampleInterfaceDef
{
  typedef ExampleInterfaceDef self;

public:
  typedef ExampleInterface instance_type;
  typedef GICppExampleInterface interface_type;

  using GI_MEMBER_CHECK_CONFLICT(vmethod) = self;
  using GI_MEMBER_CHECK_CONFLICT(imethod) = self;

  struct TypeInitData;

protected:
  ~ExampleInterfaceDef() = default;

  static void interface_init(gpointer iface, gpointer /*data*/);

  virtual int vmethod_(int a) = 0;
  virtual int imethod_(int a) = 0;
};

using ExampleInterfaceImpl = gi::detail::InterfaceImpl<ExampleInterfaceDef>;

class ExampleInterfaceClassImpl
    : public gi::detail::InterfaceClassImpl<ExampleInterfaceImpl>
{
  friend class ExampleInterfaceDef;
  typedef ExampleInterfaceImpl self;
  typedef gi::detail::InterfaceClassImpl<ExampleInterfaceImpl> super;

protected:
  using super::super;

  int vmethod_(int a) override
  {
    auto _struct = get_struct_();
    return _struct->vmethod(this->gobj_(), a);
  }

  int imethod_(int a) override
  {
    auto _struct = get_struct_();
    return _struct->imethod(this->gobj_(), a);
  }
};

struct ExampleInterfaceDef::TypeInitData
{
  GI_MEMBER_DEFINE(ExampleInterfaceClassImpl, vmethod)
  GI_MEMBER_DEFINE(ExampleInterfaceClassImpl, imethod)

  template<typename SubClass>
  constexpr static TypeInitData factory()
  {
    using DefData = detail::DefinitionData<SubClass, TypeInitData>;
    return {GI_MEMBER_HAS_DEFINITION(SubClass, DefData, vmethod),
        GI_MEMBER_HAS_DEFINITION(SubClass, DefData, imethod)};
  }
};

void
ExampleInterfaceDef::interface_init(gpointer iface, gpointer data)
{
  auto init_data = GI_MEMBER_INIT_DATA(TypeInitData, data);
  auto itf = (interface_type *)(iface);

  if (init_data.vmethod)
    itf->vmethod = gi::detail::method_wrapper<self, int (*)(int),
        transfer_full_t, std::tuple<transfer_none_t>>::wrapper<&self::vmethod_>;
  if (init_data.imethod)
    itf->imethod = gi::detail::method_wrapper<self, int (*)(int),
        transfer_full_t, std::tuple<transfer_none_t>>::wrapper<&self::imethod_>;
}

// PropertyInterface
class PropertyInterface : public gi::InterfaceBase
{
public:
  typedef GICppPropertyItf BaseObjectType;
  static GType get_type_() G_GNUC_CONST
  {
    return gi_cpp_property_interface_get_type();
  }
};

class PropertyInterfaceDef
{
  typedef PropertyInterfaceDef self;

public:
  typedef PropertyInterface instance_type;
  typedef GICppPropertyInterface interface_type;

protected:
  static void interface_init(gpointer iface, gpointer /*data*/)
  {
    auto itf = (interface_type *)(iface);
    (void)itf;
  }
};

using PropertyInterfaceImpl = gi::detail::InterfaceImpl<PropertyInterfaceDef>;

class PropertyInterfaceClassImpl
    : public gi::detail::InterfaceClassImpl<PropertyInterfaceImpl>
{
  typedef PropertyInterfaceImpl self;
  typedef gi::detail::InterfaceClassImpl<PropertyInterfaceImpl> super;

protected:
  using super::super;
};

class DerivedClassDef
{
  typedef DerivedClassDef self;

public:
  typedef Derived instance_type;
  typedef GICppExampleClass class_type;

  struct TypeInitData;

  using GI_MEMBER_CHECK_CONFLICT(vmethod) = self;
  using GI_MEMBER_CHECK_CONFLICT(cmethod) = self;

protected:
  ~DerivedClassDef() = default;

  static void class_init(gpointer g_class, gpointer class_data_factory);

  virtual int vmethod_(int a, int b) = 0;
  virtual int cmethod_(int a, int b) = 0;
};

GI_CLASS_IMPL_BEGIN

class DerivedClass
    : public gi::detail::ClassTemplate<DerivedClassDef,
          GObject_::impl::internal::ObjectClass, ExampleInterfaceClassImpl>
{
  friend class DerivedClassDef;
  typedef DerivedClass self;
  typedef gi::detail::ClassTemplate<DerivedClassDef,
      GObject_::impl::internal::ObjectClass, ExampleInterfaceClassImpl>
      super;

public:
  typedef ExampleInterfaceClassImpl ExampleInterface_type;

private:
  // make local helpers private
  using super::get_struct_;
  using super::gobj_;

protected:
  GI_DISABLE_DEPRECATED_WARN_BEGIN
  using super::super;
  GI_DISABLE_DEPRECATED_WARN_END

  virtual int vmethod_(int a, int b) override
  {
    auto _struct = get_struct_();
    return _struct->vmethod(gobj_(), a, b);
  }

  virtual int cmethod_(int a, int b) override
  {
    auto _struct = get_struct_();
    return _struct->cmethod(gobj_(), a, b);
  }
};

struct DerivedClassDef::TypeInitData
{
  GI_MEMBER_DEFINE(DerivedClass, vmethod)
  GI_MEMBER_DEFINE(DerivedClass, cmethod)

  template<typename SubClass>
  constexpr static TypeInitData factory()
  {
    using DefData = detail::DefinitionData<SubClass, TypeInitData>;
    return {GI_MEMBER_HAS_DEFINITION(SubClass, DefData, vmethod),
        GI_MEMBER_HAS_DEFINITION(SubClass, DefData, cmethod)};
  }
};

void
DerivedClassDef::class_init(gpointer g_class, gpointer class_data_factory)
{
  auto class_data = GI_MEMBER_INIT_DATA(TypeInitData, class_data_factory);
  GICppExampleClass *klass = (GICppExampleClass *)g_class;

  if (class_data.vmethod)
    klass->vmethod = gi::detail::method_wrapper<self, int (*)(int, int),
        transfer_full_t,
        std::tuple<transfer_none_t, transfer_none_t>>::wrapper<&self::vmethod_>;
  if (class_data.cmethod)
    klass->cmethod = gi::detail::method_wrapper<self, int (*)(int, int),
        transfer_full_t,
        std::tuple<transfer_none_t, transfer_none_t>>::wrapper<&self::cmethod_>;
  // local compile check
  (void)gi::detail::method_wrapper<self, int (*)(int, int),
      std::nullptr_t>::wrapper<&self::cmethod_>;
}

GI_CLASS_IMPL_END

using DerivedImpl = gi::detail::ObjectImpl<Derived, DerivedClass>;

template<typename T>
class custom_property : public gi::property<T>
{
public:
  using super = gi::property<T>;
  using super::super;

  using handler = std::function<void(const T &)>;
  handler handler_;

  void set_property(const GValue *value) override
  {
    super::set_property(value);
    if (handler_)
      handler_(this->get_value());
  }
};

class UserDerived : public DerivedImpl
{
public:
  // possible conflict for vmethod
  // so we specify the override situation explicity
  struct DefinitionData
  {
    GI_DEFINES_MEMBER(DerivedClassDef, vmethod, true)
    GI_DEFINES_MEMBER(ExampleInterfaceDef, vmethod, true)
  };

  UserDerived()
      : DerivedImpl(this), prop_int_set(this, "prop_int_set", "prop_int_set",
                               "prop_int_set", 0, 10, 0),
        prop_bool_override(this, NAME_PRESENT)
  {
    // check detection of method definitions
    constexpr auto class_def =
        DerivedClassDef::TypeInitData::factory<UserDerived>();
    static_assert(class_def.vmethod.value, "");
    static_assert(!class_def.cmethod.value, "");
    constexpr auto itf_def =
        ExampleInterfaceDef::TypeInitData::factory<UserDerived>();
    static_assert(itf_def.vmethod.value, "");
    static_assert(!itf_def.imethod.value, "");
  }

  int vmethod_(int a, int b) override { return a * b; }

  int pvmethod(int a, int b) { return DerivedImpl::vmethod_(a, b); }

  int vmethod_(int a) override { return 2 * a; }

  int pivmethod(int a) { return ExampleInterface_type::vmethod_(a); }

  custom_property<int> prop_int_set;
  custom_property<bool> prop_bool_override;
};

class UserDerived2 : public DerivedImpl
{
public:
  // possible conflict for vmethod
  // so we specify the override situation explicity
  struct DefinitionData
  {
    GI_DEFINES_MEMBER(DerivedClassDef, vmethod, true)
    GI_DEFINES_MEMBER(ExampleInterfaceDef, vmethod, false)
  };

  UserDerived2() : DerivedImpl(this)
  {
    // check detection of method definitions
    constexpr auto x = DerivedClassDef::TypeInitData::factory<UserDerived2>();
    static_assert(x.vmethod.value, "");
    static_assert(x.cmethod.value, "");
    constexpr auto itf_def =
        ExampleInterfaceDef::TypeInitData::factory<UserDerived2>();
    static_assert(!itf_def.vmethod.value, "");
    static_assert(itf_def.imethod.value, "");
  }

  int vmethod_(int a, int b) override { return a * b; }

  int cmethod_(int a, int b) override { return a * b; }

  int imethod_(int a) override { return 5 * a; }
};

GI_DISABLE_DEPRECATED_WARN_BEGIN
class OldUserDerived : public DerivedImpl
{
public:
  OldUserDerived() : DerivedImpl(typeid(*this)) {}
};
GI_DISABLE_DEPRECATED_WARN_END

static const int DEFAULT_PROP_INT = 7;

class UserObject : public ExampleInterfaceImpl,
                   public PropertyInterfaceImpl,
                   public GObject_::impl::ObjectImpl
{
public:
  UserObject()
      : ObjectImpl(this, {}, {{NAME_INUMBER, {&prop_itf_int, nullptr}}}),
        signal_demo_(this, "demo"), prop_itf_int(this, NAME_INUMBER),
        prop_int(
            this, "prop_int", "prop_int", "prop_int", 0, 10, DEFAULT_PROP_INT),
        prop_bool(this, "prop_bool", "prop_bool", "prop_bool", false),
        prop_str(this, "prop_str", "prop_str", "prop_str", ""),
        prop_object(this, "prop_object", "prop_object", "prop_object"),
        prop_enum(this, "prop_enum", "prop_enum", "prop_enum")
  {}

  int vmethod_(int a) override { return 5 * a; }
  int imethod_(int a) override { return 7 * a; }

  gi::signal<void(Object, int)> signal_demo_;
  gi::property<int> prop_itf_int;
  gi::property<int> prop_int;
  gi::property<bool> prop_bool;
  gi::property<std::string> prop_str;
  gi::property<Object> prop_object;
  gi::property<CppEnum> prop_enum;
};

class UserCObject : public ExampleInterfaceImpl,
                    public PropertyInterfaceImpl,
                    public GObject_::impl::ObjectImpl
{
  using self_type = UserCObject;

public:
  UserCObject(const InitData &id) : ObjectImpl(this, id) { assert(id); }

  static GType get_type_()
  {
    return register_type_<UserCObject>("UserCObject", 0,
        {ExampleInterfaceImpl::interface_init_data<UserCObject>(),
            {PropertyInterfaceImpl::register_interface, nullptr}},
        {{&self_type::prop_itf_int, NAME_INUMBER},
            {&self_type::prop_int, "prop_int", "prop_int", "prop_int", 0, 10,
                DEFAULT_PROP_INT}},
        {{&self_type::signal_demo_, "demo"}});
  }

  int vmethod_(int a) override { return 5 * a; }
  int imethod_(int a) override { return 7 * a; }

  // details provided in get_type_
  gi::signal<void(Object, int)> signal_demo_{this, "demo"};
  gi::property<int> prop_itf_int{this, NAME_INUMBER};
  gi::property<int> prop_int{this, "prop_int"};
};

void
test_impl()
{
  {
    // base object implements interface
    UserDerived u, v;
    assert(u.gobj_type_() == v.gobj_type_());
    assert(u.pvmethod(2, 3) == 5);
    auto klass =
        G_TYPE_INSTANCE_GET_CLASS(u.gobj_(), u.gobj_type(), GICppExampleClass);
    assert(klass->vmethod(u.gobj_(), 2, 3) == 6);
    // no cmethod
    assert(!klass->cmethod);
    // interface
    assert(u.pivmethod(4) == 6);
    auto iface = G_TYPE_INSTANCE_GET_INTERFACE(
        u.gobj_(), ExampleInterface::get_type_(), GICppExampleInterface);
    assert(iface->vmethod((GICppExampleItf *)u.gobj_(), 4) == 8);
    assert(!iface->imethod);
    // implemented here
    UserDerived2 w;
    assert(w.imethod_(2) == 10);
    // compilation check
    assert(u.gobj_klass()->vmethod);
    { // check custom property (on a non-Object derived class)
      int value = 0;
      auto &tp = u.prop_int_set;
      tp.handler_ = [&value](int setv) { value = setv; };
      auto proxy = tp.get_proxy();
      //
      const int NEW_VALUE = 7;
      proxy.set(NEW_VALUE);
      assert(value == NEW_VALUE);
      assert(tp.get_value() == NEW_VALUE);
      tp.handler_ = nullptr;
    }
    { // likewise on overriden property
      auto &tp = u.prop_bool_override;
      auto proxy = tp.get_proxy();
      proxy.set(false);
      assert(!proxy.get());
      bool value = false;
      tp.handler_ = [&value](bool setv) { value = setv; };
      const bool NEW_VALUE = true;
      //
      proxy.set(NEW_VALUE);
      assert(value == NEW_VALUE);
      assert(tp.get_value() == NEW_VALUE);
      tp.handler_ = nullptr;
    }
  }

  {
    // vanilla object
    UserObject u, v;
    assert(u.gobj_type_() == v.gobj_type_());
    auto iface = G_TYPE_INSTANCE_GET_INTERFACE(
        u.gobj_(), ExampleInterface::get_type_(), GICppExampleInterface);
    assert(iface);
    assert(iface->vmethod((GICppExampleItf *)u.gobj_(), 4) == 20);
    // signal
    int i = 0, j = 5;
    u.signal_demo_.connect([&i](GObject_::Object, int in) -> void { i = in; });
    u.signal_demo_.emit(j);
    assert(i == j);
    // properties
    {
      // also check notification
      bool notified = false;
      auto proxy = u.prop_int.get_proxy();
      auto l = proxy.signal_notify().slot(
          [&notified](
              GObject_::Object, GObject_::ParamSpec) { notified = true; });
      GObject_::SignalScopedConnection conn =
          make_connection(proxy.signal_notify().connect(l), l, u);
      assert(u.prop_int == DEFAULT_PROP_INT);
      u.prop_int = j;
      assert(notified);
      notified = false;
      assert(proxy.get() == j);
      proxy.set(2 * j);
      assert(u.prop_int == 2 * j);
      assert(notified);
    }
    {
      auto proxy = u.prop_bool.get_proxy();
      u.prop_bool = true;
      assert(proxy.get() == true);
      proxy.set(false);
      assert(u.prop_bool == false);
    }
    {
      auto proxy = u.prop_str.get_proxy();
      const std::string strv = "value";
      u.prop_str = strv;
      assert(proxy.get() == strv);
      proxy.set(strv + strv);
      assert(u.prop_str.get_value() == (strv + strv));
    }
    {
      auto proxy = u.prop_object.get_proxy();
      u.prop_object = v;
      assert(proxy.get() == v);
      proxy.set(nullptr);
      assert(u.prop_object.get_value() == nullptr);
    }
    {
      CppEnum v1 = CppEnum::VALUE_1, v0 = CppEnum::VALUE_0;
      auto proxy = u.prop_enum.get_proxy();
      u.prop_enum = v1;
      assert(proxy.get() == v1);
      proxy.set(v0);
      assert(u.prop_enum == v0);
    }
    {
      const int val = 8;
      u.prop_itf_int = val;
      auto proxy = u.prop_itf_int.get_proxy();
      assert(proxy.get() == val);
      proxy.set(val);
      assert(u.prop_itf_int == val);
    }

    {
      // local properties
      property_read<bool> p{&u, "p", "p", "p", false};
      p.get_proxy();

      property_write<bool> q{&u, "p", "p", "p", false};
      q.get_proxy();
    }
  }

  {
    // create non-stack
    auto u = gi::make_ref<UserObject>();
    assert(u->list_properties().size() > 0);
    // cast works ok
    GObject_::Object v = u;
    assert(refcount(v.gobj_()) == 2);
    auto l = [](GObject_::Object) {};
    l(u);
    auto u2 = ref_ptr_cast<UserObject>(v);
    assert(u2 == u);
    assert(refcount(v.gobj_()) == 3);
    auto u3 = u2;
    assert(refcount(v.gobj_()) == 4);
    auto u4 = std::move(u3);
    assert(refcount(v.gobj_()) == 4);
    assert(!u3);

    auto um = gi::make_ref<UserObject, gi::construct_cpp_t>();
    assert(um->gobj_type_() == u->gobj_type_());
  }

  {
    // similar for C-style
    auto u = gi::make_ref<UserCObject>("prop_int", DEFAULT_PROP_INT / 2);
    assert(u->list_properties().size() > 0);
    assert(u->prop_int.get_value() == DEFAULT_PROP_INT / 2);

    auto um = gi::make_ref<UserCObject>();
    assert(um->prop_int.get_value() == DEFAULT_PROP_INT);
    assert(um->gobj_type_() == u->gobj_type_());
    assert(um->gobj_type_() == gi::register_type<UserCObject>());
  }
}

int
main(int argc, char *argv[])
{
  (void)argc;
  (void)argv;

  test_trait();
  test_wrap();
  test_helpers();
  test_collection();
  test_string();
  test_exception();
  test_enumflag();
  test_value();
  test_property();
  test_signal();
  test_callback();
  test_impl();

  return 0;
}
