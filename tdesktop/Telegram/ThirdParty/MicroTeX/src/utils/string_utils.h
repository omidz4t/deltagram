#ifndef STRING_UTILS_H_INCLUDED
#define STRING_UTILS_H_INCLUDED

#include "../config.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <sstream>
#include <string>
#include <functional>
#include <map>
#include <vector>

namespace tex {

std::map<std::string, std::string> parseOption(const std::string& options);

/** Convert a value to string */
template<class T>
inline std::string tostring(T val) {
  std::ostringstream os;
  os << val;
  return os.str();
}

template<>
inline std::string tostring(wchar_t val) {
  char buf[16];
  // -1 for a character the current locale cannot represent; feeding it to
  // the string constructor as a length would ask for size_t(-1) bytes.
  const auto len = wctomb(buf, val);
  return (len > 0) ? std::string(buf, len) : std::string("?");
}

/** Convert a value to wide string */
template<class T>
inline std::wstring towstring(T val) {
  std::wostringstream os;
  os << val;
  return os.str();
}

template<class T>
inline void valueof(const std::string& s, T& val) {
  std::stringstream ss;
  ss << s;
  ss >> val;
}

template<class T>
inline void valueof(const std::wstring& s, T& val) {
  std::wstringstream ss;
  ss << s;
  ss >> val;
}

inline bool str2int(const std::string& str, int& res, int radix) {
  char* endptr = nullptr;
  errno = 0;

  const long val = strtol(str.c_str(), &endptr, radix);

  if ((val == LONG_MAX || val == LONG_MIN) && errno == ERANGE)
    return false;
  // On platforms where long is wider than int, a value like \char's
  // "4294967295" would silently truncate in the cast below. Reject it as
  // unparseable instead, so callers see the same "not a number" they get
  // for any other out-of-range input.
  if (val < INT_MIN || val > INT_MAX)
    return false;

  res = static_cast<int>(val);
  return endptr == str.c_str() + str.size();
}

/**
 * Character tests restricted to ASCII.
 *
 * The <cctype> functions are only defined for arguments representable as an
 * unsigned char, plus EOF; anything else indexes the C library's
 * classification table out of bounds. Both character types in this codebase
 * can leave that domain: char is signed on x86, so a UTF-8 continuation byte
 * arrives as a negative number, and a wchar_t taken straight from the formula
 * reaches 0xFFFF. The three C libraries then disagree -- glibc reads past its
 * table, Windows answers 0, macOS answers with the real Unicode class -- so
 * the same formula classified characters differently on each platform.
 *
 * Restricting the tests to ASCII costs nothing where they are used: TeX
 * command names are ASCII by construction (getCommand() accepts only
 * [a-zA-Z@]), and so are unit suffixes, colour names and roman numerals.
 */
inline bool isAsciiAlpha(wchar_t c) {
  return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

inline bool isAsciiDigit(wchar_t c) {
  return c >= L'0' && c <= L'9';
}

inline bool isAsciiLower(wchar_t c) {
  return c >= L'a' && c <= L'z';
}

inline wchar_t asciiToUpper(wchar_t c) {
  return isAsciiLower(c) ? wchar_t(c - L'a' + L'A') : c;
}

inline bool isAsciiSpace(char c) {
  return std::isspace(static_cast<unsigned char>(c)) != 0;
}

/** Transform a string to lowercase */
inline std::string& tolower(std::string& src) {
  std::transform(src.begin(), src.end(), src.begin(), [](char c) {
    return char(std::tolower(static_cast<unsigned char>(c)));
  });
  return src;
}

inline std::wstring& tolower(std::wstring& src) {
  std::transform(src.begin(), src.end(), src.begin(), [](wchar_t c) {
    return (c >= L'A' && c <= L'Z') ? wchar_t(c - L'A' + L'a') : c;
  });
  return src;
}

/** Ignore left side whitespace in a string */
inline std::string& ltrim(std::string& s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](char c) {
    return !isAsciiSpace(c);
  }));
  return s;
}

/** Ignore right side whitespace in a string */
inline std::string& rtrim(std::string& s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), [](char c) {
    return !isAsciiSpace(c);
  }).base(), s.end());
  return s;
}

/** Ignore left and right side whitespace in a string */
inline std::string& trim(std::string& s) {
  return ltrim(rtrim(s));
}

/** Split string with specified delimiter */
inline void split(const std::string& str, char del, std::vector<std::string>& res) {
  std::stringstream ss(str);
  std::string tok;
  while (std::getline(ss, tok, del)) res.push_back(tok);
}

inline bool startswith(const std::string& str, const std::string& cmp) {
  return str.find(cmp) == 0;
}

inline bool endswith(const std::string& str, const std::string& cmp) {
  return str.rfind(cmp) == (str.length() - cmp.length());
}

inline bool startswith(const std::wstring& str, const std::wstring& cmp) {
  return str.find(cmp) == 0;
}

inline bool endswith(const std::wstring& str, const std::wstring& cmp) {
  return str.rfind(cmp) == (str.length() - cmp.length());
}

/** Split string with delimiter */
class StrTokenizer {
private:
  std::string _str;
  std::string _del;
  bool _ret;
  int _pos;

public:
  StrTokenizer(std::string str);

  StrTokenizer(std::string  str, std::string  del, bool ret = false);

  int count();

  std::string next();
};

/**
 * Returns a replacement string for the given one that has all backslashes
 * and dollar signs escaped
 */
inline std::string& quotereplace(const std::string& src, std::string& out) {
  for (size_t i = 0; i < src.length(); i++) {
    char c = src[i];
    if (c == '\\' || c == '$') out.append(1, '\\');
    out.append(1, c);
  }
  return out;
}

inline std::wstring& quotereplace(const std::wstring& src, std::wstring& out) {
  for (size_t i = 0; i < src.length(); i++) {
    wchar_t c = src[i];
    if (c == L'\\' || c == L'$') out.append(1, L'\\');
    out.append(1, c);
  }
  return out;
}

/** Replace string with specified string in the first */
inline std::string& replacefirst(std::string& src, const std::string& from, const std::string& to) {
  size_t start = src.find(from);
  if (start == std::string::npos) return src;
  src.replace(start, from.length(), to);
  return src;
}

inline std::string& replaceall(std::string& src, const std::string& from, const std::string& to) {
  if (from.empty()) return src;
  size_t start = 0;
  while ((start = src.find(from, start)) != std::string::npos) {
    src.replace(start, from.length(), to);
    start += to.length();
  }
  return src;
}

inline std::wstring& replacefirst(std::wstring& src, const std::wstring& from, const std::wstring& to) {
  size_t start = src.find(from);
  if (start == std::wstring::npos) return src;
  src.replace(start, from.length(), to);
  return src;
}

inline std::wstring& replaceall(std::wstring& src, const std::wstring& from, const std::wstring& to) {
  if (from.empty()) return src;
  size_t start = 0;
  while ((start = src.find(from, start)) != std::wstring::npos) {
    src.replace(start, from.length(), to);
    start += to.length();
  }
  return src;
}

}  // namespace tex

#endif
