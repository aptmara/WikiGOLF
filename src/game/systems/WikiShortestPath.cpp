/**
 * @file WikiShortestPath.cpp
 * @brief 日本語Wikipedia最短経路計算実装
 */

#include "WikiShortestPath.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <ctime>
#include <queue>
#include <sqlite3.h>
#include <sstream>

namespace game::systems {

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

  // 同一ページ
  if (sourceId == targetId) {
    result.success = true;
    result.degrees = 0;
    result.path = {sourceTitle};
    return result;
  }

  // 双方向BFS
  std::unordered_map<int, std::vector<int>> unvisitedForward;
  std::unordered_map<int, std::vector<int>> unvisitedBackward;
  std::unordered_map<int, std::vector<int>> visitedForward;
  std::unordered_map<int, std::vector<int>> visitedBackward;

  unvisitedForward[sourceId] = {-1};
  unvisitedBackward[targetId] = {-1};

  std::vector<std::vector<int>> foundPaths;
  int depth = 0;

  while (foundPaths.empty() && !unvisitedForward.empty() &&
         !unvisitedBackward.empty() && depth < maxDepth) {

    depth++;

    // 探索方向選択
    std::vector<int> forwardKeys, backwardKeys;
    for (auto &kv : unvisitedForward)
      forwardKeys.push_back(kv.first);
    for (auto &kv : unvisitedBackward)
      backwardKeys.push_back(kv.first);

    int forwardCount = FetchOutgoingLinksCount(forwardKeys);
    int backwardCount = FetchIncomingLinksCount(backwardKeys);

    if (forwardCount < backwardCount) {
      // 前方探索
      for (auto &kv : unvisitedForward) {
        visitedForward[kv.first] = kv.second;
      }

      std::unordered_map<int, std::vector<int>> newUnvisited;

      for (int pageId : forwardKeys) {
        std::string linksStr = FetchOutgoingLinks(pageId);
        std::istringstream iss(linksStr);
        std::string token;

        while (std::getline(iss, token, '|')) {
          if (token.empty())
            continue;
          int targetPageId = std::stoi(token);

          if (visitedForward.find(targetPageId) == visitedForward.end() &&
              newUnvisited.find(targetPageId) == newUnvisited.end()) {
            newUnvisited[targetPageId] = {pageId};
          } else if (newUnvisited.find(targetPageId) != newUnvisited.end()) {
            newUnvisited[targetPageId].push_back(pageId);
          }
        }
      }

      unvisitedForward = std::move(newUnvisited);

    } else {
      // 後方探索
      for (auto &kv : unvisitedBackward) {
        visitedBackward[kv.first] = kv.second;
      }

      std::unordered_map<int, std::vector<int>> newUnvisited;

      for (int pageId : backwardKeys) {
        std::string linksStr = FetchIncomingLinks(pageId);
        std::istringstream iss(linksStr);
        std::string token;

        while (std::getline(iss, token, '|')) {
          if (token.empty())
            continue;
          int sourcePageId = std::stoi(token);

          if (visitedBackward.find(sourcePageId) == visitedBackward.end() &&
              newUnvisited.find(sourcePageId) == newUnvisited.end()) {
            newUnvisited[sourcePageId] = {pageId};
          } else if (newUnvisited.find(sourcePageId) != newUnvisited.end()) {
            newUnvisited[sourcePageId].push_back(pageId);
          }
        }
      }

      unvisitedBackward = std::move(newUnvisited);
    }

    // パス完成チェック
    for (auto &kv : unvisitedForward) {
      int pageId = kv.first;
      if (unvisitedBackward.find(pageId) != unvisitedBackward.end()) {
        auto pathsFromSource = ReconstructPaths(kv.second, visitedForward);
        auto pathsFromTarget =
            ReconstructPaths(unvisitedBackward[pageId], visitedBackward);

        for (auto &ps : pathsFromSource) {
          for (auto &pt : pathsFromTarget) {
            std::vector<int> fullPath = ps;
            fullPath.push_back(pageId);
            for (auto it = pt.rbegin(); it != pt.rend(); ++it) {
              fullPath.push_back(*it);
            }
            foundPaths.push_back(fullPath);
          }
        }
      }
    }
  }

  if (foundPaths.empty()) {
    result.errorMessage = "経路が見つかりません";
    return result;
  }

  // 最短パスをタイトルに変換
  auto &shortestPath = foundPaths[0];
  result.success = true;
  result.degrees = (int)shortestPath.size() - 1;

  for (int pageId : shortestPath) {
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
