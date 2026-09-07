#include "src/game/systems/WikiClientJson.h"
#include <cstdlib>
#include <iostream>

#define CHECK_TRUE(condition, message)                                         \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "[FAIL] " << message << "\n";                             \
      return EXIT_FAILURE;                                                     \
    }                                                                          \
    std::cout << "[PASS] " << message << "\n";                               \
  } while (false)

int main() {
  const std::string decoded =
      game::systems::wiki_json::DecodeString("日本\\u8a9e\\n記事");
  CHECK_TRUE(decoded == "日本語\\n記事", "JSON文字列のUnicodeを復元する");

  const std::string html =
      "<table><tr><td>A&amp;B</td></tr></table><p>本文</p>";
  const auto blocks = game::systems::wiki_json::ExtractTableBlocks(html);
  CHECK_TRUE(blocks.size() == 1, "HTMLからテーブルブロックを抽出する");

  const std::string plain =
      game::systems::wiki_json::StripHtmlToPlainText(blocks.front());
  CHECK_TRUE(plain.find("A&B") != std::string::npos,
             "HTMLタグと実体参照を平文へ変換する");

  return EXIT_SUCCESS;
}
