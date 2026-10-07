// Test for https://github.com/hunspell/hunspell/issues/1058
// suggest() must not return the input word as one of its suggestions when
// MAP-table substitution recursion happens to reach the original word.
#include <cstdio>
#include <string>
#include <vector>
#include "../src/hunspell/hunspell.hxx"

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s tests/gh1058\n", argv[0]);
    return 1;
  }
  std::string base(argv[1]);
  std::string aff = base + ".aff";
  std::string dic = base + ".dic";

  Hunspell h(aff.c_str(), dic.c_str());

  const std::string word("banana");
  if (!h.spell(word)) {
    fprintf(stderr, "FAIL: '%s' not in dictionary\n", word.c_str());
    return 1;
  }

  std::vector<std::string> sug = h.suggest(word);
  for (const auto& s : sug) {
    if (s == word) {
      fprintf(stderr, "FAIL: suggest returned the input word as a suggestion\n");
      return 1;
    }
  }

  fprintf(stdout, "PASS: gh1058\n");
  return 0;
}
