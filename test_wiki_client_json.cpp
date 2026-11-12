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

  const std::string info =
      "{\"query\":{\"pages\":[{\"pageid\":1,\"title\":\"DVD\","
      "\"length\":103003},{\"ns\":0,\"title\":\"無い記事\",\"missing\":true},"
      "{\"pageid\":2,\"title\":\"ドイツ\",\"length\": 221033}]}}";
  const auto lengths = game::systems::wiki_json::ParsePageInfoLengths(info);
  CHECK_TRUE(lengths.size() == 2 && lengths[0] == 103003 &&
                 lengths[1] == 221033,
             "prop=info応答から存在する記事の記事長だけを取り出す");

  return EXIT_SUCCESS;
}
