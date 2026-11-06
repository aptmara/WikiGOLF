#pragma once
/**
 * @file WikiTitleLookup.h
 * @brief 記事タイトルからリンクグラフ上のページIDを引く処理です。
 *
 * - タイトルは完全一致で引き、title の索引を使う
 *   （COLLATE NOCASE は索引が効かず全件走査になり、大文字小文字だけが違う
 *   別記事も拾ってしまう）。
 * - Wikipedia は先頭1文字の大文字小文字を区別しないため、先頭が ASCII 小文字なら
 *   大文字にした候補も引く。
 * - 転送ページ（リダイレクト）は転送先のページIDに置き換える。転送ページは
 *   転送先への出リンクを1本持つだけなので、そのまま探索すると距離が1多く出る。
*/

#include <sqlite3.h>

#include <algorithm>
#include <string>
#include <unordered_map>
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

/**
 * @brief DB に転送ページの対応表（redirects）があるかを返します。
*/
inline bool HasRedirectsTable(sqlite3 *db) {
  if (!db) {
    return false;
  }
  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db,
                         "SELECT 1 FROM sqlite_master WHERE type='table' AND "
                         "name='redirects'",
                         -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }
  const bool found = sqlite3_step(stmt) == SQLITE_ROW;
  sqlite3_finalize(stmt);
  return found;
}

/**
 * @brief タイトルを完全一致で引き、転送ページは転送先IDにして返します。
 * @param titles 引くタイトル（DB の表記そのもの）。SQLite の変数上限を
 *               超えない件数で呼んでください。
 * @param useRedirects redirects テーブルで転送先に置き換えるか
 * @param ok 失敗したら false を書き込む（nullptr 可）
 * @return 見つかったタイトル → ページID
*/
inline std::unordered_map<std::string, int>
LookupResolvedIds(sqlite3 *db, const std::vector<std::string> &titles,
                  bool useRedirects, bool *ok = nullptr) {
  std::unordered_map<std::string, int> found;
  if (ok) {
    *ok = true;
  }
  if (!db || titles.empty()) {
    return found;
  }

  std::string sql = useRedirects
                        ? "SELECT p.title, COALESCE(r.target_id, p.id) FROM "
                          "pages p LEFT JOIN redirects r ON r.source_id = p.id "
                          "WHERE p.title IN ("
                        : "SELECT p.title, p.id FROM pages p WHERE p.title IN (";
  for (size_t i = 0; i < titles.size(); ++i) {
    if (i > 0) {
      sql += ",";
    }
    sql += "?";
  }
  sql += ")";

  sqlite3_stmt *stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    if (ok) {
      *ok = false;
    }
    return found;
  }
  for (size_t i = 0; i < titles.size(); ++i) {
    sqlite3_bind_text(stmt, static_cast<int>(i + 1), titles[i].c_str(), -1,
                      SQLITE_STATIC);
  }
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const auto *title =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    if (title) {
      found[title] = sqlite3_column_int(stmt, 1);
    }
  }
  sqlite3_finalize(stmt);
  return found;
}

/**
 * @brief 正規化済みタイトル列をページIDに解決します。
 * @param normalizedTitles 空白をアンダースコアに置き換えたタイトル列
 * @param chunkSize 1回の SQL で扱う元タイトル数
 * @param onChunkDone チャンク完了ごとの通知（処理済み件数, 全件数）
 * @param shouldStop true を返すと残りを打ち切る
 * @return 元タイトル → ページID（見つからないものは含めない）
*/
template <typename OnChunkDone, typename ShouldStop>
std::unordered_map<std::string, int>
ResolveTitles(sqlite3 *db, const std::vector<std::string> &normalizedTitles,
              bool useRedirects, size_t chunkSize, OnChunkDone &&onChunkDone,
              ShouldStop &&shouldStop, bool *ok = nullptr) {
  std::unordered_map<std::string, int> result;
  if (ok) {
    *ok = true;
  }
  if (chunkSize == 0) {
    chunkSize = 1;
  }
  size_t index = 0;
  while (index < normalizedTitles.size()) {
    if (shouldStop()) {
      break;
    }
    const size_t count = std::min(chunkSize, normalizedTitles.size() - index);

    std::vector<std::vector<std::string>> candidatesPerTitle;
    candidatesPerTitle.reserve(count);
    std::vector<std::string> queryTitles;
    queryTitles.reserve(count * 2);
    for (size_t i = 0; i < count; ++i) {
      candidatesPerTitle.push_back(TitleCandidates(normalizedTitles[index + i]));
      queryTitles.insert(queryTitles.end(), candidatesPerTitle.back().begin(),
                         candidatesPerTitle.back().end());
    }

    bool chunkOk = true;
    const auto found = LookupResolvedIds(db, queryTitles, useRedirects, &chunkOk);
    if (!chunkOk) {
      if (ok) {
        *ok = false;
      }
      return result;
    }
    for (size_t i = 0; i < count; ++i) {
      for (const auto &candidate : candidatesPerTitle[i]) {
        if (const auto it = found.find(candidate); it != found.end()) {
          result[normalizedTitles[index + i]] = it->second;
          break;
        }
      }
    }

    index += count;
    onChunkDone(index, normalizedTitles.size());
  }
  return result;
}

} // namespace game::systems::wiki_title_lookup
