/**
 * @file WikiShortestPath.cpp
 * @brief 日本語Wikipedia最短経路計算実装
 */

#include "WikiShortestPath.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <queue>
#include <sqlite3.h>
#include <sstream>

namespace game::systems {

namespace {

// IN句のチャンクサイズ。ベンチでは 512〜1024 で差がほぼ無かったため安全側の512に固定。
constexpr size_t kLinkChunkSize = 512;

struct NodeInfo {
  int parent = -1;
  int depth = 0;
};

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
                std::unordered_map<int, std::vector<int>> &outLinks) {
  if (pageIds.empty())
    return true;

  size_t index = 0;
  while (index < pageIds.size()) {
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
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      LOG_ERROR("WikiShortestPath", "Failed to prepare link fetch SQL: {}",
                sqlite3_errmsg(db));
      return false;
    }

    for (size_t i = 0; i < count; ++i) {
      sqlite3_bind_int(stmt, static_cast<int>(i + 1),
                       pageIds[index + i]); // placeholders are 1-based
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      int pageId = sqlite3_column_int(stmt, 0);
      const unsigned char *raw = sqlite3_column_text(stmt, 1);
      outLinks[pageId] = ParseLinks(raw);
    }

    sqlite3_finalize(stmt);
    index += count;
  }

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
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }
}

bool WikiShortestPath::Initialize(const std::string &dbPath) {
  if (m_db) {
    sqlite3_close(m_db);
    m_db = nullptr;
  }

  int rc =
      sqlite3_open_v2(dbPath.c_str(), &m_db, SQLITE_OPEN_READONLY, nullptr);
  if (rc != SQLITE_OK) {
    LOG_ERROR("WikiShortestPath", "DB open failed: {}", sqlite3_errmsg(m_db));
    sqlite3_close(m_db);
    m_db = nullptr;
    return false;
  }

  LOG_INFO("WikiShortestPath", "Database initialized: {}", dbPath);

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

int WikiShortestPath::FetchPageId(const std::string &title) {
  if (!m_db)
    return -1;

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

std::string WikiShortestPath::FetchPageTitle(int pageId) {
  if (!m_db)
    return "";

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
                                   int maxDepth) {
  int targetId = FetchPageId(targetTitle);
  return FindShortestPath(sourceTitle, targetId, maxDepth);
}

ShortestPathResult
WikiShortestPath::FindShortestPath(const std::string &sourceTitle, int targetId,
                                   int maxDepth) {
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
  LOG_INFO("WikiShortestPath", "{} -> ID:{} ({} hops): {}", sourceTitle,
           targetId, result.degrees, pathStr);

  return result;
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
