#include <cstring>
#include <cstdio>
#include <string>
#include "emoji_suggestions.h"
int main() {
 for (const auto query : {u"heart", u"smile", u"thumb"}) {
  const auto text = std::u16string(query);
  const auto result = Ui::Emoji::GetSuggestions({reinterpret_cast<const Ui::Emoji::utf16char*>(text.data()), text.size()});
  if (result.empty()) { std::puts("FAIL: missing bundled emoji search result"); return 1; }
 }
 std::puts("PASS: bundled offline emoji search for heart, smile, and thumb");
}
