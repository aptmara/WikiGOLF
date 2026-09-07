#pragma once
/**
 * @file WikiShortestPathInternals.h
 * @brief WikiShortestPathが共有するSQLite/BFS内部処理です。
*/

#include "WikiShortestPath.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <queue>
#include <sqlite3.h>
#include <sstream>

namespace game::systems::wiki_shortest_path_detail {


// IN句のチャンクサイズ。ベンチでは 512〜1024 で差がほぼ無かったため安全側の512に固定。
constexpr size_t kLinkChunkSize = 512;
constexpr size_t kDetailedChunkLogLimit = 3;
constexpr long long kSlowLinkFetchChunkMs = 250;
constexpr size_t kPathEvaluationDepthProgressUnits = 100;

inline std::recursive_mutex g_sqliteMutex;

// 実行中の全 WikiShortestPath インスタンスが開いている sqlite3* の一覧。
// RequestCancelAll() から sqlite3_interrupt() を呼ぶために保持する。
inline std::mutex g_activeDbMutex;
inline std::vector<sqlite3 *> g_activeDbs;
inline std::atomic<bool> g_cancelRequested{false};

inline void RegisterActiveDb(sqlite3 *db) {
  std::lock_guard<std::mutex> lock(g_activeDbMutex);
  g_activeDbs.push_back(db);
}

inline void UnregisterActiveDb(sqlite3 *db) {
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
 * @brief 開始時刻からの経過時間をミリ秒で返します。
*/
inline long long ElapsedMs(const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

inline std::vector<int> ParseLinks(const unsigned char *text) {
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

inline bool FetchLinks(sqlite3 *db, const std::vector<int> &pageIds,
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
        size_t rawBytes = 0;
        if (raw) {
          rawBytes = std::strlen(reinterpret_cast<const char *>(raw));
        }
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

inline std::vector<int> BuildPath(int meet,
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
 * @brief ゴール記事（ターゲット）起点の逆方向BFS結果をプロセス内で使い回すキャッシュです。
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

inline BackwardBfsCache &GetBackwardBfsCache() {
  static BackwardBfsCache cache;
  return cache;
}

/**
 * @brief 特定ページからターゲットまでの「確定済み距離」をプロセス内で
 *        使い回すキャッシュです。
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

inline ResolvedDistanceCache &GetResolvedDistanceCache() {
  static ResolvedDistanceCache cache;
  return cache;
}


} // namespace game::systems::wiki_shortest_path_detail


