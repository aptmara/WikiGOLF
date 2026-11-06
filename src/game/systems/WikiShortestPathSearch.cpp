/**
 * @file WikiShortestPathSearch.cpp
 * @brief 2記事間の最短経路
*/

#include "WikiShortestPath.h"
#include "WikiShortestPathInternals.h"
#include "WikiLinkGraph.h"
#include "../../core/Logger.h"

namespace game::systems {

using namespace wiki_shortest_path_detail;

ShortestPathResult WikiShortestPath::FindShortestPath(
    const std::string &sourceTitle, const std::string &targetTitle,
    int maxDepth, bool logSuccess) {
  const int targetId = FetchPageId(targetTitle);
  return FindShortestPath(sourceTitle, targetId, maxDepth, logSuccess);
}

ShortestPathResult
WikiShortestPath::FindShortestPath(const std::string &sourceTitle, int targetId,
                                   int maxDepth, bool logSuccess) {
  ShortestPathResult result;
  if (!m_graph) {
    result.errorMessage = "データベース未初期化";
    return result;
  }
  const std::uint32_t target =
      targetId < 0 ? WikiLinkGraph::kInvalid
                   : m_graph->FromWikiPageId(static_cast<std::uint32_t>(targetId));
  if (target == WikiLinkGraph::kInvalid) {
    result.errorMessage = "ターゲットIDが無効です";
    return result;
  }
  const std::uint32_t source = FindArticle(sourceTitle);
  if (source == WikiLinkGraph::kInvalid) {
    result.errorMessage = "開始記事が見つかりません";
    return result;
  }
  if (source == target) {
    result.success = true;
    result.degrees = 0;
    result.path = {sourceTitle};
    return result;
  }

  const DistancesPtr distances = GetDistances(m_graph, target);
  if (!distances) {
    result.errorMessage = "リンク取得に失敗しました";
    return result;
  }
  const std::uint8_t hops = distances->hops[source];
  if (hops == WikiLinkGraph::kUnreachable || static_cast<int>(hops) > maxDepth) {
    result.errorMessage = "経路が見つかりません";
    return result;
  }

  result.success = true;
  result.degrees = hops;
  for (std::uint32_t article = source; article != WikiLinkGraph::kInvalid;
       article = distances->next[article]) {
    result.path.push_back(ArticleTitle(article));
  }
  if (logSuccess) {
    std::string pathText;
    for (std::size_t i = 0; i < result.path.size(); ++i) {
      if (i > 0) {
        pathText += " -> ";
      }
      pathText += result.path[i];
    }
    LOG_INFO("WikiShortestPath", "{} -> ID:{} ({} hops): {}", sourceTitle,
             targetId, result.degrees, pathText);
  }
  return result;
}

} // namespace game::systems
