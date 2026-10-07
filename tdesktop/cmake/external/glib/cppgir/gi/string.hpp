#ifndef GI_STRING_HPP
#define GI_STRING_HPP

#include "base.hpp"

#ifdef __has_builtin
#define GI_HAS_BUILTIN(x) __has_builtin(x)
#else
#define GI_HAS_BUILTIN(x) 0
#endif

GI_MODULE_EXPORT
namespace gi
{
namespace convert
{
// generic template; specialize as needed where appropriate
template<typename From, typename To, class Enable = void>
struct converter
{
  // fail in explanatory way if we end up here
  static To convert(const From &)
  {
    static_assert(!std::is_void<Enable>::value, "unknown type conversion");
    return To();
  }
};

// implementation should provide some types
template<typename From, typename To, class Enable = void>
struct converter_base : public std::true_type
{
  typedef From from_type;
  typedef To to_type;
};

// conversion check for complete type
template<typename From, typename To, typename Enable = void>
struct is_convertible_impl : public std::false_type
{};

template<typename From, typename To>
struct is_convertible_impl<From, To,
    typename std::enable_if<std::is_base_of<To, From>::value>::type>
    : public std::false_type
{};

template<typename From, typename To>
struct is_convertible_impl<From, To,
    typename std::enable_if<!std::is_base_of<To, From>::value &&
                            std::is_pointer<typename converter<From,
                                To>::from_type *>::value>::type>
    : public std::true_type
{};

template<typename From, typename To, bool complete>
struct is_convertible_pre
{
  using type = std::false_type;
};

template<typename From, typename To>
struct is_convertible_pre<From, To, true>
{
  using type = is_convertible_impl<From, To>;
};

// check whether conversion possible
// reject incomplete forward declared types
template<typename From, typename To, typename Enable = void>
struct is_convertible : public is_convertible_pre<From, To,
                            gi::traits::is_type_complete<To>::value>::type
{};
} // namespace convert

namespace detail
{
// tag

struct String
{};

template<typename Transfer>
struct StringFuncs
{
  static void _deleter(char *&p)
  {
    if (Transfer().value)
      g_free(p);
    p = nullptr;
  }
  static void _copy(const char *p)
  {
    return Transfer().value ? g_strdup(p) : p;
  }
};

template<typename Transfer>
class cstr : public String
{
  using self_type = cstr;

protected:
  using _member_type =
      typename std::conditional<std::is_same<Transfer, transfer_full_t>::value,
          char, const char>::type;

  _member_type *data_ = nullptr;

  void clear()
  {
    if (Transfer().value && data_)
      g_free((char *)data_);
    data_ = nullptr;
  }

public:
  using traits_type = std::char_traits<char>;
  using value_type = char;
  using pointer = char *;
  using const_pointer = const char *;
  using reference = char &;
  using const_reference = const char &;
  using const_iterator = const char *;
  using iterator = const_iterator;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;
  using reverse_iterator = const_reverse_iterator;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;

  using view_type = cstr<transfer_none_t>;
  static constexpr bool is_view_type =
      !std::is_same<Transfer, transfer_full_t>::value;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr cstr() noexcept : data_(nullptr) {}

  constexpr cstr(std::nullptr_t) noexcept : data_(nullptr) {}

  // const usually means none, so only on view type
  // also, no arbitrary size is accepted here
  template<typename Enable = void,
      typename std::enable_if<std::is_same<Enable, void>::value &&
                              is_view_type>::type * = nullptr>
  constexpr cstr(const char *data) : data_(data)
  {}

  // a single pointer, optionally specify ownership
  // behave like string by default, assume no ownership of incoming pointer
  template<typename LTransfer = transfer_none_t,
      typename std::enable_if<
          (std::is_same<LTransfer, transfer_full_t>::value ||
              std::is_same<LTransfer, transfer_none_t>::value) &&
          !is_view_type>::type * = nullptr>
  cstr(char *data, const LTransfer &t)
      : data_((t.value == transfer_full.value) || !data ? data : g_strdup(data))
  {}

  // pointer along with size
  // always behave like string and make a copy
  template<typename Enable = void,
      typename std::enable_if<std::is_same<Enable, void>::value &&
                              !is_view_type>::type * = nullptr>
  cstr(const char *data, size_t len = npos)
      : data_(!data ? nullptr
                    : (len == npos ? g_strdup(data) : g_strndup(data, len)))
  {}

  // construct from any transfer variant
  template<typename OTransfer>
  cstr(const cstr<OTransfer> &s) : cstr(s.data())
  {}

  // accept from string, but NOT string_view as that may not be null-terminated
  template<typename Allocator>
  cstr(const std::basic_string<char, std::char_traits<char>, Allocator>
          &s) noexcept
      : cstr(s.data())
  {}

#if __cplusplus >= 201703L
  // some optional variants of above
  constexpr cstr(std::nullopt_t) noexcept : data_(nullptr) {}

  template<typename Allocator>
  cstr(const std::optional<
      std::basic_string<char, std::char_traits<char>, Allocator>> &s) noexcept
      : cstr(s ? s.value().data() : nullptr)
  {}
#endif

  // hook extensible conversion
  // (avoid instantiation and confusion with self and base types)
  template<typename From,
      typename NoBase = typename std::enable_if<
          !std::is_base_of<self_type, From>::value>::type,
      typename Enable = typename std::enable_if<
          convert::is_convertible<From, self_type>::value>::type>
  cstr(const From &f) : cstr(convert::converter<From, self_type>::convert(f))
  {}

  // custom
  constexpr const_pointer gobj_() const { return data_; }

  explicit constexpr operator bool() const { return data_; }

  _member_type *release_()
  {
    auto tmp = this->data_;
    this->data_ = nullptr;
    return tmp;
  }

  // destruct / copy / assign
  ~cstr() { clear(); }

  cstr(const cstr &other) { *this = other; }

  cstr(cstr &&other) { *this = std::move(other); }

  cstr &operator=(const cstr &other)
  {
    if (this != &other) {
      clear();
      data_ = Transfer().value ? g_strdup(other.data_) : other.data_;
    }
    return *this;
  }

  cstr &operator=(cstr &&other)
  {
    if (this != &other) {
      clear();
      data_ = other.data_;
      other.data_ = nullptr;
    }
    return *this;
  }

  // deduced conversion to string(_view)
  template<typename Destination,
      typename Check = typename std::enable_if<
          convert::is_convertible<self_type, Destination>::value>::type>
  operator Destination() const
  {
    return convert::converter<self_type, Destination>::convert(*this);
  }

#if __cplusplus >= 201703L
  // unfortunately, conversion to std::optional picks Destination = std::string
  // (which can obviously not be excluded above, or as specialization)
  std::optional<std::string> opt_()
  {
    using To = std::optional<std::string>;
    return c_str() ? To({c_str(), size()}) : To(std::nullopt);
  }
#endif

  // usual string(view) stuff

  // iterators

  constexpr const_iterator begin() const noexcept { return data_; }

  constexpr const_iterator end() const noexcept
  {
    return data_ ? data_ + size() : nullptr;
  }

  constexpr const_iterator cbegin() const noexcept { return begin(); }

  constexpr const_iterator cend() const noexcept { return end(); }

  const_reverse_iterator rbegin() const noexcept
  {
    return const_reverse_iterator(end());
  }

  const_reverse_iterator rend() const noexcept
  {
    return const_reverse_iterator(begin());
  }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  const_reverse_iterator crend() const noexcept { return rend(); }

  // capacity

  constexpr size_type size() const noexcept
  {
#if GI_HAS_BUILTIN(__builtin_strlen) || \
    (defined(__GNUC__) && !defined(__clang__))
    return __builtin_strlen(data_);
#else
    return data_ ? strlen(data_) : 0;
#endif
  }

  constexpr size_type length() const noexcept { return size(); }

  constexpr size_type max_size() const noexcept
  {
    return std::numeric_limits<size_type>::max();
  }

  constexpr bool empty() const noexcept { return !data_ || *data_ == 0; }

  // access

  constexpr const_reference operator[](size_type i) const { return data_[i]; }

  constexpr const_reference at(size_type i) const
  {
    return i < size() ? data_[i]
                      : (try_throw(std::out_of_range("cstr::at")), data_[i]);
  }

  constexpr const_reference front() const { return data_[0]; }

  constexpr const_reference back() const { return data_[size() - 1]; }

  constexpr const_pointer data() const noexcept { return data_; }

  constexpr const_pointer c_str() const noexcept { return data_; }

  // modifiers

  // view only
  template<typename Enable = void,
      typename std::enable_if<std::is_same<Enable, void>::value &&
                              is_view_type>::type * = nullptr>
  constexpr void remove_prefix(size_type n)
  {
    data_ += n;
  }

  constexpr void swap(self_type &s) noexcept
  {
    std::swap(this->data_, s.data_);
  }

  // operations

  size_type copy(char *buf, size_type n, size_type pos = 0) const
  {
    auto s = size();
    if (pos > s)
      try_throw(std::out_of_range("cstr::copy"));
    size_type rlen = (std::min)(s - pos, n);
    if (rlen > 0) {
      const char *start = data_ + pos;
      traits_type::copy(buf, start, rlen);
    }
    return rlen;
  }

  int compare(view_type x) const noexcept
  {
    return g_strcmp0(data_, x.c_str());
  }

  int compare(const char *s) const { return compare(view_type(s)); }

  // find

  size_type find(view_type n, size_type pos = 0) const noexcept
  {
    auto s = size();
    auto os = n.size();
    if (os > s || pos > s - os)
      return npos;
    if (!os)
      return pos <= s ? pos : npos;
    auto loc = strstr(data_ + pos, n.data());
    return loc ? loc - data_ : npos;
  }

  size_type find(char c, size_type pos = 0) const noexcept
  {
    if (pos >= size())
      return npos;
    auto loc = strchr(data_ + pos, c);
    return loc ? loc - data_ : npos;
  }

  size_type find(const char *s, size_type pos = 0) const
  {
    return find(view_type(s), pos);
  }

  size_type rfind(view_type n, size_type pos = npos) const noexcept
  {
    auto s = size();
    if (!s)
      return npos;
    auto os = n.size();
    if (!os)
      return pos == npos ? s : pos;
    auto loc = g_strrstr_len(c_str(), pos, n.c_str());
    return loc ? loc - data_ : npos;
  }

  size_type rfind(char c, size_type pos = npos) const noexcept
  {
    // not quite efficient, but anyways
    char str[] = {c, 0};
    return rfind(str, pos);
  }

  size_type rfind(const char *s, size_type pos = npos) const
  {
    return rfind(view_type(s), pos);
  }

  // find_first_of variants
  // only provide those if std helps us out
#if __cplusplus >= 201703L
  size_type find_first_of(view_type s, size_type pos = 0) const noexcept
  {
    return std::string_view(data_).find_first_of(s.data(), pos);
  }

  size_type find_first_of(char c, size_type pos = 0) const noexcept
  {
    return find(c, pos);
  }

  size_type find_first_of(const char *s, size_type pos = 0) const
  {
    return find_first_of(view_type(s), pos);
  }

  size_type find_last_of(view_type s, size_type pos = npos) const noexcept
  {
    return std::string_view(data_).find_last_of(s.data(), pos);
  }

  size_type find_last_of(char c, size_type pos = npos) const noexcept
  {
    return rfind(c, pos);
  }

  size_type find_last_of(const char *s, size_type pos = npos) const
  {
    return find_last_of(view_type(s), pos);
  }

  size_type find_first_not_of(view_type s, size_type pos = 0) const noexcept
  {
    return std::string_view(data_).find_first_not_of(s.data(), pos);
  }

  size_type find_first_not_of(char c, size_type pos = 0) const noexcept
  {
    return std::string_view(data_).find_first_not_of(c, pos);
  }

  size_type find_first_not_of(const char *s, size_type pos = 0) const
  {
    return find_first_not_of(view_type(s), pos);
  }

  size_type find_last_not_of(view_type s, size_type pos = npos) const noexcept
  {
    return std::string_view(data_).find_last_not_of(s.data(), pos);
  }

  size_type find_last_not_of(char c, size_type pos = npos) const noexcept
  {
    return std::string_view(data_).find_last_not_of(c, pos);
  }

  size_type find_last_not_of(const char *s, size_type pos = npos) const
  {
    return find_last_not_of(view_type(s), pos);
  }
#endif
};

using _string_view = cstr<transfer_none_t>;

inline bool
operator==(_string_view x, _string_view y) noexcept
{
  return x.compare(y) == 0;
}

inline bool
operator!=(_string_view x, _string_view y) noexcept
{
  return !(x == y);
}

inline bool
operator<(_string_view x, _string_view y) noexcept
{
  return x.compare(y) < 0;
}

inline bool
operator>(_string_view x, _string_view y) noexcept
{
  return y < x;
}

inline bool
operator<=(_string_view x, _string_view y) noexcept
{
  return !(y < x);
}

inline bool
operator>=(_string_view x, _string_view y) noexcept
{
  return !(x < y);
}

#ifndef GI_NO_STRING_IOS
inline std::ostream &
operator<<(std::ostream &o, _string_view sv)
{
  // backwards compatibility; behave similar to empty string
  return o << (sv ? sv.c_str() : "");
}
#endif

// purpose of silly Delay is to avoid gcc premature instantiation of converter
// (in the hook constructor of base class with cstring as From ??)
template<typename Delay = void>
class cstring_d : public cstr<transfer_full_t>
{
  using self_type = cstring_d;
  using super_type = cstr<transfer_full_t>;

public:
  using super_type::super_type;

  // if all other construction fails, try to pass through string
  template<typename... Args,
      typename NoConvert = typename std::enable_if<
          !std::is_constructible<super_type, Args...>::value>::type,
      typename Enable = typename std::enable_if<
          std::is_constructible<std::string, Args...>::value>::type>
  cstring_d(Args &&...args)
      : super_type(std::string(std::forward<Args>(args)...))
  {}

  constexpr pointer gobj_() { return data_; }

  // re-use string
  template<typename... Args>
  self_type &assign(Args &&...t)
  {
    return *this = std::string().assign(std::forward<Args>(t)...);
  }

  // access

  using super_type::at;
  constexpr reference at(size_type i)
  {
    return i < size() ? data_[i]
                      : (try_throw(std::out_of_range("cstr::at")), data_[i]);
  }

  using super_type::front;
  constexpr reference front() { return data_[0]; }

  using super_type::back;
  constexpr reference back() { return data_[size() - 1]; }

  using super_type::data;
  constexpr pointer data() noexcept { return data_; }

  // iterators

  using super_type::begin;
  constexpr iterator begin() noexcept { return data_; }

  using super_type::end;
  constexpr iterator end() noexcept { return data_ ? data_ + size() : nullptr; }

  using super_type::rbegin;
  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }

  using super_type::rend;
  reverse_iterator rend() noexcept { return reverse_iterator(begin()); }

  // operations

  using super_type::clear;

  void push_back(char c)
  {
    char str[] = {c, 0};
    self_type n{
        g_strconcat(c_str() ? c_str() : "", str, (char *)NULL), transfer_full};
    swap(n);
  }

  void pop_back()
  {
    auto s = size();
    if (s)
      at(s - 1) = 0;
  }

  self_type &append(const char *str)
  {
    self_type n{
        g_strconcat(c_str() ? c_str() : "", str, (char *)NULL), transfer_full};
    swap(n);
    return *this;
  }

  template<typename Transfer>
  self_type &append(const cstr<Transfer> &str)
  {
    return append(str.data());
  }

  // otherwise delegate
  template<typename... Args>
  self_type &append(Args &&...args)
  {
    std::string s;
    s.append(std::forward<Args>(args)...);
    // make sure to select the desired variant
    return append((const char *)s.c_str());
  }

  // likewise for +=
  self_type &operator+=(char c)
  {
    push_back(c);
    return *this;
  }

  template<typename T>
  self_type &operator+=(const T &o)
  {
    return append(o);
  }

  self_type substr(size_type pos = 0, size_type n = npos) const
  {
    auto l = size();
    return (pos > l)
               ? (try_throw(std::out_of_range("cstr::substr")), self_type())
               : self_type(data_ + pos, std::min(n, l - pos));
  }

  // indeed some are lacking/skipped
  // only minimal compatibility

  // custom; align with other cases
  self_type copy_() { return substr(0); }
};

using cstring = cstring_d<>;

// likewise; (delayed) view type
template<typename Delay = void>
class cstring_v_d : public cstr<transfer_none_t>
{
  using self_type = cstring_v_d;
  using super_type = cstr<transfer_none_t>;

public:
  using super_type::super_type;

  // primary reason for subtype
  // provide copy/upgrade to owning variant
  cstring copy_() { return {g_strdup(this->data()), transfer_full}; }
};

using cstring_v = cstring_v_d<>;

inline cstring
operator+(const _string_view x, const _string_view y) noexcept
{
  if (!x)
    return {g_strdup(y.c_str()), transfer_full};
  if (!y)
    return {g_strdup(x.c_str()), transfer_full};
  return {g_strconcat(x.c_str(), y.c_str(), (char *)NULL), transfer_full};
}

inline cstring
operator+(const _string_view x, char y) noexcept
{
  if (!x)
    return {1, y};
  char str[] = {y, 0};
  return x + str;
}

inline cstring
operator+(char x, const _string_view y) noexcept
{
  if (!y)
    return {1, x};
  char str[] = {x, 0};
  return str + y;
}

// add convenient conversions

// local helper traits used below
namespace trait
{
template<typename T, std::size_t SIZE, typename Enable = void>
struct has_data_member : std::false_type
{};

template<typename T, std::size_t SIZE>
struct has_data_member<T, SIZE,
    typename std::enable_if<
        std::is_pointer<decltype(std::declval<T>().c_str())>::value>::type>
    : std::integral_constant<bool, sizeof(*std::declval<T>().c_str()) == SIZE>
{};

template<typename T, typename Enable = void>
struct has_size_member : std::false_type
{};

template<typename T>
struct has_size_member<T, typename std::enable_if<std::is_integral<
                              decltype(std::declval<T>().size())>::value>::type>
    : std::true_type
{};

template<typename T>
struct is_string_type
    : public std::integral_constant<bool,
          has_data_member<T, 1>::value && has_size_member<T>::value>
{};

} // namespace trait

} // namespace detail

namespace convert
{
template<typename From>
struct converter<From, detail::cstr<transfer_full_t>,
    typename std::enable_if<detail::trait::is_string_type<From>::value>::type>
    : public converter_base<From, detail::cstr<transfer_full_t>>
{
  static detail::cstring convert(const From &v)
  {
    return {(char *)v.c_str(), v.size()};
  }
};

// to a typical string(_view) case
// (avoid conflict with above)
// the traits_type (tries to) restricts this to std::string(_view)
// not doing so might conveniently allow conversion to other types as well.
// however, this would also allow e.g. QByteArray, which comes with an operator+
// in global namespace (also selected by non-ADL lookup),
// which then results in ambiguous overload
// (with the operator+ that is provided above)
template<typename Transfer, typename To>
struct converter<detail::cstr<Transfer>, To,
    typename std::enable_if<
        !std::is_base_of<detail::String, To>::value &&
        !std::is_same<typename To::traits_type, void>::value &&
        std::is_constructible<To, const char *, size_t>::value>::type>
    : public converter_base<detail::cstr<Transfer>, To>
{
  static To convert(const detail::cstr<Transfer> &v)
  {
    return v.c_str() ? To{(char *)v.c_str(), v.size()} : To();
  }
};

#if __cplusplus >= 201703L
// to an std::optional string(_view) case
template<typename Transfer, typename To>
struct converter<detail::cstr<Transfer>, To,
    typename std::enable_if<std::is_constructible<To, std::in_place_t,
        const char *, size_t>::value>::type>
    : public converter_base<detail::cstr<Transfer>, To>
{
  static To convert(const detail::cstr<Transfer> &v)
  {
    abort();
    return v.c_str() ? To{std::in_place, (char *)v.c_str(), v.size()} : To();
  }
};
#endif

} // namespace convert

using detail::cstring;
using detail::cstring_v;

// sanity check; match C counterpart
static_assert(sizeof(cstring) == sizeof(char *), "");
static_assert(sizeof(cstring_v) == sizeof(char *), "");

namespace traits
{
template<>
struct ctype<const gi::cstring, void>
{
  typedef const char *type;
};

template<>
struct ctype<gi::cstring, void>
{
  typedef char *type;
};

template<>
struct ctype<const gi::cstring_v, void>
{
  typedef const char *type;
};

template<>
struct ctype<gi::cstring_v, void>
{
  typedef char *type;
};

template<>
struct cpptype<char *, transfer_full_t>
{
  using type = gi::cstring;
};

template<>
struct cpptype<char *, transfer_none_t>
{
  using type = gi::cstring_v;
};

} // namespace traits

} // namespace gi

// specialize std::hash for suitable use
GI_MODULE_EXPORT
namespace std
{
template<>
struct hash<gi::cstring>
{
  typedef gi::cstring argument_type;
  typedef std::size_t result_type;
  result_type operator()(argument_type const &s) const
  {
    return s.c_str() ? g_str_hash(s.c_str()) : 0;
  }
};

template<>
struct hash<gi::cstring_v>
{
  typedef gi::cstring_v argument_type;
  typedef std::size_t result_type;
  result_type operator()(argument_type const &s) const
  {
    return s.c_str() ? g_str_hash(s.c_str()) : 0;
  }
};

} // namespace std

#endif // GI_STRING_HPP
