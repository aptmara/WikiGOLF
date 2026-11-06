/**
 * @file test_wiki_title_lookup.cpp
 * @brief 記事タイトルからページIDを引く処理（括弧・大文字小文字・転送）を検証します。
 */

#include "src/game/systems/WikiTitleLookup.h"

#include <cassert>
#include <string>
#include <vector>

namespace lookup = game::systems::wiki_title_lookup;

namespace {

sqlite3 *OpenFixture(bool withRedirects) {
  sqlite3 *db = nullptr;
  assert(sqlite3_open(":memory:", &db) == SQLITE_OK);
  std::string sql =
      "CREATE TABLE pages (id INTEGER PRIMARY KEY, title TEXT NOT NULL, "
      "is_redirect INTEGER NOT NULL DEFAULT 0);"
      "CREATE INDEX idx_pages_title ON pages(title);"
      "INSERT INTO pages VALUES (1, 'EU_(欧州連合)', 0);"
      "INSERT INTO pages VALUES (2, 'Tokyo', 1);"
      "INSERT INTO pages VALUES (3, 'TOKYO', 0);"
      "INSERT INTO pages VALUES (4, '東京都', 0);"
      "INSERT INTO pages VALUES (5, 'Apple', 0);"
      "INSERT INTO pages VALUES (6, '転送のみ', 1);";
  if (withRedirects) {
    sql += "CREATE TABLE redirects (source_id INTEGER PRIMARY KEY, "
           "target_id INTEGER NOT NULL);"
           "INSERT INTO redirects VALUES (2, 4);";
  }
  assert(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr) ==
         SQLITE_OK);
  return db;
}

void VerifyCandidates() {
  assert(lookup::TitleCandidates("").empty());
  assert((lookup::TitleCandidates("東京都") == std::vector<std::string>{"東京都"}));
  assert((lookup::TitleCandidates("apple") ==
          std::vector<std::string>{"apple", "Apple"}));
  assert((lookup::TitleCandidates("Apple") == std::vector<std::string>{"Apple"}));
}

void VerifyLookup() {
  sqlite3 *db = OpenFixture(true);
  assert(lookup::HasRedirectsTable(db));

  const std::vector<std::string> titles = {
      "EU_(欧州連合)", // 末尾の ')' を含めたまま一致する
      "EU_(欧州連合",  // ')' を落とした表記は別物
      "TOKYO",         // 大文字小文字が違う別記事を拾わない
      "Tokyo",         // 転送ページは転送先へ
      "apple",         // 先頭1文字だけは大文字小文字を無視する
      "aPPLE",         // 2文字目以降は区別する
      "転送のみ",      // 対応表が無い転送ページは自分自身
      "無い記事"};
  bool ok = false;
  size_t chunks = 0;
  const auto ids = lookup::ResolveTitles(
      db, titles, true, 3, [&](size_t, size_t) { ++chunks; },
      [] { return false; }, &ok);
  assert(ok);
  assert(chunks == 3);
  assert(ids.at("EU_(欧州連合)") == 1);
  assert(ids.count("EU_(欧州連合") == 0);
  assert(ids.at("TOKYO") == 3);
  assert(ids.at("Tokyo") == 4);
  assert(ids.at("apple") == 5);
  assert(ids.count("aPPLE") == 0);
  assert(ids.at("転送のみ") == 6);
  assert(ids.count("無い記事") == 0);
  assert(ids.size() == 5);
  sqlite3_close(db);
}

void VerifyWithoutRedirectsTable() {
  sqlite3 *db = OpenFixture(false);
  assert(!lookup::HasRedirectsTable(db));
  const auto ids = lookup::LookupResolvedIds(db, {"Tokyo"}, false);
  assert(ids.at("Tokyo") == 2);
  sqlite3_close(db);
}

void VerifyStop() {
  sqlite3 *db = OpenFixture(true);
  const auto ids = lookup::ResolveTitles(
      db, {"Apple", "TOKYO"}, true, 1, [](size_t, size_t) {},
      [] { return true; });
  assert(ids.empty());
  sqlite3_close(db);
}

} // namespace

int main() {
  VerifyCandidates();
  VerifyLookup();
  VerifyWithoutRedirectsTable();
  VerifyStop();
  return 0;
}
