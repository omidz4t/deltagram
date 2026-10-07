#ifndef GENUTILS_HPP
#define GENUTILS_HPP

#include "common.hpp"

#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

class skip : public std::runtime_error
{
public:
  static const int TODO = 0;
  static const int OK = 0;
  static const int INVALID = 1;
  static const int IGNORE = 2;
  int cause;

  skip(const std::string &reason, int _cause = INVALID)
      : std::runtime_error(reason), cause(_cause)
  {}
};

class ScopeGuard
{
private:
  std::function<void()> cleanup_;

public:
  ScopeGuard(std::function<void()> &&cleanup) : cleanup_(std::move(cleanup)) {}

  ~ScopeGuard() noexcept(false)
  {
#if __cplusplus >= 201703L
    auto pending = std::uncaught_exceptions();
#else
    auto pending = std::uncaught_exception();
#endif
    try {
      cleanup_();
    } catch (...) {
      if (!pending)
        throw;
    }
  }
};

class NamespaceGuard
{
  typedef std::vector<std::string> ns_list;

private:
  ns_list ns_;
  std::ostream &out_;

  void push(const ns_list &ns)
  {
    for (auto &&v : ns) {
      out_ << "namespace " << v << " {" << std::endl << std::endl;
      ns_.push_back(v);
    }
  }

public:
  NamespaceGuard(std::ostream &_out) : out_(_out) {}

  void push(const std::string &ns, bool autodetect = true)
  {
    if (ns_.empty() && autodetect) {
      ns_list l{GI_NS, GI_REPOSITORY_NS};
      if (ns == GI_NS)
        l.pop_back();
      else if (ns != GI_REPOSITORY_NS)
        l.push_back(ns);
      push(l);
    } else {
      push(ns_list{ns});
    }
  }

  void pop(int count = -1)
  {
    while (!ns_.empty() && (count > 0 || count < 0)) {
      out_ << "} // namespace";
      if (ns_.back().size())
        out_ << ' ' << ns_.back();
      out_ << std::endl << std::endl;
      ns_.pop_back();
      --count;
    }
  }

  ~NamespaceGuard() { pop(); }
};

class Matcher
{
  std::regex pattern_;

  static std::vector<std::string> readfile(std::istream &input)
  {
    std::vector<std::string> result;
    for (std::string line; std::getline(input, line);) {
      result.emplace_back(line);
    }
    return result;
  }

public:
  Matcher(const std::vector<std::string> &paths,
      const std::string &patterns = {},
      const std::function<void(const std::string &)> &custom = {})
  {
    std::set<std::string> lines;
    for (auto &fpath : paths) {
      if (fpath.size()) {
        std::ifstream input(fpath);
        auto flines = readfile(input);
        logger(Log::DEBUG, "read {} lines from {}", flines.size(), fpath);
        std::copy(
            flines.begin(), flines.end(), std::inserter(lines, lines.begin()));
      }
    }
    if (patterns.size()) {
      std::istringstream iss(patterns);
      auto flines = readfile(iss);
      logger(Log::DEBUG, "read {} data lines", flines.size());
      std::copy(
          flines.begin(), flines.end(), std::inserter(lines, lines.begin()));
    }

    std::string lpatterns;
    for (auto &&l : lines) {
      if (!l.empty() && l[0] != '#') {
        // combine all expressions into 1 expression
        // so we only need to call once to check for matching
        lpatterns += (lpatterns.size() ? "|" : "") + l;
      } else if (custom && l.size() > 2 && l[0] == '#' && l[1] == '!') {
        custom(l);
      }
    }
    if (lpatterns.size())
      pattern_ = std::regex(lpatterns, std::regex::optimize);
  }

  static std::string format(const std::string &first, const std::string &second,
      const std::string name)
  {
    return first + ":" + second + ":" + name;
  }

  bool matches(const std::string &first, const std::string &second,
      const std::vector<std::string> &options) const
  {
    for (auto &&c : options) {
      // assemble string to match
      auto find = format(first, second, c);
      if (std::regex_match(find, pattern_))
        return true;
    }
    return false;
  }
};

inline bool
is_const(const std::string &ctype)
{
  return (ctype.find("const ") != ctype.npos) || ctype == "gconstpointer";
}

inline bool
is_volatile(const std::string &ctype)
{
  return ctype.find("volatile ") != ctype.npos;
}

inline int
get_pointer_depth(const std::string &ctype)
{
  int pointer = ctype.find("gpointer") != ctype.npos ||
                ctype.find("gconstpointer") != ctype.npos;
  // note that things like const gchar* const* are also possible
  return std::count(ctype.begin(), ctype.end(), GI_PTR) + !!pointer;
}

// check if @s is somehow special
// and mangle it a bit if so
std::string unreserve(const std::string &s, bool force = false);

#endif // GENUTILS_HPP
