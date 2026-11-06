#pragma once
/**
 * @file WikiTitleLookup.h
 * @brief 記事タイトルの表記ゆれの扱い
 *
 * - タイトルは完全一致で引く（大文字小文字だけが違う別記事があるため）。
 * - Wikipedia は先頭1文字の大文字小文字を区別しないため、先頭が ASCII 小文字なら
 *   大文字にした候補も引く。
 * - 転送ページ（リダイレクト）のタイトルはリンクグラフの辞書に入っていて、
 *   転送先の記事が引ける。
*/

#include <string>
#include <vector>

namespace game::systems::wiki_title_lookup {

/**
 * @brief 検索に使うタイトル候補を優先順に返します。
 * @param normalized 空白をアンダースコアに置き換えたタイトル
*/
inline std::vector<std::string> TitleCandidates(const std::string &normalized) {
  std::vector<std::string> candidates;
  if (normalized.empty()) {
    return candidates;
  }
  candidates.push_back(normalized);
  const char first = normalized.front();
  if (first >= 'a' && first <= 'z') {
    std::string upper = normalized;
    upper.front() = static_cast<char>(first - 'a' + 'A');
    candidates.push_back(std::move(upper));
  }
  return candidates;
}

} // namespace game::systems::wiki_title_lookup
