#ifndef GENNS_HPP
#define GENNS_HPP

struct GeneratorContext;

#include <memory>
#include <vector>

class NamespaceGenerator
{
public:
  static std::shared_ptr<NamespaceGenerator> new_(
      GeneratorContext &ctx, const std::string &filename);

  virtual ~NamespaceGenerator() {}

  virtual std::string get_ns() const = 0;

  virtual std::vector<std::string> get_dependencies() const = 0;

  virtual std::string process_tree(
      const std::vector<std::string> &dep_headers) = 0;
};

#endif // GENNS_HPP
