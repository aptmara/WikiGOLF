// 配布ビルドのログから個人情報を伏せる処理（LogPrivacy）のテスト
#include "src/core/LogPrivacy.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void ExpectEq(const std::string &actual, const std::string &expected,
              const char *label) {
  if (actual != expected) {
    std::cerr << "FAILED: " << label << "\n  expected: " << expected
              << "\n  actual:   " << actual << '\n';
    ++g_failures;
  }
}

} // namespace

int main() {
  using core::PrivacyReplacement;
  using core::RedactPrivateText;

  const std::vector<PrivacyReplacement> replacements = {
      {"C:\\Users\\taro", "%USERPROFILE%", false},
      {"D:\\Games\\taro_games\\WikiGOLF", "<GAME_DIR>", false},
      {"taro", "<USER>", true},
      {"DESKTOP-TARO01", "<PC>", true},
  };

  ExpectEq(RedactPrivateText("open C:\\Users\\taro\\AppData\\x.ini", replacements),
           "open %USERPROFILE%\\AppData\\x.ini", "profile path");
  ExpectEq(RedactPrivateText("open c:/users/TARO/Documents/a.txt", replacements),
           "open %USERPROFILE%/Documents/a.txt", "slash and case");
  ExpectEq(RedactPrivateText("D:\\Games\\taro_games\\WikiGOLF\\Assets\\a.hlsl(3)",
                             replacements),
           "<GAME_DIR>\\Assets\\a.hlsl(3)", "game directory");
  ExpectEq(RedactPrivateText("user taro on DESKTOP-TARO01", replacements),
           "user <USER> on <PC>", "names");
  // 単語の途中は置き換えない
  ExpectEq(RedactPrivateText("Pitarou tarot", replacements), "Pitarou tarot",
           "word boundary");
  // 置き換え後の文字列は再び書き換えない（ユーザー名が "user" のとき）
  ExpectEq(RedactPrivateText("user at C:\\Users\\user\\a",
                             {{"C:\\Users\\user", "%USERPROFILE%", false},
                              {"user", "<USER>", true}}),
           "<USER> at %USERPROFILE%\\a", "no re-replacement");
  // 環境変数に無い別ユーザーのパスも伏せる。共有フォルダは残す
  ExpectEq(RedactPrivateText("C:\\Users\\hanako\\x and C:\\Users\\Public\\y", {}),
           "C:\\Users\\<USER>\\x and C:\\Users\\Public\\y", "other user path");
  ExpectEq(RedactPrivateText("'C:/Users/山田/x'", {}), "'C:/Users/<USER>/x'",
           "non-ascii user path");
  // 日本語の名前は単語の区切りを問わず伏せる
  ExpectEq(RedactPrivateText("山田のPC", {{"山田", "<USER>", true}}), "<USER>のPC",
           "non-ascii name");
  // 個人情報が無ければそのまま
  ExpectEq(RedactPrivateText("Texture generation failed for 東京", replacements),
           "Texture generation failed for 東京", "unchanged");

  // 実行環境から作る対象に、ユーザーフォルダとゲームのフォルダが入り、長い順に並ぶ
  const auto fromEnvironment =
      core::BuildPrivacyReplacementsFromEnvironment("C:\\Users\\someone\\Games\\WikiGOLF");
  bool hasGameDir = false;
  bool sorted = true;
  for (std::size_t i = 0; i < fromEnvironment.size(); ++i) {
    if (fromEnvironment[i].placeholder == "<GAME_DIR>") hasGameDir = true;
    if (i > 0 && fromEnvironment[i - 1].secret.size() < fromEnvironment[i].secret.size()) {
      sorted = false;
    }
  }
  if (!hasGameDir || !sorted) {
    std::cerr << "FAILED: environment replacements\n";
    ++g_failures;
  }

  return g_failures == 0 ? 0 : 1;
}
