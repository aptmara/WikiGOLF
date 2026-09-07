/**
 * @file WikiShortestPathSearch.cpp
 * @brief WikiShortestPathの責務別実装です。
*/

#include "WikiShortestPath.h"
#include "WikiShortestPathInternals.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <queue>
#include <sqlite3.h>
#include <sstream>

namespace game::systems {

using namespace wiki_shortest_path_detail;

ShortestPathResult WikiShortestPath::FindShortestPath(
    const std::string &sourceTitle,
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

} // namespace game::systems
