#ifndef GI_BOXED_HPP
#define GI_BOXED_HPP

#include "gi_inc.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace repository
{
// specialize to enable copyable boxed wrapper
// ideally only used in case where the copy is (equivalently);
// * cheap
// * does not change the underlying pointer
// * refcount based
// * has no semantics effects
//   (e.g. GstMiniObject refcount affects writable)
template<typename CType>
struct enable_boxed_copy :
#if GI_ENABLE_BOXED_COPY_ALL
    public std::true_type
#else
    public std::false_type
#endif
{};

} // namespace repository

namespace detail
{
// class tags
class Boxed
{};
class CBoxed : public Boxed
{};
class GBoxed : public Boxed
{};

template<typename CType>
class SharedWrapper
{
public:
  typedef SharedWrapper self;

protected:
  CType *data_ = nullptr;

public:
  CType *gobj_() { return data_; }
  const CType *gobj_() const { return data_; }

  explicit operator bool() const { return (bool)data_; }

  bool operator==(const SharedWrapper &other) const
  {
    return data_ == other.data_;
  }

  bool operator==(std::nullptr_t o) const { return data_ == o; }

  bool operator!=(const SharedWrapper &other) const
  {
    return data_ != other.data_;
  }

  bool operator!=(std::nullptr_t o) const { return data_ != o; }

  CType *release_()
  {
    auto tmp = this->data_;
    this->data_ = nullptr;
    return tmp;
  }
};

template<typename CppType, typename CType>
struct GBoxedFuncs
{
  static CType *copy(const void *data)
  {
    return (CType *)g_boxed_copy(CppType::get_type_(), data);
  }
  static void free(void *data) { g_boxed_free(CppType::get_type_(), data); }
};

struct CBoxedFuncsBase
{
  static void free(void *data) { g_free(data); }
};

template<typename CppType, typename CType>
struct CBoxedFuncs : CBoxedFuncsBase
{
  static CType *copy(const void *data)
  {
#if GLIB_CHECK_VERSION(2, 68, 0)
    return (CType *)g_memdup2(data, sizeof(CType));
#else
    return (CType *)g_memdup(data, sizeof(CType));
#endif
  }
};

template<typename CType, typename Funcs, typename TagType>
class BoxedWrapper : public SharedWrapper<CType>, public TagType
{
  typedef BoxedWrapper self;

protected:
  static void _deleter(CType *obj, ...)
  {
    if (obj)
      Funcs::free(obj);
  }

  static CType *_copy(const CType *obj)
  {
    return obj ? Funcs::copy(obj) : nullptr;
  }

public:
  typedef CType BaseObjectType;

  BoxedWrapper(CType *obj = nullptr) noexcept { this->data_ = obj; }

  CType *gobj_copy_() const { return _copy(this->gobj_()); }

  // resulting casted type determines ownership
  template<typename Cpp>
  static Cpp wrap(const typename Cpp::BaseObjectType *obj)
  {
    static_assert(sizeof(Cpp) == sizeof(self), "type wrap not supported");
    static_assert(std::is_base_of<self, Cpp>::value, "type wrap not supported");
    BoxedWrapper w(const_cast<typename Cpp::BaseObjectType *>(obj));
    return std::move(*static_cast<Cpp *>(&w));
  }
};

// in templates below, Base should be a subclass of BoxedWrapper
// so we re-use the members it provides, as well as the wrap template
// to avoid ambiguous reference to the latter
// (if BoxedWrapper were inherited from again)

// the nullptr_t constructor (indirectly) supports `= nullptr` (assignment)

template<typename CppType, typename CType>
using GBoxedWrapperBase =
    BoxedWrapper<CType, GBoxedFuncs<CppType, CType>, GBoxed>;

// assuming Base has suitable members such as above BoxedWrapper
template<typename Base>
class MoveWrapper : public Base
{
  typedef Base super;

public:
  using super::super;

  ~MoveWrapper() { this->_deleter(this->data_, static_cast<Base *>(this)); }

  MoveWrapper(const MoveWrapper &) = delete;
  MoveWrapper &operator=(const MoveWrapper &) = delete;

  MoveWrapper(MoveWrapper &&o) noexcept { *this = std::move(o); }
  MoveWrapper &operator=(MoveWrapper &&o) noexcept
  {
    if (this != &o) {
      this->_deleter(this->data_, static_cast<Base *>(this));
      (Base &)(*this) = std::move(o);
      o.data_ = nullptr;
    }
    return *this;
  }
};

template<typename Base>
class CopyWrapper : public MoveWrapper<Base>
{
  typedef MoveWrapper<Base> super;

public:
  using super::super;

  CopyWrapper(const CopyWrapper &o) noexcept : super()
  {
    this->data_ = this->_copy(o.data_);
  }
  CopyWrapper &operator=(const CopyWrapper &o) noexcept
  {
    if (this != &o) {
      this->_deleter(this->data_, static_cast<Base *>(this));
      if (sizeof(Base) > sizeof(this->data_))
        (Base &)(*this) = std::move(o);
      this->data_ = this->_copy(o.data_);
    }
    return *this;
  }

  CopyWrapper(CopyWrapper &&o) noexcept = default;
  CopyWrapper &operator=(CopyWrapper &&o) noexcept = default;

  // also accept from corresponding Reference (also based on Base)
  CopyWrapper(const Base &o) noexcept : super()
  {
    this->data_ = this->_copy(((CopyWrapper &)o).data_);
  }
  CopyWrapper(Base &&o) noexcept : super()
  {
    (super &)(*this) = std::move((super &)o);
  }
};

template<typename CType, typename Base>
using SelectWrapper =
    typename std::conditional<repository::enable_boxed_copy<CType>::value,
        CopyWrapper<Base>, MoveWrapper<Base>>::type;

// basis for registered boxed types
template<typename CppType, typename CType,
    typename Base = GBoxedWrapperBase<CppType, CType>, typename RefType = void>
class GBoxedWrapper : public SelectWrapper<CType, Base>
{
  typedef SelectWrapper<CType, Base> super;

public:
  typedef RefType ReferenceType;

  using super::super;

  GBoxedWrapper(std::nullptr_t = nullptr) {}

  void allocate_()
  {
    if (this->data_)
      return;
    // make sure we match boxed allocation with boxed free
    // still guessing here that all-0 makes for a decent init :-(
    CType tmp;
    memset(&tmp, 0, sizeof(tmp));
    this->data_ = (CType *)g_boxed_copy(this->get_type_(), &tmp);
  }

  // essentially g_boxed_copy
  CppType copy_() const
  {
    return super::template wrap<CppType>(this->gobj_copy_());
  }
};

template<typename CppType, typename CType>
using CBoxedWrapperBase =
    BoxedWrapper<CType, CBoxedFuncs<CppType, CType>, CBoxed>;

// basis for non-registered plain C boxed type
template<typename CppType, typename CType,
    typename Base = CBoxedWrapperBase<CppType, CType>, typename RefType = void>
class CBoxedWrapper : public MoveWrapper<Base>
{
  typedef Base super;

public:
  typedef RefType ReferenceType;

  CBoxedWrapper(std::nullptr_t = nullptr) {}

  void allocate_()
  {
    if (this->data_)
      return;
    this->data_ = g_new0(CType, 1);
  }

  static CppType new_()
  {
    return super::template wrap<CppType>(g_new0(CType, 1));
  }
};

// allocate helper;
// dispatch to method if available
template<typename T, typename Enable = void>
struct allocator : public std::false_type
{
  static void allocate(T &) {}
};

template<typename T>
struct allocator<T, decltype(T().allocate_())> : public std::true_type
{
  static void allocate(T &v) { v.allocate_(); }
};

template<typename T>
void
allocate(T &v)
{
  allocator<T>::allocate(v);
}

// basis for ref-holding to registered box type
template<typename CppType, typename CType, typename Base>
class GBoxedRefWrapper : public Base
{
  typedef Base super;

public:
  typedef CppType BoxType;

  GBoxedRefWrapper(std::nullptr_t = nullptr) {}

  // construct from owning version, but not the other way around
  // (which requires an explicit copy)
  GBoxedRefWrapper(const CppType &other)
  {
    (super &)(*this) = super::template wrap<super>(other.gobj_());
  }

  // support way to convert to owning box
  // (by means of copy as opposed to a simple ref)
  CppType copy_() const
  {
    return super::template wrap<CppType>(this->gobj_copy_());
  }
};

// basis for ref-holding to non-registered plain C boxed type
template<typename CppType, typename CType, typename Base>
class CBoxedRefWrapper : public Base
{
  typedef Base super;

public:
  typedef CppType BoxType;

  CBoxedRefWrapper(std::nullptr_t = nullptr) {}

  // construct from owning version, but not the other way around
  // (which requires an explicit copy)
  CBoxedRefWrapper(const CppType &other)
  {
    (super &)(*this) = super::template wrap<super>(other.gobj_());
  }
};

} // namespace detail

} // namespace gi

#endif // GI_BOXED_HPP
