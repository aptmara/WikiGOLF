/**
 * @file WikiShortestPathDistances.cpp
 * @brief 複数記事からゴールまでの手数の一括計算
*/

#include "WikiShortestPath.h"
#include "WikiShortestPathInternals.h"
#include "WikiLinkGraph.h"
#include "../../core/Logger.h"

#include <algorithm>
#include <unordered_map>

namespace game::systems {

using namespace wiki_shortest_path_detail;

namespace {
/** @brief 呼び出し元（AsyncPathEvaluator）の進捗の単位に合わせた、1段あたりの単位数*/
constexpr size_t kPathEvaluationDepthProgressUnits = 100;
} // namespace

std::unordered_map<std::string, int> WikiShortestPath::ComputeDistancesToTarget(
    const std::vector<std::string> &sourceTitles, int targetId, int maxDepth,
    std::atomic<size_t> *progressUnits, size_t progressBase,
    const std::function<void(const std::string &, int)> &onResolved,
    const std::atomic<bool> *cancelRequested) {
  const auto startedAt = std::chrono::steady_clock::now();
  std::unordered_map<std::string, int> distances;
  const auto finishProgress = [&] {
    if (progressUnits) {
      progressUnits->store(progressBase + sourceTitles.size() +
                               static_cast<size_t>(std::max(0, maxDepth)) *
                                   kPathEvaluationDepthProgressUnits,
                           std::memory_order_relaxed);
    }
  };
  const std::uint32_t target =
      (!m_graph || targetId < 0)
          ? WikiLinkGraph::kInvalid
          : m_graph->FromWikiPageId(static_cast<std::uint32_t>(targetId));
  if (target == WikiLinkGraph::kInvalid || sourceTitles.empty() || maxDepth < 0) {
    LOG_WARN("WikiShortestPath",
             "ComputeDistancesToTarget skipped: graph={}, targetId={}, sources={}, maxDepth={}",
             m_graph ? "ready" : "null", targetId, sourceTitles.size(), maxDepth);
    finishProgress();
    return distances;
  }

  const DistancesPtr table = GetDistances(m_graph, target);
  if (!table || (cancelRequested && cancelRequested->load(std::memory_order_relaxed))) {
    LOG_WARN("WikiShortestPath", "ComputeDistancesToTarget cancelled: targetId={}", targetId);
    finishProgress();
    return distances;
  }

  size_t missing = 0;
  size_t unreachable = 0;
  std::unordered_map<std::string, std::uint32_t> articleByTitle;
  for (size_t i = 0; i < sourceTitles.size(); ++i) {
    const std::string &title = sourceTitles[i];
    if (title.empty() || distances.count(title)) {
      continue;
    }
    const std::uint32_t article = FindArticle(title);
    if (article == WikiLinkGraph::kInvalid) {
      ++missing;
      continue;
    }
    const std::uint8_t hops = table->hops[article];
    if (hops == WikiLinkGraph::kUnreachable || static_cast<int>(hops) > maxDepth) {
      ++unreachable;
      continue;
    }
    distances[title] = hops;
    if (onResolved) {
      onResolved(title, hops);
    }
    if (progressUnits && (i & 63) == 0) {
      progressUnits->store(progressBase + i, std::memory_order_relaxed);
    }
  }
  finishProgress();
  LOG_INFO("WikiShortestPath",
           "Computed target distances: sources={}, resolved={}, missing={}, "
           "unreachable={}, targetId={}, maxDepth={}, elapsed={}ms",
           sourceTitles.size(), distances.size(), missing, unreachable, targetId,
           maxDepth, ElapsedMs(startedAt));
  return distances;
}

} // namespace game::systems
