/**
 * @file WikiShortestPath.cpp
 * @brief 日本語Wikipedia最短経路計算実装
 */

#include "WikiShortestPath.h"
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

namespace {

// IN句のチャンクサイズ。ベンチでは 512〜1024 で差がほぼ無かったため安全側の512に固定。
constexpr size_t kLinkChunkSize = 512;
constexpr size_t kDetailedChunkLogLimit = 3;
constexpr long long kSlowLinkFetchChunkMs = 250;
constexpr size_t kPathEvaluationDepthProgressUnits = 100;

std::recursive_mutex g_sqliteMutex;

struct NodeInfo {
  int parent = -1;
  int depth = 0;
};

struct LinkFetchStats {
  size_t requestedPages = 0;
  size_t returnedRows = 0;
  size_t parsedLinks = 0;
  size_t rawBytes = 0;
  size_t chunks = 0;
};

struct LinkFetchChunkStats {
  size_t chunkIndex = 0;
  size_t totalChunks = 0;
  size_t requestedPages = 0;
  size_t returnedRows = 0;
  size_t parsedLinks = 0;
  size_t rawBytes = 0;
  long long prepareMs = 0;
  long long stepParseMs = 0;
  long long elapsedMs = 0;
};

/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。 山内陽
 */
long long ElapsedMs(const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

std::vector<int> ParseLinks(const unsigned char *text) {
  std::vector<int> links;
  if (!text)
    return links;

  const char *ptr = reinterpret_cast<const char *>(text);
  const char *start = ptr;

  while (*ptr) {
    if (*ptr == '|') {
      if (ptr > start) {
        links.push_back(static_cast<int>(std::strtol(start, nullptr, 10)));
      }
      start = ptr + 1;
    }
    ++ptr;
  }

  if (ptr > start) {
    links.push_back(static_cast<int>(std::strtol(start, nullptr, 10)));
  }

  return links;
}

bool FetchLinks(sqlite3 *db, const std::vector<int> &pageIds,
                const char *fieldName,
                std::unordered_map<int, std::vector<int>> &outLinks,
                const std::function<void(size_t, size_t)> &onChunkDone =
                    nullptr) {
  if (pageIds.empty())
    return true;

  const auto fetchStartedAt = std::chrono::steady_clock::now();
  LinkFetchStats stats;
  stats.requestedPages = pageIds.size();
  std::vector<LinkFetchChunkStats> chunkLogs;
  const size_t totalChunks = (pageIds.size() + kLinkChunkSize - 1) / kLinkChunkSize;

  {
    std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
    size_t index = 0;
    while (index < pageIds.size()) {
      const auto chunkStartedAt = std::chrono::steady_clock::now();
      size_t count = std::min(kLinkChunkSize, pageIds.size() - index);

      std::string sql = "SELECT id, ";
      sql += fieldName;
      sql += " FROM links WHERE id IN (";
      for (size_t i = 0; i < count; ++i) {
        if (i > 0)
          sql += ",";
        sql += "?";
      }
      sql += ")";

      sqlite3_stmt *stmt = nullptr;
      const auto prepareStartedAt = std::chrono::steady_clock::now();
      if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) !=
          SQLITE_OK) {
        LOG_ERROR("WikiShortestPath", "Failed to prepare link fetch SQL: {}",
                  sqlite3_errmsg(db));
        return false;
      }
      const long long prepareMs = ElapsedMs(prepareStartedAt);

      for (size_t i = 0; i < count; ++i) {
        sqlite3_bind_int(
            stmt, static_cast<int>(i + 1),
            pageIds[index + i]); // プレースホルダーのインデックスは1から開始
      }

      size_t chunkRows = 0;
      size_t chunkLinks = 0;
      size_t chunkBytes = 0;
      const auto stepStartedAt = std::chrono::steady_clock::now();
      while (sqlite3_step(stmt) == SQLITE_ROW) {
        int pageId = sqlite3_column_int(stmt, 0);
        const unsigned char *raw = sqlite3_column_text(stmt, 1);
        const size_t rawBytes =
            raw ? std::strlen(reinterpret_cast<const char *>(raw)) : 0;
        auto links = ParseLinks(raw);
        chunkBytes += rawBytes;
        chunkLinks += links.size();
        outLinks[pageId] = std::move(links);
        ++chunkRows;
      }
      const long long stepMs = ElapsedMs(stepStartedAt);

      sqlite3_finalize(stmt);
      ++stats.chunks;
      stats.returnedRows += chunkRows;
      stats.parsedLinks += chunkLinks;
      stats.rawBytes += chunkBytes;
      if (onChunkDone) {
        onChunkDone(stats.chunks, totalChunks);
      }

      const long long chunkMs = ElapsedMs(chunkStartedAt);
      if (stats.chunks <= kDetailedChunkLogLimit ||
          chunkMs >= kSlowLinkFetchChunkMs) {
        chunkLogs.push_back(LinkFetchChunkStats{
            stats.chunks,
            totalChunks,
            count,
            chunkRows,
            chunkLinks,
            chunkBytes,
            prepareMs,
            stepMs,
            chunkMs});
      }

      index += count;
    }
  }

  for (const auto &chunk : chunkLogs) {
    LOG_DEBUG("WikiShortestPath",
             "FetchLinks chunk field={} chunk={}/{} requested={} rows={} "
             "links={} bytes={} prepare={}ms stepParse={}ms elapsed={}ms",
             fieldName, chunk.chunkIndex, chunk.totalChunks,
             chunk.requestedPages, chunk.returnedRows, chunk.parsedLinks,
             chunk.rawBytes, chunk.prepareMs, chunk.stepParseMs,
             chunk.elapsedMs);
  }

  LOG_DEBUG("WikiShortestPath",
           "FetchLinks summary field={} requested={} rows={} links={} bytes={} "
           "chunks={} elapsed={}ms",
           fieldName, stats.requestedPages, stats.returnedRows,
           stats.parsedLinks, stats.rawBytes, stats.chunks,
           ElapsedMs(fetchStartedAt));

  return true;
}

std::vector<int> BuildPath(int meet,
                           const std::unordered_map<int, NodeInfo> &forwardInfo,
                           const std::unordered_map<int, NodeInfo> &backwardInfo) {
  std::vector<int> left;
  int cur = meet;
  auto it = forwardInfo.find(cur);
  while (it != forwardInfo.end()) {
    left.push_back(cur);
    cur = it->second.parent;
    it = forwardInfo.find(cur);
  }
  std::reverse(left.begin(), left.end());

  std::vector<int> right;
  cur = backwardInfo.at(meet).parent;
  auto itBack = backwardInfo.find(cur);
  while (itBack != backwardInfo.end()) {
    right.push_back(cur);
    cur = itBack->second.parent;
    itBack = backwardInfo.find(cur);
  }

  left.insert(left.end(), right.begin(), right.end());
  return left;
}

} // namespace

WikiShortestPath::~WikiShortestPath() {
  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}

bool WikiShortestPath::Initialize(const std::string &dbPath,
                                  bool cachePopularPages) {
  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);
  if (m_db) {
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

  LOG_INFO("WikiShortestPath", "Database initialized: {}", targetPath);

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

  const char *sql = "SELECT id FROM pages WHERE title = ? COLLATE NOCASE";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_STATIC);

  int pageId = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    pageId = sqlite3_column_int(stmt, 0);
  }

  sqlite3_finalize(stmt);
  return pageId;
}

std::unordered_map<std::string, int>
WikiShortestPath::FetchPageIdsBatch(
    const std::vector<std::string>& normalizedTitles,
    const std::function<void(size_t processed, size_t total)>& onChunkDone) {
  std::unordered_map<std::string, int> result;
  if (!m_db || normalizedTitles.empty()) {
    return result;
  }

  result.reserve(normalizedTitles.size());
  const auto batchStartedAt = std::chrono::steady_clock::now();

  std::lock_guard<std::recursive_mutex> lock(g_sqliteMutex);

  size_t index = 0;
  size_t totalHits = 0;
  while (index < normalizedTitles.size()) {
    const size_t count =
        std::min(kLinkChunkSize, normalizedTitles.size() - index);

    // IN句で複数タイトルを一括検索します。
    // COLLATE NOCASE は FetchPageId の既存挙動と合わせています。
    std::string sql =
        "SELECT id, title FROM pages WHERE title COLLATE NOCASE IN (";
    for (size_t i = 0; i < count; ++i) {
      if (i > 0) sql += ",";
      sql += "?";
    }
    sql += ")";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr) !=
        SQLITE_OK) {
      LOG_ERROR("WikiShortestPath",
                "FetchPageIdsBatch prepare failed chunk={}/{}: {}",
                index / kLinkChunkSize + 1,
                (normalizedTitles.size() + kLinkChunkSize - 1) / kLinkChunkSize,
                sqlite3_errmsg(m_db));
      return result;
    }

    // バインドするタイトル文字列は sqlite3_finalize まで生存します。
    for (size_t i = 0; i < count; ++i) {
      sqlite3_bind_text(
          stmt, static_cast<int>(i + 1),
          normalizedTitles[index + i].c_str(), -1, SQLITE_STATIC);
    }

    size_t chunkHits = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const int pageId = sqlite3_column_int(stmt, 0);
      const char* storedTitle =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      if (storedTitle) {
        result[storedTitle] = pageId;
        ++chunkHits;
      }
    }
    sqlite3_finalize(stmt);
    totalHits += chunkHits;
    index += count;

    // チャンク完了ごとに進捗を報告します。
    // 呼び出し元の ComputeDistancesToTarget が storeProgress を更新します。
    if (onChunkDone) {
      onChunkDone(index, normalizedTitles.size());
    }
  }

  LOG_INFO("WikiShortestPath",
           "FetchPageIdsBatch: requested={} found={} elapsed={}ms",
           normalizedTitles.size(), totalHits,
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

ShortestPathResult
WikiShortestPath::FindShortestPath(const std::string &sourceTitle,
                                   const std::string &targetTitle,
                                   int maxDepth, bool logSuccess) {
  int targetId = FetchPageId(targetTitle);
  return FindShortestPath(sourceTitle, targetId, maxDepth, logSuccess);
}

ShortestPathResult
WikiShortestPath::FindShortestPath(const std::string &sourceTitle, int targetId,
                                   int maxDepth, bool logSuccess) {
  ShortestPathResult result;

  if (!m_db) {
    result.errorMessage = "データベース未初期化";
    return result;
  }

  // ページID取得
  int sourceId = FetchPageId(sourceTitle);
  if (targetId < 0) {
    result.errorMessage = "ターゲットIDが無効です";
    return result;
  }
  if (sourceId < 0) {
    result.errorMessage = "開始記事が見つかりません";
    return result;
  }

  // 同一ページ
  if (sourceId == targetId) {
    result.success = true;
    result.degrees = 0;
    result.path = {sourceTitle};
    return result;
  }

  // 双方向BFS（長さ優先。タイトル変換は1本のみ）
  std::vector<int> frontierForward = {sourceId};
  std::vector<int> frontierBackward = {targetId};
  std::unordered_map<int, NodeInfo> forwardInfo;
  std::unordered_map<int, NodeInfo> backwardInfo;
  forwardInfo[sourceId] = {-1, 0};
  backwardInfo[targetId] = {-1, 0};

  bool found = false;
  std::vector<int> pathIds;

  for (int iter = 0; iter < maxDepth; ++iter) {
    if (frontierForward.empty() || frontierBackward.empty())
      break;

    const bool expandForward = frontierForward.size() <= frontierBackward.size();

    if (expandForward) {
      std::unordered_map<int, std::vector<int>> linkMap;
      if (!FetchLinks(m_db, frontierForward, "outgoing_links", linkMap)) {
        result.errorMessage = "リンク取得に失敗しました";
        return result;
      }

      std::vector<int> next;
      for (int pageId : frontierForward) {
        int nextDepth = forwardInfo[pageId].depth + 1;
        for (int nb : linkMap[pageId]) {
          if (forwardInfo.find(nb) == forwardInfo.end()) {
            forwardInfo[nb] = {pageId, nextDepth};
            next.push_back(nb);
          }
          if (backwardInfo.find(nb) != backwardInfo.end()) {
            pathIds = BuildPath(nb, forwardInfo, backwardInfo);
            found = true;
            break;
          }
        }
        if (found)
          break;
      }
      frontierForward = std::move(next);
    } else {
      std::unordered_map<int, std::vector<int>> linkMap;
      if (!FetchLinks(m_db, frontierBackward, "incoming_links", linkMap)) {
        result.errorMessage = "リンク取得に失敗しました";
        return result;
      }

      std::vector<int> next;
      for (int pageId : frontierBackward) {
        int nextDepth = backwardInfo[pageId].depth + 1;
        for (int nb : linkMap[pageId]) {
          if (backwardInfo.find(nb) == backwardInfo.end()) {
            backwardInfo[nb] = {pageId, nextDepth};
            next.push_back(nb);
          }
          if (forwardInfo.find(nb) != forwardInfo.end()) {
            pathIds = BuildPath(nb, forwardInfo, backwardInfo);
            found = true;
            break;
          }
        }
        if (found)
          break;
      }
      frontierBackward = std::move(next);
    }

    if (found)
      break;
  }

  if (!found) {
    result.errorMessage = "経路が見つかりません";
    return result;
  }

  result.success = true;
  result.degrees = static_cast<int>(pathIds.size()) - 1;

  for (int pageId : pathIds) {
    result.path.push_back(FetchPageTitle(pageId));
  }

  std::string pathStr = "";
  for (size_t i = 0; i < result.path.size(); ++i) {
    if (i > 0)
      pathStr += " -> ";
    pathStr += result.path[i];
  }
  if (logSuccess) {
    LOG_INFO("WikiShortestPath", "{} -> ID:{} ({} hops): {}", sourceTitle,
             targetId, result.degrees, pathStr);
  }

  return result;
}

std::unordered_map<std::string, int> WikiShortestPath::ComputeDistancesToTarget(
    const std::vector<std::string> &sourceTitles, int targetId, int maxDepth,
    std::atomic<size_t> *progressUnits, size_t progressBase,
    const std::function<void(const std::string&, int)>& onResolved) {
  const auto computeStartedAt = std::chrono::steady_clock::now();
  std::unordered_map<std::string, int> distances;
  if (!m_db || targetId < 0 || sourceTitles.empty() || maxDepth < 0) {
    LOG_WARN("WikiShortestPath",
             "ComputeDistancesToTarget skipped: db={}, targetId={}, sources={}, maxDepth={}",
             m_db ? "ready" : "null", targetId, sourceTitles.size(), maxDepth);
    return distances;
  }

  LOG_INFO("WikiShortestPath",
           "ComputeDistancesToTarget started: sources={}, targetId={}, maxDepth={}",
           sourceTitles.size(), targetId, maxDepth);

  const auto storeProgress = [&](size_t units) {
    if (progressUnits) {
      progressUnits->store(progressBase + units, std::memory_order_relaxed);
    }
  };

  std::unordered_map<int, std::vector<std::string>> titlesByPageId;
  std::unordered_set<int> unresolvedPageIds;
  size_t missingPageIds = 0;
  const auto resolveStartedAt = std::chrono::steady_clock::now();

  // --- バッチタイトル解決 ---
  // 1件ずつ FetchPageId を呼ぶ代わりに IN句バッチクエリで一括解決します。
  // FetchLinks と同じ kLinkChunkSize（512件）のチャンク方式を採用します。

  // スペース→アンダースコア正規化と重複排除を行います。
  // normalizedToOriginals: 正規化タイトル → 元タイトル列
  std::unordered_map<std::string, std::vector<std::string>> normalizedToOriginals;
  std::vector<std::string> uniqueNormalized;
  uniqueNormalized.reserve(sourceTitles.size());

  for (const auto& title : sourceTitles) {
    if (title.empty() || distances.find(title) != distances.end()) {
      continue; // 解決済み・空はスキップ
    }
    std::string normalized = title;
    std::replace(normalized.begin(), normalized.end(), ' ', '_');
    if (normalizedToOriginals.find(normalized) == normalizedToOriginals.end()) {
      uniqueNormalized.push_back(normalized);
    }
    normalizedToOriginals[normalized].push_back(title);
  }

  // バッチSQLでページIDを一括取得します。
  // チャンク完了コールバック内で storeProgress を呼び、
  // 1チャンク処理ごとに進捗バーが連続的に動くようにします。
  const size_t srcSize = sourceTitles.size();
  const auto batchIdMap = FetchPageIdsBatch(
      uniqueNormalized,
      [&](size_t processed, size_t total) {
        // バッチ内の処理済み割合を sourceTitles.size() 分にスケールして報告します。
        const size_t units =
            total > 0 ? (processed * srcSize + total - 1) / total : srcSize;
        storeProgress(units);
      });

  // バッチ結果を titlesByPageId / distances / unresolvedPageIds に展開します。
  for (const auto& [normalizedTitle, pageId] : batchIdMap) {
    const auto origIt = normalizedToOriginals.find(normalizedTitle);
    if (origIt == normalizedToOriginals.end()) {
      continue; // DB側タイトルとクエリタイトルが異なるケース（通常は発生しない）
    }
    for (const auto& origTitle : origIt->second) {
      titlesByPageId[pageId].push_back(origTitle);
      if (pageId == targetId) {
        distances[origTitle] = 0;
        if (onResolved) {
          onResolved(origTitle, 0);
        }
      } else {
        unresolvedPageIds.insert(pageId);
      }
    }
    normalizedToOriginals.erase(origIt); // 解決済みを除去してmissing集計に使います
  }

  // batchIdMap に含まれなかったタイトルは DB 未登録です。
  for (const auto& [_, originals] : normalizedToOriginals) {
    missingPageIds += originals.size();
  }

  // ビッグバンプ起笪にコールバックで progress が未更新のままの場合は、
  // バッチ完了後に srcSize まで確実に更新します。
  storeProgress(srcSize);

  LOG_INFO("WikiShortestPath",
           "ComputeDistancesToTarget title resolution: sources={} uniquePageIds={} "
           "directHits={} unresolved={} missing={} elapsed={}ms",
           sourceTitles.size(), titlesByPageId.size(), distances.size(),
           unresolvedPageIds.size(), missingPageIds, ElapsedMs(resolveStartedAt));

  if (unresolvedPageIds.empty()) {
    storeProgress(sourceTitles.size() +
                  static_cast<size_t>(maxDepth) *
                      kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget completed without BFS: resolved={} elapsed={}ms",
             distances.size(), ElapsedMs(computeStartedAt));
    return distances;
  }

  std::vector<int> frontier = {targetId};
  std::unordered_set<int> visited;
  visited.insert(targetId);

  for (int depth = 1; depth <= maxDepth && !frontier.empty() &&
                      !unresolvedPageIds.empty();
       ++depth) {
    std::unordered_map<int, std::vector<int>> linkMap;
    const auto depthStartedAt = std::chrono::steady_clock::now();
    const size_t frontierBefore = frontier.size();
    const size_t unresolvedBefore = unresolvedPageIds.size();
    const size_t resolvedBefore = distances.size();
    const auto fetchStartedAt = std::chrono::steady_clock::now();
    const size_t depthBase =
        sourceTitles.size() +
        static_cast<size_t>(depth - 1) * kPathEvaluationDepthProgressUnits;
    if (!FetchLinks(
            m_db, frontier, "incoming_links", linkMap,
            [&](size_t processedChunks, size_t totalChunks) {
              if (totalChunks == 0) {
                return;
              }
              const size_t depthUnits =
                  (processedChunks * kPathEvaluationDepthProgressUnits) /
                  totalChunks;
              storeProgress(depthBase +
                            std::min(depthUnits,
                                     kPathEvaluationDepthProgressUnits));
            })) {
      LOG_ERROR("WikiShortestPath",
                "ComputeDistancesToTarget BFS failed: depth={} frontier={} elapsed={}ms",
                depth, frontierBefore, ElapsedMs(depthStartedAt));
      return distances;
    }
    const long long fetchMs = ElapsedMs(fetchStartedAt);

    std::vector<int> next;
    size_t incomingEdges = 0;
    for (int pageId : frontier) {
      const auto linkIt = linkMap.find(pageId);
      if (linkIt == linkMap.end()) {
        continue;
      }

      incomingEdges += linkIt->second.size();
      for (int incomingId : linkIt->second) {
        if (!visited.insert(incomingId).second) {
          continue;
        }

        if (auto titleIt = titlesByPageId.find(incomingId);
            titleIt != titlesByPageId.end()) {
          for (const auto &title : titleIt->second) {
            distances[title] = depth;
            if (onResolved) {
              onResolved(title, depth);
            }
          }
          unresolvedPageIds.erase(incomingId);
        }

        next.push_back(incomingId);
      }
    }

    frontier = std::move(next);
    storeProgress(sourceTitles.size() + static_cast<size_t>(depth) *
                                            kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget depth={} frontier={} rows={} incomingEdges={} "
             "next={} visited={} resolvedDelta={} resolved={} unresolvedDelta={} "
             "unresolved={} fetch={}ms elapsed={}ms",
             depth, frontierBefore, linkMap.size(), incomingEdges,
             frontier.size(), visited.size(), distances.size() - resolvedBefore,
             distances.size(), unresolvedBefore - unresolvedPageIds.size(),
             unresolvedPageIds.size(), fetchMs, ElapsedMs(depthStartedAt));
  }

  storeProgress(sourceTitles.size() + static_cast<size_t>(maxDepth) *
                                          kPathEvaluationDepthProgressUnits);

  LOG_INFO("WikiShortestPath",
           "Computed target distances: sources={}, resolved={}, targetId={}, "
           "maxDepth={}, elapsed={}ms",
           sourceTitles.size(), distances.size(), targetId, maxDepth,
           ElapsedMs(computeStartedAt));
  return distances;
}

std::pair<std::string, int>
WikiShortestPath::FetchPopularPageTitle(int minIncomingLinks) {
  if (!m_db)
    return {"", -1};

  // キャッシュからランダムに選ぶ
  if (m_popularPageIds.empty()) {
    LOG_WARN("WikiShortestPath", "No popular pages cached.");
    return {"", -1};
  }

  // 10回リトライ（フィルタリング用）
  for (int i = 0; i < 10; ++i) {
    int randIdx = rand() % m_popularPageIds.size();
    int pageId = m_popularPageIds[randIdx];

    std::string title = FetchPageTitle(pageId);

    // フィルタリング
    bool isIgnored = false;
    if (title.empty())
      isIgnored = true;
    if (std::all_of(title.begin(), title.end(),
                    [](unsigned char c) { return std::isdigit(c); }))
      isIgnored = true;
    if (title.size() >= 3) {
      std::string suffix = title.substr(title.size() - 3);
      if (suffix == "年" || suffix == "月" || suffix == "日")
        isIgnored = true;
    }

    if (!isIgnored) {
      LOG_INFO("WikiShortestPath",
               "Selected popular page from cache: {} (ID: {})", title, pageId);
      return {title, pageId};
    }
  }

  LOG_WARN("WikiShortestPath",
           "Failed to select valid popular page from cache after retries.");
  return {"", -1};
}

} // namespace game::systems
