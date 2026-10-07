#ifndef GI_CONTAINER_HPP
#define GI_CONTAINER_HPP

#include "base.hpp"
#include "wrap.hpp"

GI_MODULE_EXPORT
namespace gi
{
namespace detail
{
// helpers

// element unwrap cast helper;
// element reference type depends on container reference type
// lvalue
template<typename T>
T
cast_ref(T &&el, std::true_type)
{
  return el;
}
// rvalue
template<typename T>
typename std::remove_reference<T>::type &&
cast_ref(T &&el, std::false_type)
{
  return std::move(el);
}

// std::iterator may be deprecated, but it is still handy
// so replicate it here to avoid deprecation warnings
template<typename Category, typename T, typename Distance = std::ptrdiff_t,
    typename Pointer = T *, typename Reference = T &>
struct iterator
{
  using iterator_category = Category;
  using value_type = T;
  using difference_type = Distance;
  using pointer = Pointer;
  using reference = Reference;
};
namespace _traits
{
template<typename T>
struct hash
{};

template<typename T>
struct equal
{};

template<typename CType>
void
_destroy(CType v)
{
  // destroy by wrapping a temporary with full transfer
  // and then simply dropping that temporary
  wrap(v, transfer_full);
}

// C type case
template<typename CType, typename Enable = void>
struct destroy
{
  static constexpr auto func = _destroy<CType>;
};

// Cpp type case
template<typename CppType>
struct destroy<CppType, typename traits::if_valid_type<typename traits::ctype<
                            typename std::decay<CppType>::type>::type>::type>
{
  using CType =
      typename traits::ctype<typename std::decay<CppType>::type>::type;
  static constexpr auto func = _destroy<CType>;
};

// let's simply stick to the following for now
// since others are not so likely and not always clear semantics
template<>
struct hash<std::string>
{
  static constexpr GHashFunc func = g_str_hash;
};

template<>
struct hash<gi::cstring>
{
  static constexpr GHashFunc func = g_str_hash;
};

template<>
struct hash<gi::cstring_v>
{
  static constexpr GHashFunc func = g_str_hash;
};

template<>
struct equal<std::string>
{
  static constexpr GEqualFunc func = g_str_equal;
};

template<>
struct equal<gi::cstring>
{
  static constexpr GEqualFunc func = g_str_equal;
};

template<>
struct equal<gi::cstring_v>
{
  static constexpr GEqualFunc func = g_str_equal;
};

} // namespace _traits

/////////////////
//
////////////////

// helper check/trait
namespace trait
{
std::false_type is_initializer_list_dispatch(...);
template<typename T>
std::true_type is_initializer_list_dispatch(std::initializer_list<T> *);
template<typename T>
struct is_initializer_list
    : decltype(is_initializer_list_dispatch(static_cast<T *>(nullptr)))
{};

} // namespace trait

// helper; combine some traits
template<typename T, typename Transfer>
using elcpptype = traits::cpptype<T, typename element_transfer<Transfer>::type>;

// hard convert/cast of a pointer to a C type to its wrapper type
// (which is expected of same size)
template<typename Transfer, typename CType,
    typename CppType = typename elcpptype<CType, Transfer>::type>
CppType *
wrap_cast(CType *c)
{
  static_assert(sizeof(*c) == sizeof(std::declval<CppType>()), "");
  return reinterpret_cast<CppType *>(c);
}

// snippet to disable a method for transfer none case
#define GI_DISABLE_METHOD_NONE \
  template<typename Enable = void, \
      typename Check = typename std::enable_if< \
          std::is_void<Enable>::value && \
          !std::is_same<Transfer, transfer_none_t>::value>::type>

template<typename ListType, typename ElType, typename Transfer,
    typename Enable = void>
class CollectionBase
{};

template<typename ListType, typename ElCType, typename Transfer>
struct list_ops;

struct GPtrArrayFuncs
{
  constexpr static bool refcnt = true;
  using value_type = gpointer;
  static GType get_type_() { return G_TYPE_PTR_ARRAY; }
  static ::GPtrArray *ref(::GPtrArray *data)
  {
    return data ? g_ptr_array_ref(data) : data;
  }
  static ::GPtrArray *sink(::GPtrArray *data)
  {
    return data ? g_ptr_array_ref(data) : data;
  }
  static void free(::GPtrArray *&data)
  {
    if (data) {
      g_ptr_array_unref(data);
      data = nullptr;
    }
  }
  static ::GPtrArray *float_(::GPtrArray *data) { return data; }
};

template<typename ElCType, typename Transfer>
struct list_ops<::GPtrArray, ElCType, Transfer> : public GPtrArrayFuncs
{};

template<typename ElCType, typename Transfer>
class CollectionBase<::GPtrArray, ElCType, Transfer>
    : public Wrapper<GPtrArrayFuncs, ::GPtrArray>
{
protected:
  using list_ops = GPtrArrayFuncs;

  using state_type = int; // dummy
  state_type _create()
  {
    if (!this->data_) {
      auto &l = this->data_;
      l = g_ptr_array_new();
      if (std::is_same<Transfer, transfer_full_t>::value)
        g_ptr_array_set_free_func(
            l, (GDestroyNotify)_traits::destroy<ElCType>::func);
    }
    return 0;
  }

  void _finish() {}

  state_type _push(state_type s, list_ops::value_type item)
  {
    g_ptr_array_add(this->data_, item);
    return s;
  }

  struct iterator_type
  {
    CollectionBase *self;
    int index;

    bool next(list_ops::value_type &val)
    {
      if (self->data_ && (index < (int)self->data_->len)) {
        val = g_ptr_array_index(self->data_, index++);
        return true;
      }
      return false;
    }
  };

  iterator_type _iterator() { return iterator_type{this, 0}; }

  void _steal()
  {
#if GLIB_CHECK_VERSION(2, 64, 0)
    g_free(g_ptr_array_steal(this->data_, nullptr));
#else
    // nasty, essentially above function
    // but works for code of the past, which no longer changes
    auto parray = (GPtrArray *)this->data_;
    g_free(parray->pdata);
    parray->pdata = 0;
    parray->len = 0;
#endif
    g_ptr_array_unref(this->data_);
    this->data_ = nullptr;
  }

public:
  using value_type = typename elcpptype<ElCType, Transfer>::type;

protected:
  value_type *_cast() const
  {
    return wrap_cast<Transfer>((ElCType *)(this->data_->pdata));
  }

  GI_DISABLE_METHOD_NONE
  void _ensure_array() { _create(); }

public:
  // assume same layout of wrapper and wrappee
  static_assert(sizeof(value_type) == sizeof(gpointer), "");
  static_assert(sizeof(value_type[2]) == sizeof(gpointer[2]), "");

  using iterator = value_type *;
  using const_iterator = const value_type *;

  const_iterator cbegin() const { return this->data_ ? _cast() : nullptr; }
  const_iterator cend() const
  {
    return this->data_ ? begin() + this->data_->len : nullptr;
  }

  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  GI_DISABLE_METHOD_NONE
  iterator begin() { return this->data_ ? _cast() : nullptr; }
  GI_DISABLE_METHOD_NONE iterator end()
  {
    return this->data_ ? begin() + this->data_->len : nullptr;
  }

  size_t size() const noexcept { return this->data_ ? this->data_->len : 0; }
  bool empty() const noexcept { return size() == 0; }

  GI_DISABLE_METHOD_NONE
  void push_front(const value_type &v) { insert(begin(), v); }
  GI_DISABLE_METHOD_NONE
  void push_front(value_type &&v) { insert(begin(), std::move(v)); }

  GI_DISABLE_METHOD_NONE
  void pop_front() { erase(begin()); }

  GI_DISABLE_METHOD_NONE
  void push_back(const value_type &v) { insert(end(), v); }
  GI_DISABLE_METHOD_NONE
  void push_back(value_type &&v) { insert(end(), std::move(v)); }

  GI_DISABLE_METHOD_NONE
  void pop_back()
  {
    if (!empty())
      erase(end() - 1);
  }

  GI_DISABLE_METHOD_NONE
  iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

  GI_DISABLE_METHOD_NONE
  iterator erase(const_iterator first, const_iterator last)
  {
    if (this->data_ && first < last) {
      auto index = first - begin();
      auto cnt = last - first;
      // erase from back to front to minimize (or even avoid) data moves
      while (cnt) {
        --cnt;
        auto v = g_ptr_array_steal_index(this->data_, index + cnt);
        _traits::destroy<ElCType>::func(ElCType(v));
      }
      return begin() + index;
    }
    return begin();
  }

  GI_DISABLE_METHOD_NONE
  iterator insert(const_iterator pos, const value_type &v)
  {
    auto cv = unwrap(v, Transfer());
    return _insert(pos, cv);
  }
  GI_DISABLE_METHOD_NONE
  iterator insert(const_iterator pos, value_type &&v)
  {
    auto cv = unwrap(std::move(v), Transfer());
    return _insert(pos, cv);
  }

  // erase to avoid memory free
  GI_DISABLE_METHOD_NONE
  void clear() { erase(begin(), end()); }

protected:
  iterator _insert(const_iterator pos, gpointer v)
  {
    // establish index before potential create
    auto index = pos - begin();
    _ensure_array();
    g_ptr_array_insert(this->data_, index, v);
    return begin() + index;
  }
};

struct GHashTableFuncs
{
  constexpr static bool refcnt = true;
  using value_type = std::pair<gpointer, gpointer>;
  static GType get_type_() { return G_TYPE_HASH_TABLE; }
  static ::GHashTable *ref(::GHashTable *data)
  {
    return data ? g_hash_table_ref(data) : data;
  }
  static ::GHashTable *sink(::GHashTable *data)
  {
    return data ? g_hash_table_ref(data) : data;
  }
  static void free(::GHashTable *&data)
  {
    if (data) {
      g_hash_table_unref(data);
      data = nullptr;
    }
  }
  static ::GHashTable *float_(::GHashTable *data) { return data; }
};

template<typename ElCType, typename Transfer>
struct list_ops<::GHashTable, ElCType, Transfer> : public GHashTableFuncs
{};

template<typename ElCType, typename Transfer>
class CollectionBase<::GHashTable, ElCType, Transfer>
    : public Wrapper<GHashTableFuncs, ::GHashTable>
{
protected:
  using list_ops = GHashTableFuncs;
  using ElCKeyType = typename std::tuple_element<0, ElCType>::type;
  using ElCMappedType = typename std::tuple_element<1, ElCType>::type;
  using ElCppKeyType = typename elcpptype<ElCKeyType, Transfer>::type;
  using ElCppMappedType = typename elcpptype<ElCMappedType, Transfer>::type;

  static_assert(sizeof(ElCKeyType) == sizeof(gpointer), "");
  static_assert(sizeof(ElCMappedType) == sizeof(gpointer), "");

  using key_type = const ElCppKeyType;
  using mapped_type = ElCppMappedType;
  using value_type = std::pair<ElCppKeyType, mapped_type>;
  using const_value_type = std::pair<const ElCppKeyType, const mapped_type>;

  using state_type = int; // dummy
  state_type _create()
  {
    if (!this->data_) {
      if (std::is_same<Transfer, transfer_full_t>::value) {
        this->data_ = g_hash_table_new_full(_traits::hash<ElCppKeyType>::func,
            _traits::equal<ElCppKeyType>::func,
            GDestroyNotify(_traits::destroy<ElCppKeyType>::func),
            GDestroyNotify(_traits::destroy<ElCppMappedType>::func));
      } else {
        this->data_ = g_hash_table_new(_traits::hash<ElCppKeyType>::func,
            _traits::equal<ElCppKeyType>::func);
      }
    }
    return 0;
  }

  state_type _push(state_type s, list_ops::value_type item)
  {
    g_hash_table_replace(this->data_, item.first, item.second);
    return s;
  }

  void _finish() {}

  struct iterator_type
  {
    CollectionBase &self;
    GHashTableIter iter;
    int index{};

    iterator_type(CollectionBase &_self) : self(_self)
    {
      if (self.data_)
        g_hash_table_iter_init(&iter, self.data_);
    }

    bool next(list_ops::value_type &val)
    {
      if (!self.data_)
        return false;
      gpointer key, value;
      auto more = g_hash_table_iter_next(&iter, &key, &value);
      val = {key, value};
      return more;
    }
  };

  iterator_type _iterator() { return iterator_type{*this}; }

  void _steal()
  {
    g_hash_table_steal_all(this->data_);
    g_hash_table_unref(this->data_);
    this->data_ = nullptr;
  }

public:
  class const_iterator
      : public detail::iterator<std::input_iterator_tag, const_value_type>
  {
    using self_type = const_iterator;

    enum State { NONE, INIT, ITERATING, DONE };

    ::GHashTableIter iter;
    std::pair<gpointer, gpointer> kv;
    State state;

    const_value_type *_cast()
    {
      // sanity check in view of rough cast
      static_assert(sizeof(kv) == sizeof(const_value_type), "");
      return reinterpret_cast<const_value_type *>(&kv);
    }

    ::GHashTable *_table() const
    {
      return state == NONE || state == DONE
                 ? nullptr
                 : g_hash_table_iter_get_hash_table(
                       const_cast<::GHashTableIter *>(&iter));
    }

  public:
    const_iterator(
        ::GHashTable *table, gpointer key = nullptr, gpointer value = nullptr)
        : state(NONE)
    {
      if (table) {
        g_hash_table_iter_init(&iter, table);
        state = INIT;
        // if iterator is created as a result of a find,
        // then we arrange for lazy sync of hash table iterator
        if (G_LIKELY(!key && !value)) {
          state = ITERATING;
          ++(*this);
        } else {
          kv = {key, value};
        }
      }
    }

    const_value_type &operator*() { return *_cast(); }
    const_value_type *operator->() { return _cast(); }

    bool operator==(const self_type &other)
    {
      auto t = _table();
      return (t == other._table()) && (!t || (kv.first == other.kv.first));
    }
    bool operator!=(const self_type &other) { return !(*this == other); }

    self_type &operator++()
    {
      bool has_more = true;
      // if needed, sync iterator with key
      if (G_UNLIKELY(state != ITERATING)) {
        gpointer key{}, value{};
        while (has_more && key != kv.first) {
          has_more = g_hash_table_iter_next(&iter, &key, &value);
        }
      }
      if (G_LIKELY(has_more))
        has_more = g_hash_table_iter_next(&iter, &kv.first, &kv.second);
      state = has_more ? ITERATING : DONE;
      return *this;
    }
  };

  // consistency
  using iterator = const_iterator;

  const_iterator cbegin() const { return {this->data_}; }
  const_iterator cend() const { return {nullptr}; }

  const_iterator begin() const { return cbegin(); };
  const_iterator end() const { return cend(); };

  size_t size() const
  {
    return this->data_ ? g_hash_table_size(this->data_) : 0;
  }
  bool empty() const { return size() == 0; }

  GI_DISABLE_METHOD_NONE
  void clear()
  {
    if (this->data_)
      g_hash_table_remove_all(this->data_);
  };

  size_t count(key_type &key) const
  {
    if (!this->data_)
      return 0;
    auto v = unwrap(key, transfer_none);
    return g_hash_table_contains(this->data_, v) ? 1 : 0;
  }

  const_iterator find(key_type &key) const
  {
    if (this->data_) {
      auto v = unwrap(key, transfer_none);
      gpointer lv;
      if (g_hash_table_lookup_extended(this->data_, v, nullptr, &lv))
        return {this->data_, gpointer(v), gpointer(lv)};
    }
    return end();
  }

  typename elcpptype<ElCMappedType, transfer_none_t>::type lookup(
      key_type &key) const
  {
    auto it = find(key);
    if (it == end())
      return {};
    // decay copy should be possible
    return it->second;
  }

  GI_DISABLE_METHOD_NONE
  bool replace(ElCppKeyType &&key, ElCppMappedType &&value)
  {
    _create();
    auto kv = unwrap(std::move(key), Transfer());
    auto vv = unwrap(std::move(value), Transfer());
    return g_hash_table_replace(this->data_, kv, vv);
  }

  GI_DISABLE_METHOD_NONE
  size_t erase(key_type &key)
  {
    if (this->data_) {
      auto v = unwrap(key, transfer_none);
      return g_hash_table_remove(this->data_, gpointer(v)) ? 1 : 0;
    }
    return 0;
  }
};

template<typename ListType>
struct ListFuncs
{
  constexpr static bool refcnt = false;
  using value_type = gpointer;
  // this value type is used in e.g. signal argument
  static GType get_type_() { return G_TYPE_POINTER; }
  static ListType *ref(ListType *data) { return data; }
};

template<typename ElCType, typename Transfer>
struct list_ops<::GList, ElCType, Transfer> : public ListFuncs<::GList>
{};

template<typename ElCType, typename Transfer>
struct list_ops<::GSList, ElCType, Transfer> : public ListFuncs<::GSList>
{};

template<typename ListType>
struct LinkedListOps;

template<>
struct LinkedListOps<::GSList>
{
  static void free(::GSList *l) { g_slist_free(l); }
  static void free_full(::GSList *l, GDestroyNotify func)
  {
    g_slist_free_full(l, func);
  }
  static ::GSList *append(::GSList *l, gpointer data)
  {
    return g_slist_append(l, data);
  }
  static ::GSList *prepend(::GSList *l, gpointer data)
  {
    return g_slist_prepend(l, data);
  }
  static ::GSList *last(::GSList *l) { return g_slist_last(l); }
  static ::GSList *insert_before(::GSList *l, ::GSList *link, gpointer data)
  {
    return g_slist_insert_before(l, link, data);
  }
  static ::GSList *delete_link(::GSList *l, ::GSList *link)
  {
    return g_slist_delete_link(l, link);
  }
  static ::GSList *reverse(::GSList *l) { return g_slist_reverse(l); }
};

template<>
struct LinkedListOps<::GList>
{
  static void free(::GList *l) { g_list_free(l); }
  static void free_full(::GList *l, GDestroyNotify func)
  {
    return g_list_free_full(l, func);
  }
  static ::GList *append(::GList *l, gpointer data)
  {
    return g_list_append(l, data);
  }
  static ::GList *prepend(::GList *l, gpointer data)
  {
    return g_list_prepend(l, data);
  }
  static ::GList *last(::GList *l) { return g_list_last(l); }
  static ::GList *insert_before(::GList *l, ::GList *link, gpointer data)
  {
    return g_list_insert_before(l, link, data);
  }
  static ::GList *delete_link(::GList *l, ::GList *link)
  {
    return g_list_delete_link(l, link);
  }
  static ::GList *reverse(::GList *l) { return g_list_reverse(l); }
};

template<typename ListType, typename ElType, typename Transfer>
struct LinkedListBase;

template<typename ListType, typename ElCType>
struct LinkedListBase<ListType, ElCType, transfer_none_t>
    : public SharedWrapper<ListType>
{
protected:
  static void _deleter(ListType *&d, ...) { d = nullptr; }
  static ListType *_copy(ListType *d) { return d; }
};

template<typename ListType, typename ElCType>
struct LinkedListBase<ListType, ElCType, transfer_container_t>
    : public SharedWrapper<ListType>
{
protected:
  static void _deleter(ListType *&l, ...)
  {
    LinkedListOps<ListType>::free(l);
    l = nullptr;
  }
};

template<typename ListType, typename ElCType>
struct LinkedListBase<ListType, ElCType, transfer_full_t>
    : public SharedWrapper<ListType>
{
protected:
  static void _deleter(ListType *&l, ...)
  {
    LinkedListOps<ListType>::free_full(
        l, (GDestroyNotify)_traits::destroy<ElCType>::func);
    l = nullptr;
  }
};

// enable copy constructor on none transfer cases
// compiler otherwise always selects the deleted one, even if other options
template<typename Transfer, typename Base>
using SelectBaseWrapper =
    typename std::conditional<std::is_same<Transfer, transfer_none_t>::value,
        CopyWrapper<Base>, MoveWrapper<Base>>::type;

template<typename ListType, typename ElCType, typename Transfer>
class CollectionBase<ListType, ElCType, Transfer,
    typename std::enable_if<std::is_same<ListType, ::GSList>::value ||
                            std::is_same<ListType, ::GList>::value>::type>
    : public SelectBaseWrapper<Transfer,
          LinkedListBase<ListType, ElCType, Transfer>>
{
protected:
  using list_ops = ListFuncs<ListType>;
  using state_type = ListType *;

  state_type _create() { return nullptr; }

  state_type _push(state_type, typename list_ops::value_type item)
  {
    this->data_ = LinkedListOps<ListType>::prepend(this->data_, item);
    return nullptr;
  }

  void _finish()
  {
    this->data_ = LinkedListOps<ListType>::reverse(this->data_);
  }

  struct iterator_type
  {
    ListType *it;
    bool next(typename list_ops::value_type &val)
    {
      if (it) {
        val = it->data;
        it = it->next;
        return true;
      }
      return false;
    }
  };

  iterator_type _iterator() { return {this->data_}; }

  void _steal()
  {
    LinkedListOps<ListType>::free(this->data_);
    this->data_ = nullptr;
  }

  static void _destroy(gpointer value)
  {
    _traits::destroy<ElCType>::func(ElCType(value));
  }

public:
  using value_type = typename elcpptype<ElCType, Transfer>::type;
  static constexpr bool is_glist = std::is_same<ListType, ::GList>::value;

  template<typename value_t>
  class iterator_t : public detail::iterator<std::forward_iterator_tag, value_t>
  {
    using self_type = iterator_t;
    friend class CollectionBase;
    ListType *it = nullptr;

  public:
    iterator_t(ListType *l = nullptr) : it(l){};

    // construct const from non-const
    template<typename other_value_t,
        typename Enable = typename std::enable_if<
            std::is_convertible<other_value_t *, value_t *>::value>::type>
    iterator_t(const iterator_t<other_value_t> &oi) : iterator_t(oi.it)
    {}

    value_t &operator*() { return *operator->(); }

    value_t *operator->()
    {
      return wrap_cast<Transfer>((ElCType *)(&it->data));
    }

    bool operator==(const self_type &other) { return it == other.it; }

    bool operator!=(const self_type &other) { return !(*this == other); }

    self_type &operator++()
    {
      if (it)
        it = it->next;
      return *this;
    }

    self_type operator++(int)
    {
      auto ret = *this;
      ++(*this);
      return ret;
    }
  };

  using const_iterator = iterator_t<const value_type>;
  using iterator = iterator_t<value_type>;

  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  const_iterator cbegin() const { return {this->data_}; }
  const_iterator cend() const { return {}; }

  GI_DISABLE_METHOD_NONE
  iterator begin() { return {this->data_}; }
  GI_DISABLE_METHOD_NONE
  iterator end() { return {}; }

  bool empty() const noexcept { return !this->data_; }

  GI_DISABLE_METHOD_NONE
  void push_front(const value_type &v)
  {
    auto cv = unwrap(v, Transfer());
    this->data_ = LinkedListOps<ListType>::prepend(this->data_, cv);
  }
  GI_DISABLE_METHOD_NONE
  void push_front(value_type &&v)
  {
    auto cv = unwrap(std::move(v), Transfer());
    this->data_ = LinkedListOps<ListType>::prepend(this->data_, cv);
  }

  GI_DISABLE_METHOD_NONE
  void pop_front()
  {
    if (this->data_)
      _destroy(this->data_->data);
    this->data_ =
        LinkedListOps<ListType>::delete_link(this->data_, this->data_);
  }

  GI_DISABLE_METHOD_NONE
  void push_back(const value_type &v)
  {
    auto cv = unwrap(v, Transfer());
    this->data_ = LinkedListOps<ListType>::append(this->data_, cv);
  }
  GI_DISABLE_METHOD_NONE
  void push_back(value_type &&v)
  {
    auto cv = unwrap(std::move(v), Transfer());
    this->data_ = LinkedListOps<ListType>::append(this->data_, cv);
  }

  GI_DISABLE_METHOD_NONE
  void pop_back()
  {
    auto last = LinkedListOps<ListType>::last(this->data_);
    if (last)
      _destroy(last->data);
    this->data_ = LinkedListOps<ListType>::delete_link(this->data_, last);
  }

  GI_DISABLE_METHOD_NONE
  iterator erase(const_iterator pos)
  {
    ListType *next = nullptr;
    if (pos.it) {
      next = pos.it->next;
      _destroy(pos.it->data);
    }
    this->data_ = LinkedListOps<ListType>::delete_link(this->data_, pos.it);
    return {next};
  }

  template<typename Enable = void,
      typename Check = typename std::enable_if<
          std::is_void<Enable>::value && Transfer().value && is_glist>::type>
  iterator insert(const_iterator pos, const value_type &v)
  {
    auto cv = unwrap(v, Transfer());
    return _insert(pos, cv);
  }
  template<typename Enable = void,
      typename Check = typename std::enable_if<
          std::is_void<Enable>::value && Transfer().value && is_glist>::type>
  iterator insert(const_iterator pos, value_type &&v)
  {
    auto cv = unwrap(std::move(v), Transfer());
    return _insert(pos, cv);
  }

  GI_DISABLE_METHOD_NONE
  void reverse()
  {
    this->data_ = LinkedListOps<ListType>::reverse(this->data_);
  }

  GI_DISABLE_METHOD_NONE
  void clear()
  {
    this->_deleter(this->data_);
    this->data_ = nullptr;
  }

protected:
  iterator _insert(const_iterator pos, gpointer v)
  {
    this->data_ =
        LinkedListOps<ListType>::insert_before(this->data_, pos.it, v);
    return {pos.it ? pos.it->prev : LinkedListOps<ListType>::last(this->data_)};
  }
};

enum SpanType {
  DYNAMIC = -1,
  ZT = 0,
};

template<int SIZE>
struct Span
{};

template<typename ElCType, typename Transfer, int SIZE>
struct SpanBase : public SharedWrapper<ElCType>
{
protected:
  // extra members needed in this case
  // only > 0 if data locally allocated (as opposed to from elsewhere)
  size_t capacity_ = 0;
  size_t size_ = 0;

  static void _deleter(decltype(SpanBase::data_) &d, SpanBase *self)
  {
    assert(self);
    if (std::is_same<Transfer, transfer_full_t>::value && d) {
      auto end = d + self->size_;
      for (auto p = d; p != end; ++p) {
        _traits::destroy<ElCType>::func(*p);
      }
    }
    if (!std::is_same<Transfer, transfer_none_t>::value)
      g_free(d);
    d = nullptr;
  }

  template<typename Enable = void>
  static decltype(SpanBase::data_) _copy(decltype(SpanBase::data_) d)
  {
    // should only end up used in none case
    static_assert(std::is_same<Transfer, transfer_none_t>::value, "");
    return d;
  }
};

template<typename ElCType, typename Transfer, int SIZE>
struct list_ops<Span<SIZE>, ElCType, Transfer> : public ListFuncs<ElCType>
{};

template<typename ElCType, typename Transfer, int SIZE>
class CollectionBase<Span<SIZE>, ElCType, Transfer>
    : public SelectBaseWrapper<Transfer, SpanBase<ElCType, Transfer, SIZE>>
{
protected:
  static auto constexpr SPAN_SIZE = SIZE;
  struct list_ops : public ListFuncs<ElCType>
  {
    using value_type = ElCType;
  };
  using state_type = ElCType *;

  // used by wrap
  CollectionBase(ElCType *p = nullptr, int s = 0)
  {
    // NOTE no copy needed;
    // either transfer is full/container (and ownership is assumed)
    // either transfer is none (and will never free/release)
    this->data_ = p;
    this->capacity_ = 0;
    // normalize -1 from C side to avoid potential surprises elsewhere
    if (s < 0 || SIZE == ZT) {
      s = 0;
      if (p) {
        while (*p) {
          ++s;
          ++p;
        }
      }
    }
    this->size_ = s;
  }

  ElCType *_create()
  {
    if (!this->data_) {
      this->capacity_ = SIZE > 0 ? SIZE : 2;
      this->data_ = (ElCType *)g_malloc0(this->capacity_ * sizeof(ElCType));
      this->size_ = 0;
    }
    return this->data_;
  }

  void _create_from(ElCType *d, size_t lsize)
  {
    using T = ElCType;
    // try to re-use incoming data
    // otherwise fall back to plain copy
    // (if transfer or zero-termination fiddling is needed)
    bool allocate = (Transfer().value != transfer_none.value) || (SIZE == ZT);
    auto size = SIZE == ZT ? lsize + 1 : lsize;
    auto *data = d;

    if (allocate) {
      data = (T *)g_malloc(size * sizeof(T));
      this->capacity_ = size;
      memcpy(data, d, lsize * sizeof(T));
      if (SIZE == ZT)
        data[lsize] = T{};
    }

    // track data
    assert(!this->data_);
    g_free(this->data_);
    this->data_ = data;
    this->size_ = lsize;
  }

  void _reserve(size_t capacity)
  {
    if (this->capacity_ < capacity + (SIZE == ZT)) {
      this->capacity_ =
          std::max(std::max(2 * this->capacity_, size_t(2)), capacity);
      this->data_ =
          (ElCType *)g_realloc_n(this->data_, this->capacity_, sizeof(ElCType));
    }
  }

  state_type _push(state_type s, ElCType item)
  {
    // should only get here if previously allocated
    assert(this->capacity_ > 0);
    auto offset = s - this->data_;
    _reserve(offset + 1);
    s = this->data_ + offset;
    *s = item;
    ++this->size_;
    ++s;
    // ensure ZT if applicable
    if (SIZE == ZT) {
      assert(size_t(s - this->data_) < this->capacity_);
      *s = ElCType{};
    }
    return s;
  }

  void _finish()
  {
    if ((SIZE > 0) && (this->size_ != (size_t)SIZE))
      g_error("fixed list size mismatch");
  }

  struct iterator_type
  {
    CollectionBase &self;
    ElCType *p;

    iterator_type(CollectionBase &s) : self(s), p(self.data_) {}

    explicit operator bool()
    {
      return p && (ZT ? !!*p : (p != self.data_ + self.size_));
    };

    bool next(ElCType &val)
    {
      if (*this) {
        val = *(p++);
        return true;
      }
      return false;
    }
  };

  iterator_type _iterator() { return {*this}; }

  void _steal()
  {
    // if Holder is trying to destroy, check if we really allocated
    // (may have optimized array case)
    if (!std::is_same<Transfer, transfer_none_t>::value || this->capacity_)
      g_free(this->data_);
    this->data_ = nullptr;
    this->size_ = this->capacity_ = 0;
  };

public:
  // this one is used by code generation (the unwrap size part)
  // so it should only be used in specific situations
  // let's add checks to make it so
  size_t _size() const
  {
    static_assert(SIZE != ZT, "");
    return this->size_;
  }

  using value_type = typename elcpptype<ElCType, Transfer>::type;

  // assume same layout of wrapper and wrappee
  static_assert(sizeof(value_type) == sizeof(ElCType), "");
  static_assert(sizeof(value_type[2]) == sizeof(ElCType[2]), "");

  using iterator = value_type *;
  using const_iterator = const value_type *;

  const_iterator cbegin() const
  {
    return this->data_ ? wrap_cast<Transfer>(this->data_) : nullptr;
  }
  const_iterator cend() const
  {
    return this->data_ ? begin() + this->size_ : nullptr;
  }

  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }

  GI_DISABLE_METHOD_NONE
  iterator begin()
  {
    return this->data_ ? wrap_cast<Transfer>(this->data_) : nullptr;
  }
  GI_DISABLE_METHOD_NONE
  iterator end() { return this->data_ ? begin() + this->size_ : nullptr; }

  size_t size() const { return this->size_; }
  bool empty() const { return size() == 0; }

  GI_DISABLE_METHOD_NONE
  void push_front(const value_type &v) { insert(begin(), v); }
  GI_DISABLE_METHOD_NONE
  void push_front(value_type &&v) { insert(begin(), std::move(v)); }

  GI_DISABLE_METHOD_NONE
  void pop_front() { erase(begin()); }

  GI_DISABLE_METHOD_NONE
  void push_back(const value_type &v) { insert(end(), v); }
  GI_DISABLE_METHOD_NONE
  void push_back(value_type &&v) { insert(end(), std::move(v)); }

  GI_DISABLE_METHOD_NONE
  void pop_back()
  {
    if (!empty())
      erase(end() - 1);
  }

  GI_DISABLE_METHOD_NONE
  iterator erase(const_iterator pos) { return erase(pos, pos + 1); }

  GI_DISABLE_METHOD_NONE
  iterator erase(const_iterator first, const_iterator last)
  {
    if (this->data_ && this->size_ > 0 && first < last) {
      size_t index = first - begin();
      size_t cnt = last - first;
      size_t lindex = std::min(index + cnt, this->size_);
      for (auto i = index; i < lindex; ++i)
        _traits::destroy<ElCType>::func(this->data_[index]);
      if (lindex < this->size_)
        memmove(this->data_ + index, this->data_ + lindex,
            sizeof(*this->data_) * (this->size_ - lindex));
      this->size_ -= cnt;
      if (SIZE == ZT)
        this->data_[this->size_] = ElCType{};
      return begin() + index;
    }
    return begin();
  }

  GI_DISABLE_METHOD_NONE
  iterator insert(const_iterator pos, const value_type &v)
  {
    auto cv = unwrap(v, Transfer());
    return _insert(pos, cv);
  }
  GI_DISABLE_METHOD_NONE
  iterator insert(const_iterator pos, value_type &&v)
  {
    auto cv = unwrap(std::move(v), Transfer());
    return _insert(pos, cv);
  }

  size_t capacity() const { return this->capacity_; }

  GI_DISABLE_METHOD_NONE
  void reserve(size_t capacity) { _reserve(capacity); }

  // erase to avoid memory free
  GI_DISABLE_METHOD_NONE
  void clear() { erase(begin(), end()); }

protected:
  iterator _insert(const_iterator pos, ElCType v)
  {
    // establish index before potential create
    size_t index = pos - begin();
    // grow if needed
    _reserve(this->size_ + 1);
    // move if needed
    if (index < this->size_)
      memmove(this->data_ + index + 1, this->data_ + index,
          sizeof(*this->data_) * (this->size_ - index));
    // insert
    ++this->size_;
    this->data_[index] = v;
    // ensure ZT if applicable
    if (SIZE == ZT) {
      assert(this->size_ < this->capacity_);
      this->data_[this->size_] = ElCType{};
    }
    return begin() + index;
  }
};

// class tag
class Container
{};

// minor helper, essentially compile-time signal/notification
template<typename T>
struct Notifier
{
  static constexpr bool construct_none = false;
  static void _updated(T *, bool /*create*/) {}
};

// considered owning for container or full
// unwrap will have to release_()
template<typename ListType, typename T, typename Transfer = transfer_full_t,
    typename Notify = Notifier<void>, typename ExtraBase = Container>
struct Collection : public CollectionBase<ListType, T, Transfer>,
                    public ExtraBase
{
protected:
  using self_type = Collection;
  using super_type = CollectionBase<ListType, T, Transfer>;

public:
  using list_ops = typename super_type::list_ops;

protected:
  using ElTransfer = typename element_transfer<Transfer>::type;

  // decompose pair in case of map
  template<typename G>
  struct get_types
  {
    // map to harmless type if no key applicable
    using key_type = bool;
    using mapped_type = G;
    using cpp_type = typename traits::cpptype<G, ElTransfer>::type;
  };

  template<typename T1, typename T2>
  struct get_types<std::pair<T1, T2>>
  {
    using key_type = T1;
    using mapped_type = T2;
    using cpp_type = std::pair<typename traits::cpptype<T1, ElTransfer>::type,
        typename traits::cpptype<T2, ElTransfer>::type>;
  };

  using KeyCType = typename get_types<T>::key_type;
  using ElCType = typename get_types<T>::mapped_type;

  using KeyCppType = typename traits::cpptype<KeyCType, ElTransfer>::type;
  using ElCppType = typename traits::cpptype<ElCType, ElTransfer>::type;

  using ValueCppType = typename get_types<T>::cpp_type;

  // sanity/consistency checks
  // expect normalized template types
  static_assert(
      std::is_same<ListType, typename std::decay<ListType>::type>::value, "");
  static_assert(
      std::is_same<KeyCppType, typename std::decay<KeyCppType>::type>::value,
      "");
  static_assert(
      std::is_same<ElCppType, typename std::decay<ElCppType>::type>::value, "");

  // internal wrap of list-case
  Collection(ListType *l, bool own)
  {
    this->data_ = ((!own && l) ? list_ops::ref(l) : l);
  }

  // internal wrap of span case
  // (also avoid conflict with a public constructor below)
  template<typename Enable = void>
  Collection(ElCType *d, size_t s, std::nullptr_t) : super_type(d, s)
  {}

  template<typename T1, typename T2, typename LVR>
  std::pair<gpointer, gpointer> unwrap_item(std::pair<T1, T2> &el, LVR)
  {
    // NOTE if compiler error occurs here, perhaps std::move() input
    auto item1 = unwrap(cast_ref(el.first, LVR()), ElTransfer());
    static_assert(std::is_pointer<decltype(item1)>::value, "expected pointer");
    auto item2 = unwrap(cast_ref(el.second, LVR()), ElTransfer());
    static_assert(std::is_pointer<decltype(item2)>::value, "expected pointer");
    return {(gpointer)item1, (gpointer)item2};
  };

  template<typename T1, typename LVR>
  typename list_ops::value_type unwrap_item(T1 &el, LVR)
  {
    // NOTE if compiler error occurs here, perhaps std::move() input
    auto item = unwrap(cast_ref(el, LVR()), ElTransfer());
    // only force pointers or same size to same size
    using item_type = decltype(item);
    static_assert(
        std::is_pointer<item_type>::value ==
                std::is_pointer<typename list_ops::value_type>::value &&
            sizeof(item_type) == sizeof(typename list_ops::value_type),
        "expected pointer or matched size");
    return (typename list_ops::value_type)item;
  }

  template<typename Iterator, typename LVR>
  void create_from(Iterator first, Iterator last, LVR)
  {
    // empty container is not null, so always init/create
    // NOTE should also add leading/initial zero if needed
    Notify::_updated(this, true);
    auto s = this->_create();
    while (first != last) {
      auto &el = *first;
      auto item = unwrap_item(el, LVR());
      s = this->_push(s, item);
      ++first;
    }
    // possible (size) check might be done here
    this->_finish();
  }

  // array as surrogate generic container
  template<typename InputT>
  void create_from_array(InputT *d, size_t s, std::false_type)
  {
    create_from(d, d + s, std::true_type());
  }

  // array shortcut case; from plain array to matching span
  template<typename InputT>
  void create_from_array(InputT *d, size_t s, std::true_type)
  {
    // should be around as this should be Span case
    // add cast to avoid const issues etc
    using v_type = typename list_ops::value_type;
    static_assert(sizeof(v_type) == sizeof(InputT), "");
    Notify::_updated(this, true);
    this->_create_from((v_type *)d, s);
  }

  template<typename Out, typename Enable = void>
  struct get_value_type
  {
    using type = typename Out::container_type::value_type;
  };

  template<typename Tp>
  struct get_value_type<Tp *>
  {
    using type = typename std::decay<Tp>::type;
  };

  template<typename OutIterator, typename T1, typename T2>
  void wrap_item_add(std::pair<T1, T2> &el, OutIterator &out)
  {
    using DestType = typename get_value_type<OutIterator>::type;
    // expected to be a pair
    using DestType1 = typename std::tuple_element<0, DestType>::type;
    using DestType2 = typename std::tuple_element<1, DestType>::type;
    static_assert(!(std::is_same<Transfer, transfer_full_t>::value &&
                      (traits::is_reftype<DestType1>::value ||
                          traits::is_reftype<DestType2>::value)),
        "full transfer to reftype");
    auto item1 = wrap((KeyCType)el.first, ElTransfer());
    auto item2 = wrap((ElCType)el.second, ElTransfer());
    *out = std::make_pair(std::move(item1), std::move(item2));
  };

  template<typename OutIterator, typename T1>
  void wrap_item_add(T1 &el, OutIterator &out)
  {
    // validation check; no transfer full to a reftype
    using DestType = typename get_value_type<OutIterator>::type;
    // sanity check that we probably got the right type
    static_assert(std::is_convertible<ElCppType, DestType>::value, "");
    static_assert(!(std::is_same<Transfer, transfer_full_t>::value &&
                      traits::is_reftype<DestType>::value),
        "full transfer to reftype");
    using item_type = typename std::decay<decltype(el)>::type;
    // would also lead to cast failure below, but make it more expressive
    static_assert(
        std::is_pointer<item_type>::value == std::is_pointer<ElCType>::value &&
            sizeof(item_type) == sizeof(ElCType),
        "expected pointer or matched size");
    // could have const char** or so, so let's (const) cast the hard way
    *out = wrap((ElCType)el, ElTransfer());
  }

public:
  // some common type blurbs
  using value_type = typename super_type::value_type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type &;
  using const_reference = const value_type &;

  static GType get_type_() { return list_ops::get_type_(); }

  // used by code generation (for non-plain array case)
  // but could be of general use
  template<typename OutIterator>
  void move_to(OutIterator out) &&
  {
    // NOTE assumption;
    // out creates as it goes (e.g. back_inserter)
    // or there is enough output space
    typename list_ops::value_type item;
    auto it = this->_iterator();
    while (it.next(item)) {
      wrap_item_add(item, out);
      ++out;
    }
    // clear out elements without destroying them if needed
    // (if we pulled their ownership with full transfer)
    constexpr bool steal = std::is_same<ElTransfer, transfer_full_t>::value;
    // provide for equally cleared state in either case
    Notify::_updated(this, false);
    if (steal && this->data_) {
      this->_steal();
    } else if (this->data_) {
      this->~self_type();
      this->data_ = nullptr;
    }
  }

public:
  // group a few helpers
  struct _detail
  {
    using DataType = decltype(std::declval<super_type>().gobj_());

    // check if it is possible to create a container
    // a none variant does not assume ownership
    // so it is not in a position to create a collection (with no other owner)
    // however, as usual, the ref types can always own
    // also, allow tweaking this restriction (e.g. for holder subclass below)
    static constexpr bool constructible =
        !std::is_same<Transfer, transfer_none_t>::value || list_ops::refcnt ||
        Notify::construct_none;

    static constexpr bool is_span = !std::is_class<typename std::decay<
        decltype(*std::declval<super_type>().gobj_())>::type>::value;

    template<bool B, typename Enable = void>
    struct span_size_impl : public std::integral_constant<int, 0>
    {};

    template<typename Enable>
    struct span_size_impl<true, Enable>
        : public std::integral_constant<int,
              super_type::SPAN_SIZE >= 0 ? super_type::SPAN_SIZE : 0>
    {};

    static constexpr int span_size = span_size_impl<is_span>::value;
    static constexpr bool is_fixed_span = is_span && span_size > 0;

    // check if this is Span<> collection of plain V elements
    // in case none transfer, we can also copy around any typical element
    // (the deref of data_ is so we only end up matching a Span<> case)
    template<typename V>
    using is_plain = typename std::conditional<
        is_span &&
            (traits::is_plain<typename std::decay<V>::type>::value ||
                std::is_same<Transfer, transfer_none_t>::value) &&
            std::is_same<typename list_ops::value_type,
                typename std::decay<V>::type>::value,
        std::true_type, std::false_type>::type;

    // check Cpp type compatibility
    // this could be triggered on non-complete V, in which case
    // std::is_convertible is undefined, so guard that approach
    template<typename V, bool Complete = false>
    struct is_acceptable_helper : public std::integral_constant<bool, false>
    {};

    template<typename V>
    struct is_acceptable_helper<V, true>
        : public std::is_convertible<V, ValueCppType>
    {};

    template<typename V>
    using is_acceptable = typename is_acceptable_helper<V,
        traits::is_type_complete<V>::value>::type;

    // likewise, deal with const variations on e.g. char**
    // (as char* can be converted to const char*)
    template<typename CType>
    using is_compatible =
        typename std::conditional<std::is_convertible<ElCType, CType>::value,
            std::true_type, std::false_type>::type;
  };

  Collection(std::nullptr_t = nullptr) {}

  // a non-owning accepts from all other types
  // (either the other owns or there is another non-owner already)
  template<typename OtherTransfer,
      typename Enable = typename std::enable_if<
          !std::is_same<void, OtherTransfer>::value &&
          std::is_same<Transfer, transfer_none_t>::value>::type>
  Collection(const Collection<ListType, T, OtherTransfer> &cother)
  {
    // never mind any const, as usual
    auto &other = const_cast<Collection<ListType, T, OtherTransfer> &>(cother);
    this->data_ = other.gobj_() ? list_ops::ref(other.gobj_()) : nullptr;
  }

  // construct from reasonably matching container
  // NOTE may need r-value input for non-movable (content) types
  template<typename Container,
      typename Check = typename std::enable_if<
          _detail::template is_acceptable<
              typename std::decay<Container>::type::value_type>::value &&
          _detail::constructible>::type,
      typename Enable =
          typename detail::disable_if_same_or_derived<Collection, Container>>
  Collection(Container &&c)
  {
    create_from(
        std::begin(c), std::end(c), std::is_lvalue_reference<Container>());
  }

  // construct from initializer list
  template<typename InitElement,
      typename Check = typename std::enable_if<
          _detail::template is_acceptable<InitElement>::value &&
          _detail::constructible>::type,
      typename Enable =
          typename detail::disable_if_same_or_derived<Collection, Container>>
  Collection(std::initializer_list<InitElement> c)
  {
    // initializer_list iterator is a const iterator, so no moving from
    create_from(std::begin(c), std::end(c), std::true_type());
  }

  // construct from array
  // (these arguments could come from a vector, or span, or ...)
  // NOTE no evident way to mark as r-value/movable-from,
  //   so it will just have to do and work out
  template<typename InputT,
      typename Check = typename std::enable_if<
          _detail::template is_acceptable<InputT>::value &&
          _detail::constructible>::type>
  Collection(InputT *d, size_t s)
  {
    create_from_array(d, s, typename _detail::template is_plain<InputT>());
  }

  // construct from fixed-size array if this is a fixed-size Span<> case
  // NOTE other notes as above apply
  template<typename InputT,
      typename Check = typename std::enable_if<
          _detail::template is_acceptable<InputT>::value &&
          _detail::constructible>::type,
      typename Enable = typename std::enable_if<
          !std::is_same<InputT, void>::value && _detail::is_fixed_span>::type>
  Collection(InputT (&d)[_detail::span_size])
  {
    create_from_array(d, super_type::SPAN_SIZE,
        typename _detail::template is_plain<InputT>());
  }

  // Avoid std::allocator type; check for const_iterator member.
  // Disable conversion to an std::initializer_list (also has const_iterator).
  // (in C++11 std::vector<T>::operator= is overloaded to take either a
  // std::vector<T> or an std::initializer_list<T>).

  // move to container
  // (clear/empty this one)
  template<typename Container,
      typename Check = typename std::enable_if<
          _detail::template is_acceptable<
              typename std::decay<Container>::type::value_type>::value &&
          sizeof(typename std::decay<Container>::type::const_iterator) &&
          !trait::is_initializer_list<
              typename std::remove_reference<Container>::type>::value>::type>
  operator Container() &&
  {
    Container result;
    std::move(*this).move_to(std::inserter(result, result.end()));
    return result;
  }

  // generic method(s)

  GI_DISABLE_METHOD_NONE
  value_type &front() { return *this->begin(); }
  const value_type &front() const { return *this->begin(); }

#define GI_ENABLE_METHOD_ARRAY_NOT_NONE \
  template<typename Enable = void, \
      typename Check = typename std::enable_if< \
          std::is_void<Enable>::value && \
          std::is_pointer<typename super_type::const_iterator>::value && \
          !std::is_same<Transfer, transfer_none_t>::value>::type>

#define GI_ENABLE_METHOD_ARRAY \
  template<typename Enable = void, \
      typename Check = typename std::enable_if< \
          std::is_void<Enable>::value && \
          std::is_pointer<typename super_type::const_iterator>::value>::type>

  GI_ENABLE_METHOD_ARRAY_NOT_NONE
  value_type &back() { return *(this->end() - 1); }
  GI_ENABLE_METHOD_ARRAY
  const value_type &back() const { return *(this->end() - 1); }

  GI_ENABLE_METHOD_ARRAY_NOT_NONE
  value_type *data() { return this->begin(); }
  GI_ENABLE_METHOD_ARRAY
  const value_type *data() const { return this->begin(); }

  GI_ENABLE_METHOD_ARRAY_NOT_NONE
  value_type &operator[](size_type pos) { return *(this->begin() + pos); }
  GI_ENABLE_METHOD_ARRAY
  const value_type &operator[](size_type pos) const
  {
    return *(this->begin() + pos);
  }

  GI_ENABLE_METHOD_ARRAY_NOT_NONE
  value_type &at(size_type pos)
  {
    if (pos >= this->size())
      try_throw(std::out_of_range("Collection::at"));
    return *(this->begin() + pos);
  }
  GI_ENABLE_METHOD_ARRAY
  const value_type &at(size_type pos) const
  {
    if (pos >= this->size())
      try_throw(std::out_of_range("Collection::at"));
    return *(this->begin() + pos);
  }

  GI_ENABLE_METHOD_ARRAY
  void resize(size_type s)
  {
    auto size = this->size();
    if (s < size) {
      this->erase(this->begin() + s, this->end());
    } else if (s > size) {
      auto cnt = s - size;
      while (cnt) {
        this->push_back({});
        --cnt;
      }
    }
  }

  GI_DISABLE_METHOD_NONE
  void swap(Collection &other)
  {
    std::swap((super_type &)*this, (super_type &)other);
  }

#undef GI_ENABLE_METHOD_ARRAY
#undef GI_ENABLE_METHOD_ARRAY_NOT_NONE
  // in unwrap below;
  // refcnt based cases support all sorts of transfer,
  // otherwise transfer must match the Transfer encoded in type

  // r-value case; data can be snatched from this instance
  template<typename ReqTransfer,
      typename std::enable_if<std::is_same<Transfer, ReqTransfer>::value ||
                              list_ops::refcnt>::type * = nullptr>
  typename _detail::DataType _unwrap(const ReqTransfer &t) &&
  {
    auto l = this->data_;
    // be nice to subtype below
    // avoid yanking away possibly owned value
    if (t.value != transfer_none.value)
      this->data_ = nullptr;
    return l;
  }

  // l-value case; no snatching, so only transfer none (if not refcnt)
  template<typename ReqTransfer,
      typename std::enable_if<
          list_ops::refcnt ||
          (std::is_same<Transfer, ReqTransfer>::value &&
              std::is_same<ReqTransfer, transfer_none_t>::value)>::type * =
          nullptr>
  typename _detail::DataType _unwrap(const ReqTransfer &t) &
  {
    if (list_ops::refcnt)
      return ((t.value != transfer_none.value) && this->data_)
                 ? list_ops::ref(this->data_)
                 : this->data_;
    // must be none transfer in this case
    return this->data_;
  }

  template<typename CppType, typename ReqTransfer,
      typename std::enable_if<std::is_same<Transfer, ReqTransfer>::value &&
                              !_detail::is_span>::type * = nullptr>
  static CppType _wrap(const ListType *obj, const ReqTransfer &t)
  {
    static_assert(sizeof(CppType) == sizeof(self_type), "invalid wrap");
    static_assert(std::is_base_of<self_type, CppType>::value, "invalid wrap");
    self_type w(const_cast<ListType *>(obj), t.value);
    return std::move(*static_cast<CppType *>(&w));
  }

  // special case; wrap of 2 arguments (ptr and size)
  // (could be combined in a single span,
  // but it would have to be unpacked anyway)
  template<typename CppType, typename CType, typename ReqTransfer,
      typename std::enable_if<
          std::is_same<Transfer, ReqTransfer>::value && _detail::is_span &&
          _detail::template is_compatible<CType>::value>::type * = nullptr>
  static CppType _wrap(CType *obj, int s, const ReqTransfer &)
  {
    static_assert(sizeof(CppType) == sizeof(self_type), "invalid wrap");
    static_assert(std::is_base_of<self_type, CppType>::value, "invalid wrap");
    // select internal protected constructor
    self_type w(const_cast<ElCType *>(obj), s, nullptr);
    return std::move(*static_cast<CppType *>(&w));
  }

  // likewise, ZT
  template<typename CppType, typename CType, typename ReqTransfer,
      typename std::enable_if<
          std::is_same<Transfer, ReqTransfer>::value && _detail::is_span &&
          _detail::span_size == SpanType::ZT>::type * = nullptr>
  static CppType _wrap(CType *obj, const ReqTransfer &)
  {
    return _wrap<CppType>(obj, -1, ReqTransfer());
  }

  template<typename CppType,
      typename std::enable_if<!std::is_void<CppType>::value &&
                              list_ops::refcnt>::type * = nullptr>
  static CppType _get_value(const GValue *v)
  {
    auto wv = std::is_same<Transfer, transfer_none_t>::value()
                  ? g_value_get_boxed(v)
                  : g_value_dup_boxed(v);
    return _wrap<CppType>((ListType *)(wv), Transfer());
  }

  template<typename CppType,
      typename std::enable_if<!std::is_void<CppType>::value &&
                              !list_ops::refcnt>::type * = nullptr>
  static CppType _get_value(const GValue *v)
  {
    // tracked as raw gpointer
    auto wv = g_value_get_pointer(v);
    return _wrap<CppType>((ElCType *)(wv), Transfer());
  }

  void _set_value(GValue *v)
  {
    if (list_ops::refcnt) {
      g_value_set_boxed(v, this->gobj_());
    } else {
      g_value_set_pointer(v, this->gobj_());
    }
  }
};

// add additional owner tracking data
// (see below for reason of simple/silly separate type)
struct OwnerData : public Container
{
  bool own_ = false;
};

template<typename... Args>
struct NotifierThunk;

// should only end up used with transfer_none
template<typename ListType, typename T, typename Transfer = transfer_none_t>
class CollectionHolder : public Collection<ListType, T, Transfer,
                             NotifierThunk<ListType, T, Transfer>, OwnerData>
{
  // ownership tracked by OwnerData
  // as notified by helper which needs access
  friend struct NotifierThunk<ListType, T, Transfer>;
  // to call here
  void _updated(bool create)
  {
    if (create) {
      this->own_ = true;
    } else {
      this->~CollectionHolder();
    }
  }

public:
  // NOTE OwnerData is inserted as a lower base class to ensure it is
  // initialized soon enough (before the using'ed constructor will notify
  // of creation)
  using super_type = Collection<ListType, T, Transfer,
      NotifierThunk<ListType, T, Transfer>, OwnerData>;

  using super_type::super_type;

  // only allowed (limited) move
  // (even if some parents might allow more)
  CollectionHolder(CollectionHolder &&other) : super_type(std::move(other))
  {
    this->own_ = other.own_;
    other.own_ = false;
  }

  CollectionHolder &operator=(CollectionHolder &&other)
  {
    if (this != &other) {
      this->~CollectionHolder();
      (super_type &)(*this) = std::move(other);
      this->own_ = other.own_;
      other.own_ = false;
    }
    return *this;
  }

  ~CollectionHolder()
  {
    // only need to act if super type does not handle (owns) anyway
    // only free/clear list, items not owned
    if (std::is_same<Transfer, transfer_none_t>::value && this->own_ &&
        this->data_) {
      this->_steal();
      this->own_ = false;
    }
  }
};

#undef GI_DISABLE_METHOD_NONE

template<typename... Args>
struct NotifierThunk
{
  static constexpr bool construct_none = true;
  static void _updated(Container *c, bool create)
  {
    using CT = CollectionHolder<Args...>;
    return static_cast<CT *>(c)->_updated(create);
  }
};

struct glib_deleter
{
  template<typename T>
  void operator()(T *p)
  {
    g_free(p);
  }
};

template<typename T>
using unique_ptr = std::unique_ptr<T, glib_deleter>;

template<typename ListType, typename ElType, typename Transfer,
    typename Enable = void>
struct ListAcceptorImpl
{
  using type = Collection<ListType, ElType, Transfer>;
};

template<typename ListType, typename ElType, typename Transfer>
struct ListAcceptorImpl<ListType, ElType, Transfer,
    typename std::enable_if<
        (std::is_same<ListType, ::GList>::value ||
            std::is_same<ListType, ::GSList>::value) &&
        std::is_same<Transfer, transfer_none_t>::value>::type>
{
  using type = CollectionHolder<ListType, ElType, Transfer>;
};

} // namespace detail

using detail::unwrap;

using ZTSpan = detail::Span<detail::SpanType::ZT>;
using DSpan = detail::Span<detail::SpanType::DYNAMIC>;
template<int S>
using FSpan = detail::Span<S>;

// only the full_transfer version is really safe for general use
// but let's export it all
using detail::Collection;

// type to use in (input) parameter declaration
// use basic type for ref-cases, otherwise need Holder subtype
// NOTE add a workaround hack;
// use full transfer for a none transfer HashTable parameter
// (as annotation there might be bogus; looking at add gst_uri_set_query_table)
// (and if the annotiation is not bogus, the full should work out ok-ish)
// NOTE list_ops is used directly hereto avoid instantiation of Collection
// (as that might otherwise occur during declaration)
template<typename ListType, typename T, typename Transfer>
using CollectionParameter = typename std::conditional<
    std::is_same<ListType, ::GHashTable>::value &&
        std::is_same<Transfer, transfer_none_t>::value,
    detail::Collection<ListType, T, transfer_full_t>,
    typename std::conditional<detail::list_ops<ListType, T, Transfer>::refcnt,
        detail::Collection<ListType, T, Transfer>,
        detail::CollectionHolder<ListType, T, Transfer>>::type>::type;

namespace traits
{
// (many to one) map from Cpp to C type (so no other way around)
template<typename ListType, typename El, typename Transfer>
struct ctype<detail::Collection<ListType, El, Transfer>>
{
  using type =
      typename detail::Collection<ListType, El, Transfer>::_detail::DataType;
};

template<typename ListType, typename El, typename Transfer>
struct ctype<const detail::Collection<ListType, El, Transfer>>
{
  using type = const typename detail::Collection<ListType, El,
      Transfer>::_detail::DataType;
};
} // namespace traits

} // namespace gi

#endif // GI_CONTAINER_HPP
