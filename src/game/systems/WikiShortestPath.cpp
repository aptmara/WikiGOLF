/**
 * @file WikiShortestPath.cpp
 * @brief 日本語Wikipedia最短経路計算実装
*/
#include "WikiShortestPathInternals.h"
#include "WikiShortestPath.h"
#include "WikiTitleLookup.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <mutex>
#include <queue>
#include <sqlite3.h>
#include <sstream>

namespace game::systems {

using namespace wiki_shortest_path_detail;

WikiShortestPath::~WikiShortestPath() {
  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  if (m_db) {
    UnregisterActiveDb(m_db);
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}

void WikiShortestPath::RequestCancelAll() {
  g_cancelRequested.store(true, std::memory_order_relaxed);
  std::lock_guard<std::mutex> lock(g_activeDbMutex);
  for (sqlite3 *db : g_activeDbs) {
    // 別スレッドで sqlite3_step() 実行中でも安全に呼べる。
    // ブロック中のクエリは SQLITE_INTERRUPT を返して即座に打ち切られる。
    sqlite3_interrupt(db);
  }
}

bool WikiShortestPath::IsCancelRequested() {
  return g_cancelRequested.load(std::memory_order_relaxed);
}

void WikiShortestPath::ClearProcessCaches() {
  {
    auto &cache = GetBackwardBfsCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.targetId = -1;
    cache.depthByPageId.clear();
    cache.frontier.clear();
  }
  {
    auto &cache = GetResolvedDistanceCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.targetId = -1;
    cache.distanceByPageId.clear();
  }
}

bool WikiShortestPath::Initialize(const std::string &dbPath,
                                  bool cachePopularPages) {
  if (IsCancelRequested()) {
    // ウィンドウが閉じられた後：新規に重いDBセッションを開かず即座に諦める。
    LOG_WARN("WikiShortestPath", "Initialize skipped: cancel already requested");
    return false;
  }

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  if (m_db) {
    UnregisterActiveDb(m_db);
    sqlite3_close(m_db);
    m_db = nullptr;
  }
  m_popularPageIds.clear();

  std::string targetPath = dbPath;
  int rc = sqlite3_open_v2(targetPath.c_str(), &m_db, SQLITE_OPEN_READONLY, nullptr);

  bool isValid = false;
  if (rc == SQLITE_OK && m_db) {
    sqlite3_stmt *checkStmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT name FROM sqlite_master WHERE type='table' AND name='pages';", -1, &checkStmt, nullptr) == SQLITE_OK) {
      if (sqlite3_step(checkStmt) == SQLITE_ROW) {
        isValid = true;
      }
      sqlite3_finalize(checkStmt);
    }
  }

  if (!isValid) {
    if (m_db) {
      sqlite3_close(m_db);
      m_db = nullptr;
    }
    size_t pos = targetPath.find("jawiki_sdow.sqlite");
    if (pos != std::string::npos) {
      targetPath.replace(pos, 18, "jawiki_sdow-001.sqlite");
      LOG_INFO("WikiShortestPath", "Attempting fallback database: {}", targetPath);
      rc = sqlite3_open_v2(targetPath.c_str(), &m_db, SQLITE_OPEN_READONLY, nullptr);
      if (rc == SQLITE_OK && m_db) {
        sqlite3_stmt *checkStmt = nullptr;
        if (sqlite3_prepare_v2(m_db, "SELECT name FROM sqlite_master WHERE type='table' AND name='pages';", -1, &checkStmt, nullptr) == SQLITE_OK) {
          if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            isValid = true;
          }
          sqlite3_finalize(checkStmt);
        }
      }
    }
  }

  if (!isValid || rc != SQLITE_OK || !m_db) {
    LOG_ERROR("WikiShortestPath", "DB open failed or invalid schema for path: {}", targetPath);
    if (m_db) {
      sqlite3_close(m_db);
      m_db = nullptr;
    }
    return false;
  }

  m_hasRedirects = wiki_title_lookup::HasRedirectsTable(m_db);
  LOG_INFO("WikiShortestPath", "Database initialized: {} (redirects={})",
           targetPath, m_hasRedirects);
  RegisterActiveDb(m_db);

  if (!cachePopularPages) {
    return true;
  }

  // 人気記事のキャッシュを作成（初回）
  LOG_INFO("WikiShortestPath", "Caching popular pages...");
  const char *cacheSql =
      "SELECT p.id FROM pages p "
      "INNER JOIN links l ON p.id = l.id "
      "WHERE l.incoming_links_count >= 10000 AND p.is_redirect = 0 "
      "ORDER BY l.incoming_links_count DESC LIMIT 2000";

  LOG_DEBUG("WikiShortestPath", "Preparing SQL statement...");
  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(m_db, cacheSql, -1, &stmt, nullptr) == SQLITE_OK) {
    LOG_DEBUG("WikiShortestPath", "Executing query...");
    int rowCount = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      m_popularPageIds.push_back(sqlite3_column_int(stmt, 0));
      rowCount++;
      if (rowCount % 500 == 0) {
        LOG_DEBUG("WikiShortestPath", "Cached {} pages so far...", rowCount);
      }
    }
    LOG_DEBUG("WikiShortestPath", "Finalizing statement...");
    sqlite3_finalize(stmt);
  } else {
    LOG_ERROR("WikiShortestPath", "Failed to prepare cache SQL: {}",
              sqlite3_errmsg(m_db));
  }
  LOG_INFO("WikiShortestPath", "Cached {} popular pages.",
           m_popularPageIds.size());

  // 乱数シード初期化（ターゲット選択用）
  srand(static_cast<unsigned int>(time(nullptr)));

  return true;
}

int WikiShortestPath::ResolvePageId(const std::string &title) {
  return FetchPageId(title);
}

int WikiShortestPath::FetchPageId(const std::string &title) {
  if (!m_db)
    return -1;

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  // スペースをアンダースコアに変換
  std::string normalized = title;
  std::replace(normalized.begin(), normalized.end(), ' ', '_');

  const auto candidates = wiki_title_lookup::TitleCandidates(normalized);
  const auto found =
      wiki_title_lookup::LookupResolvedIds(m_db, candidates, m_hasRedirects);
  for (const auto &candidate : candidates) {
    if (const auto it = found.find(candidate); it != found.end()) {
      return it->second;
    }
  }
  return -1;
}

std::unordered_map<std::string, int>
WikiShortestPath::FetchPageIdsBatch(
    const std::vector<std::string>& normalizedTitles,
    const std::function<void(size_t processed, size_t total)>& onChunkDone,
    const std::atomic<bool>* cancelRequested) {
  std::unordered_map<std::string, int> result;
  if (!m_db || normalizedTitles.empty()) {
    return result;
  }

  const auto batchStartedAt = std::chrono::steady_clock::now();

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);

  size_t processed = 0;
  bool ok = true;
  // IN句で複数タイトルを一括検索します（1タイトルにつき候補は最大2つ）。
  result = wiki_title_lookup::ResolveTitles(
      m_db, normalizedTitles, m_hasRedirects, kLinkChunkSize,
      [&](size_t done, size_t total) {
        processed = done;
        // 呼び出し元の ComputeDistancesToTarget が storeProgress を更新します。
        if (onChunkDone) {
          onChunkDone(done, total);
        }
      },
      [&]() {
        return WikiShortestPath::IsCancelRequested() ||
               (cancelRequested &&
                cancelRequested->load(std::memory_order_relaxed));
      },
      &ok);

  if (!ok) {
    LOG_ERROR("WikiShortestPath", "FetchPageIdsBatch query failed: {}",
              sqlite3_errmsg(m_db));
  } else if (processed < normalizedTitles.size()) {
    LOG_WARN("WikiShortestPath", "FetchPageIdsBatch cancelled at {}/{}",
             processed, normalizedTitles.size());
  }
  LOG_INFO("WikiShortestPath",
           "FetchPageIdsBatch: requested={} found={} elapsed={}ms",
           normalizedTitles.size(), result.size(),
           ElapsedMs(batchStartedAt));
  return result;
}

std::string WikiShortestPath::FetchPageTitle(int pageId) {
  if (!m_db)
    return "";

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  const char *sql = "SELECT title FROM pages WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return "";
  }

  sqlite3_bind_int(stmt, 1, pageId);

  std::string title;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text = (const char *)sqlite3_column_text(stmt, 0);
    if (text) {
      title = text;
      // アンダースコアをスペースに変換
      std::replace(title.begin(), title.end(), '_', ' ');
    }
  }

  sqlite3_finalize(stmt);
  return title;
}

std::string WikiShortestPath::FetchOutgoingLinks(int pageId) {
  if (!m_db)
    return "";

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  const char *sql = "SELECT outgoing_links FROM links WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return "";
  }

  sqlite3_bind_int(stmt, 1, pageId);

  std::string links;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text = (const char *)sqlite3_column_text(stmt, 0);
    if (text)
      links = text;
  }

  sqlite3_finalize(stmt);
  return links;
}

std::string WikiShortestPath::FetchIncomingLinks(int pageId) {
  if (!m_db)
    return "";

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  const char *sql = "SELECT incoming_links FROM links WHERE id = ?";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return "";
  }

  sqlite3_bind_int(stmt, 1, pageId);

  std::string links;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *text = (const char *)sqlite3_column_text(stmt, 0);
    if (text)
      links = text;
  }

  sqlite3_finalize(stmt);
  return links;
}

int WikiShortestPath::FetchOutgoingLinksCount(const std::vector<int> &pageIds) {
  if (!m_db || pageIds.empty())
    return 0;

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  std::string sql = "SELECT SUM(outgoing_links_count) FROM links WHERE id IN (";
  for (size_t i = 0; i < pageIds.size(); ++i) {
    if (i > 0)
      sql += ",";
    sql += std::to_string(pageIds[i]);
  }
  sql += ")";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  sqlite3_finalize(stmt);
  return count;
}

int WikiShortestPath::FetchIncomingLinksCount(const std::vector<int> &pageIds) {
  if (!m_db || pageIds.empty())
    return 0;

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  std::string sql = "SELECT SUM(incoming_links_count) FROM links WHERE id IN (";
  for (size_t i = 0; i < pageIds.size(); ++i) {
    if (i > 0)
      sql += ",";
    sql += std::to_string(pageIds[i]);
  }
  sql += ")";

  sqlite3_stmt *stmt;
  if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }

  sqlite3_finalize(stmt);
  return count;
}

std::vector<std::vector<int>> WikiShortestPath::ReconstructPaths(
    const std::vector<int> &pageIds,
    const std::unordered_map<int, std::vector<int>> &visitedDict) {

  std::vector<std::vector<int>> paths;

  for (int pageId : pageIds) {
    if (pageId == -1) {
      // ソース/ターゲット到達
      paths.push_back({});
    } else {
      auto it = visitedDict.find(pageId);
      if (it != visitedDict.end()) {
        auto childPaths = ReconstructPaths(it->second, visitedDict);
        for (auto &childPath : childPaths) {
          childPath.push_back(pageId);
          paths.push_back(std::move(childPath));
        }
      }
    }
  }

  return paths;
}

} // namespace game::systems
