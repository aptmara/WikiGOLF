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

// 実行中の全 WikiShortestPath インスタンスが開いている sqlite3* の一覧。
// RequestCancelAll() から sqlite3_interrupt() を呼ぶために保持する。
std::mutex g_activeDbMutex;
std::vector<sqlite3 *> g_activeDbs;
std::atomic<bool> g_cancelRequested{false};

void RegisterActiveDb(sqlite3 *db) {
  std::lock_guard<std::mutex> lock(g_activeDbMutex);
  g_activeDbs.push_back(db);
}

void UnregisterActiveDb(sqlite3 *db) {
  std::lock_guard<std::mutex> lock(g_activeDbMutex);
  const auto it = std::find(g_activeDbs.begin(), g_activeDbs.end(), db);
  if (it != g_activeDbs.end()) {
    g_activeDbs.erase(it);
  }
}

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
                    nullptr,
                const std::atomic<bool>* cancelRequested = nullptr) {
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
      if (WikiShortestPath::IsCancelRequested() ||
          (cancelRequested &&
           cancelRequested->load(std::memory_order_relaxed))) {
        LOG_WARN("WikiShortestPath",
                 "FetchLinks cancelled: field={} chunk={}/{}",
                 fieldName, stats.chunks + 1, totalChunks);
        break;
      }
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

/**
 * @brief ゴール記事（ターゲット）起点の逆方向BFS結果をプロセス内で使い回すキャッシュです。 山内陽
 * @details 1プレイ中はゲーム開始時に決まったターゲット記事を目指してページ
 *          （＝ホール）を渡り歩くため、ページ移動のたびに ComputeDistancesToTarget
 *          が呼ばれても target->incoming_links の探索結果は不変。前回到達済みの
 *          ノード集合とその境界フロンティアを保持して使い回す。新しいゲームで
 *          別のターゲットを目指すことになったら破棄する。1ターゲット分だけ
 *          保持すれば十分（同時に複数のターゲットを探索することはない）。
 */
struct BackwardBfsCache {
  std::mutex mutex;
  int targetId = -1;
  std::unordered_map<int, int> depthByPageId; // 到達済みノード -> ターゲットまでの距離
  std::vector<int> frontier;                  // 次に展開すべき境界ノード
};

BackwardBfsCache &GetBackwardBfsCache() {
  static BackwardBfsCache cache;
  return cache;
}

/**
 * @brief 特定ページからターゲットまでの「確定済み距離」をプロセス内で
 *        使い回すキャッシュです。 山内陽
 * @details BackwardBfsCache は逆方向BFSの到達済みノード（探索の途中経過）を
 *          保持するだけなので、双方向BFSで forward 側との合流によって解決した
 *          ソース自身の最終距離までは保持できない。同じ記事へのリンクは
 *          コース中の複数ページに何度も登場するため、一度解決した
 *          （リンク先ページID, 距離）は個別に憶えておき、次に同じリンクが
 *          別のページに出てきた時はBFSを一切行わずに即答する。
 *          ターゲットが変わったら破棄する。
 */
struct ResolvedDistanceCache {
  std::mutex mutex;
  int targetId = -1;
  std::unordered_map<int, int> distanceByPageId; // ページID -> ターゲットまでの距離
};

ResolvedDistanceCache &GetResolvedDistanceCache() {
  static ResolvedDistanceCache cache;
  return cache;
}

} // namespace

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

  LOG_INFO("WikiShortestPath", "Database initialized: {}", targetPath);
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
  // DB内のタイトルは末尾の')'が削除されているため、検索時も末尾の')'を取り除く
  if (!normalized.empty() && normalized.back() == ')') {
    normalized.pop_back();
  }

  const char *sql = "SELECT id FROM pages WHERE title = ? COLLATE NOCASE";
  sqlite3_stmt *stmt;

  if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, normalized.c_str(), -1, SQLITE_TRANSIENT);

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
    const std::function<void(size_t processed, size_t total)>& onChunkDone,
    const std::atomic<bool>* cancelRequested) {
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
    if (WikiShortestPath::IsCancelRequested() ||
        (cancelRequested &&
         cancelRequested->load(std::memory_order_relaxed))) {
      LOG_WARN("WikiShortestPath",
               "FetchPageIdsBatch cancelled at {}/{}", index,
               normalizedTitles.size());
      break;
    }
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
    std::unordered_map<std::string, std::string> dbToOriginal;
    std::vector<std::string> chunkTitles;
    chunkTitles.reserve(count);
    for (size_t i = 0; i < count; ++i) {
      std::string orig = normalizedTitles[index + i];
      std::string dbTitle = orig;
      // DB内のタイトルは末尾の')'が削除されているため、検索時も末尾の')'を取り除く
      if (!dbTitle.empty() && dbTitle.back() == ')') {
        dbTitle.pop_back();
      }
      chunkTitles.push_back(dbTitle);
      dbToOriginal[dbTitle] = orig;
    }

    for (size_t i = 0; i < count; ++i) {
      sqlite3_bind_text(
          stmt, static_cast<int>(i + 1),
          chunkTitles[i].c_str(), -1, SQLITE_STATIC);
    }

    size_t chunkHits = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      const int pageId = sqlite3_column_int(stmt, 0);
      const char* storedTitle =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      if (storedTitle) {
        std::string sTitle = storedTitle;
        auto it = dbToOriginal.find(sTitle);
        if (it != dbToOriginal.end()) {
          result[it->second] = pageId;
        } else {
          result[sTitle] = pageId;
        }
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
      // DB内のタイトルは末尾の')'が削除されているため、'('があって')'がない場合は復元する
      if (title.find('(') != std::string::npos && title.find(')') == std::string::npos) {
        title += ')';
      }
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
    const std::function<void(const std::string&, int)>& onResolved,
    const std::atomic<bool>* cancelRequested) {
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
  const auto isCancelled = [&]() {
    return IsCancelRequested() ||
           (cancelRequested &&
            cancelRequested->load(std::memory_order_relaxed));
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
      },
      cancelRequested);

  if (isCancelled()) {
    LOG_WARN("WikiShortestPath",
             "ComputeDistancesToTarget cancelled during title resolution");
    return distances;
  }

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

  // 双方向BFS: ターゲット1点から incoming_links だけで外側へ広げると、
  // ターゲットが入次数の大きいハブ記事（都道府県・国等）の場合、2〜3ホップ目で
  // グラフのほぼ全体（実測で数百万〜億単位のエッジ）に達してしまい、致命的に遅い。
  // そのため、未解決の複数ソース側からも outgoing_links で広げ、毎ラウンド
  // 「フロンティアが小さい側」だけを1段広げる（単一ペア用の FindShortestPath と
  // 同じ発想）。forward側は複数ソースの合流探索になるため、各ノードへ
  // 「最初に到達した起点ソース」を記録しておき、backward側と出会った時点で
  // そのソースの距離を確定させる。
  struct ForwardNode {
    int originSourceId; ///< このノードへ最初に到達した起点ソースのページID
    int depth;          ///< 起点ソースからの距離
  };

  const auto resolveSource = [&](int sourceId, int depth) {
    if (unresolvedPageIds.find(sourceId) == unresolvedPageIds.end()) {
      return;
    }
    if (auto titleIt = titlesByPageId.find(sourceId);
        titleIt != titlesByPageId.end()) {
      for (const auto &title : titleIt->second) {
        distances[title] = depth;
        if (onResolved) {
          onResolved(title, depth);
        }
      }
    }
    unresolvedPageIds.erase(sourceId);

    // このページからターゲットまでの距離を確定できたので、同じターゲットを
    // 目指している間は別のページに同じリンクが出てきても即答できるように憶えておく。
    auto &resolvedCache = GetResolvedDistanceCache();
    std::lock_guard<std::mutex> resolvedCacheLock(resolvedCache.mutex);
    if (resolvedCache.targetId == targetId) {
      resolvedCache.distanceByPageId[sourceId] = depth;
    }
  };

  // 確定済み距離キャッシュ: 同じリンク先ページは複数のページに何度も登場するため、
  // 一度解決した（ページID, 距離）を憶えておき、再登場時はBFSを一切行わず即答する。
  // 注意: resolveSource() 自体が内部で resolvedCache.mutex をロックするため、
  // ここでロックを保持したまま resolveSource() を呼ぶと同一スレッドによる
  // 二重ロック（std::mutex は非再帰なので未定義動作）になる。そのため
  // ヒット候補の収集とロック解放を先に済ませてから resolveSource() を呼ぶ。
  {
    const size_t distancesBeforeResolvedCache = distances.size();
    std::vector<std::pair<int, int>> cacheHits; // (pageId, depth)
    size_t cachedEntryCount = 0;
    {
      auto &resolvedCache = GetResolvedDistanceCache();
      std::lock_guard<std::mutex> resolvedCacheLock(resolvedCache.mutex);
      if (resolvedCache.targetId != targetId) {
        resolvedCache.targetId = targetId;
        resolvedCache.distanceByPageId.clear();
      }
      for (int srcId : unresolvedPageIds) {
        if (auto it = resolvedCache.distanceByPageId.find(srcId);
            it != resolvedCache.distanceByPageId.end()) {
          cacheHits.emplace_back(srcId, it->second);
        }
      }
      cachedEntryCount = resolvedCache.distanceByPageId.size();
    }
    for (const auto &[srcId, depth] : cacheHits) {
      resolveSource(srcId, depth);
    }
    if (const size_t resolvedCacheHits =
            distances.size() - distancesBeforeResolvedCache;
        resolvedCacheHits > 0) {
      LOG_INFO("WikiShortestPath",
               "ComputeDistancesToTarget resolved-distance cache reused: "
               "cachedEntries={} immediateHits={}",
               cachedEntryCount, resolvedCacheHits);
    }
  }
  if (unresolvedPageIds.empty()) {
    storeProgress(sourceTitles.size() + static_cast<size_t>(maxDepth) *
                                            kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget completed via resolved-distance cache: "
             "resolved={} elapsed={}ms",
             distances.size(), ElapsedMs(computeStartedAt));
    return distances;
  }

  // ターゲット起点のBFSキャッシュから引き継ぐ。同じゲーム内で同じターゲットを
  // 目指している間はページ移動のたびに incoming_links を再探索せずに済む
  // （ターゲットが変わっていればここでリセットされる）。
  std::vector<int> frontierBackward;
  std::unordered_map<int, int> backwardDepth;
  {
    auto &cache = GetBackwardBfsCache();
    std::lock_guard<std::mutex> cacheLock(cache.mutex);
    if (cache.targetId != targetId) {
      cache.targetId = targetId;
      cache.depthByPageId = {{targetId, 0}};
      cache.frontier = {targetId};
    }
    backwardDepth = cache.depthByPageId;
    frontierBackward = cache.frontier;
  }
  const size_t backwardCacheHitNodes = backwardDepth.size();
  const size_t distancesBeforeCacheHits = distances.size();

  // キャッシュに既に含まれているソースはDBに触れずその場で解決する。
  {
    const std::vector<int> unresolvedSnapshot(unresolvedPageIds.begin(),
                                              unresolvedPageIds.end());
    for (int srcId : unresolvedSnapshot) {
      if (auto it = backwardDepth.find(srcId); it != backwardDepth.end()) {
        resolveSource(srcId, it->second);
      }
    }
  }
  if (const size_t cachedHits = distances.size() - distancesBeforeCacheHits;
      cachedHits > 0) {
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget backward cache reused: cachedNodes={} "
             "immediateHits={}",
             backwardCacheHitNodes, cachedHits);
  }

  std::vector<int> frontierForward;
  frontierForward.reserve(unresolvedPageIds.size());
  std::unordered_map<int, ForwardNode> forwardInfo;
  forwardInfo.reserve(unresolvedPageIds.size() * 2);
  for (int srcId : unresolvedPageIds) {
    forwardInfo[srcId] = ForwardNode{srcId, 0};
    frontierForward.push_back(srcId);
  }

  for (int depth = 1; depth <= maxDepth && !unresolvedPageIds.empty() &&
                      !frontierForward.empty() && !frontierBackward.empty();
       ++depth) {
    if (isCancelled()) {
      LOG_WARN("WikiShortestPath",
               "ComputeDistancesToTarget cancelled before depth={}",
               depth);
      break;
    }

    const bool expandForward =
        frontierForward.size() <= frontierBackward.size();
    const auto &expandingFrontier =
        expandForward ? frontierForward : frontierBackward;
    const auto depthStartedAt = std::chrono::steady_clock::now();
    const size_t frontierBefore = expandingFrontier.size();
    const size_t unresolvedBefore = unresolvedPageIds.size();
    const size_t resolvedBefore = distances.size();
    const auto fetchStartedAt = std::chrono::steady_clock::now();
    const size_t depthBase =
        sourceTitles.size() +
        static_cast<size_t>(depth - 1) * kPathEvaluationDepthProgressUnits;

    std::unordered_map<int, std::vector<int>> linkMap;
    if (!FetchLinks(
            m_db, expandingFrontier,
            expandForward ? "outgoing_links" : "incoming_links", linkMap,
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
            },
            cancelRequested)) {
      LOG_ERROR("WikiShortestPath",
                "ComputeDistancesToTarget BFS failed: depth={} side={} "
                "frontier={} elapsed={}ms",
                depth, expandForward ? "forward" : "backward", frontierBefore,
                ElapsedMs(depthStartedAt));
      return distances;
    }
    const long long fetchMs = ElapsedMs(fetchStartedAt);

    size_t traversedLinks = 0;
    bool cancelledDuringTraversal = false;
    size_t traversedEdges = 0;

    if (expandForward) {
      std::vector<int> next;
      for (int pageId : frontierForward) {
        if (isCancelled()) {
          cancelledDuringTraversal = true;
          break;
        }
        const auto parentIt = forwardInfo.find(pageId);
        const auto linkIt = linkMap.find(pageId);
        if (parentIt == forwardInfo.end() || linkIt == linkMap.end()) {
          continue;
        }
        const ForwardNode parent = parentIt->second;
        const int nextDepth = parent.depth + 1;
        traversedLinks += linkIt->second.size();
        for (int nb : linkIt->second) {
          if ((traversedEdges++ & 4095U) == 0U && isCancelled()) {
            cancelledDuringTraversal = true;
            break;
          }
          const auto [it, inserted] = forwardInfo.try_emplace(
              nb, ForwardNode{parent.originSourceId, nextDepth});
          if (inserted) {
            next.push_back(nb);
          }
          if (const auto bIt = backwardDepth.find(nb);
              bIt != backwardDepth.end()) {
            resolveSource(it->second.originSourceId,
                          it->second.depth + bIt->second);
          }
        }
        if (cancelledDuringTraversal) {
          break;
        }
      }
      frontierForward = std::move(next);
    } else {
      std::vector<int> next;
      for (int pageId : frontierBackward) {
        if (isCancelled()) {
          cancelledDuringTraversal = true;
          break;
        }
        const auto depthIt = backwardDepth.find(pageId);
        const auto linkIt = linkMap.find(pageId);
        if (depthIt == backwardDepth.end() || linkIt == linkMap.end()) {
          continue;
        }
        const int nextDepth = depthIt->second + 1;
        traversedLinks += linkIt->second.size();
        for (int nb : linkIt->second) {
          if ((traversedEdges++ & 4095U) == 0U && isCancelled()) {
            cancelledDuringTraversal = true;
            break;
          }
          const auto [it, inserted] = backwardDepth.try_emplace(nb, nextDepth);
          if (inserted) {
            next.push_back(nb);
          }
          resolveSource(nb, it->second);
          if (const auto fIt = forwardInfo.find(nb); fIt != forwardInfo.end()) {
            resolveSource(fIt->second.originSourceId,
                          fIt->second.depth + it->second);
          }
        }
        if (cancelledDuringTraversal) {
          break;
        }
      }
      frontierBackward = std::move(next);
    }

    if (cancelledDuringTraversal) {
      LOG_WARN("WikiShortestPath",
               "ComputeDistancesToTarget cancelled while traversing depth={}",
               depth);
      break;
    }

    storeProgress(sourceTitles.size() + static_cast<size_t>(depth) *
                                            kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget depth={} side={} frontier={} rows={} "
             "links={} forwardFrontier={} backwardFrontier={} resolvedDelta={} "
             "resolved={} unresolvedDelta={} unresolved={} fetch={}ms elapsed={}ms",
             depth, expandForward ? "forward" : "backward", frontierBefore,
             linkMap.size(), traversedLinks, frontierForward.size(),
             frontierBackward.size(), distances.size() - resolvedBefore,
             distances.size(), unresolvedBefore - unresolvedPageIds.size(),
             unresolvedPageIds.size(), fetchMs, ElapsedMs(depthStartedAt));
  }

  storeProgress(sourceTitles.size() + static_cast<size_t>(maxDepth) *
                                          kPathEvaluationDepthProgressUnits);

  // backward側で新たに到達したノードをキャッシュへ書き戻す。次に同じ
  // ターゲットで呼ばれたときはこの続きから再開できる。ターゲットが
  // 既に切り替わっていれば（別ホールの呼び出しが割り込んでいれば）書き戻さない。
  if (backwardDepth.size() > backwardCacheHitNodes) {
    auto &cache = GetBackwardBfsCache();
    std::lock_guard<std::mutex> cacheLock(cache.mutex);
    if (cache.targetId == targetId &&
        backwardDepth.size() > cache.depthByPageId.size()) {
      cache.depthByPageId = backwardDepth;
      cache.frontier = frontierBackward;
    }
  }

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

    // メタページ、曖昧さ回避、テンプレート、特定のシステムページの除外
    if (title.find("曖昧さ回避") != std::string::npos ||
        title.find("分類学") != std::string::npos ||
        title.find("ウェイバックマシン") != std::string::npos) {
      isIgnored = true;
    }
    if (title.find("プロジェクト:") != std::string::npos ||
        title.find("Wikipedia:") != std::string::npos ||
        title.find("Help:") != std::string::npos ||
        title.find("Template:") != std::string::npos ||
        title.find("Category:") != std::string::npos ||
        title.find("Portal:") != std::string::npos) {
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
