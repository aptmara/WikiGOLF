/**
 * @file WikiShortestPathPopular.cpp
 * @brief ゴール候補にする人気記事の選択
*/

#include "WikiShortestPath.h"
#include "WikiShortestPathInternals.h"
#include "ContentFilter.h"
#include "DailyChallengeRules.h"
#include "WikiLinkGraph.h"
#include "../../core/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace game::systems {

using namespace wiki_shortest_path_detail;

std::pair<std::string, int>
WikiShortestPath::FetchPopularPageTitle(int minIncomingLinks) {
  (void)minIncomingLinks;
  return FetchPopularPageTitleWithIndexSelector(
      [](std::size_t candidateCount) {
        return static_cast<std::size_t>(rand()) % candidateCount;
      });
}

std::pair<std::string, int>
WikiShortestPath::FetchPopularPageTitle(int minIncomingLinks,
                                        std::uint32_t seed) {
  (void)minIncomingLinks;
  DailyChallengeRandom random(seed);
  return FetchPopularPageTitleWithIndexSelector(
      [&random](std::size_t candidateCount) {
        return random.NextIndex(candidateCount);
      });
}

bool WikiShortestPath::IsUnsuitableChallengeTitle(const std::string &title,
                                                  int pageId) {
  if (title.empty() || ContentFilter::Instance().IsPageIdBlocked(pageId)) {
    return true;
  }
  if (std::all_of(title.begin(), title.end(),
                  [](unsigned char c) { return std::isdigit(c); })) {
    return true;
  }
  if (title.size() >= 3) {
    const std::string suffix = title.substr(title.size() - 3);
    if (suffix == "年" || suffix == "月" || suffix == "日") {
      return true;
    }
  }

  // メタページ、曖昧さ回避、テンプレート、特定のシステムページの除外
  if (title.find("曖昧さ回避") != std::string::npos ||
      title.find("分類学") != std::string::npos ||
      title.find("ウェイバックマシン") != std::string::npos) {
    return true;
  }
  if (title.find("プロジェクト:") != std::string::npos ||
      title.find("Wikipedia:") != std::string::npos ||
      title.find("Help:") != std::string::npos ||
      title.find("Template:") != std::string::npos ||
      title.find("Category:") != std::string::npos ||
      title.find("Portal:") != std::string::npos) {
    return true;
  }
  return false;
}

std::pair<std::string, int>
WikiShortestPath::FetchDailyStartPage(int targetId, std::uint32_t seed,
                                      int *hops) {
  if (!m_graph || targetId < 0) {
    return {"", -1};
  }
  const std::uint32_t target =
      m_graph->FromWikiPageId(static_cast<std::uint32_t>(targetId));
  if (target == WikiLinkGraph::kInvalid) {
    return {"", -1};
  }
  const auto startedAt = std::chrono::steady_clock::now();
  const DistancesPtr table = GetDistances(m_graph, target);
  if (!table) {
    return {"", -1};
  }

  int attempts = 0;
  const std::uint32_t article = PickDailyStartArticle(
      seed, m_graph->ArticleCount(),
      [&](std::uint32_t candidate) {
        if (candidate == target) {
          return DailyStartFit::Reject;
        }
        const std::uint8_t candidateHops = table->hops[candidate];
        if (candidateHops == WikiLinkGraph::kUnreachable) {
          return DailyStartFit::Reject;
        }
        const DailyStartFit fit = ClassifyDailyStartHops(candidateHops);
        if (fit == DailyStartFit::Reject) {
          return fit;
        }
        const std::uint32_t incoming = m_graph->IncomingCount(candidate);
        if (incoming < kDailyStartMinIncoming ||
            incoming >= kDailyStartMaxIncoming) {
          return DailyStartFit::Reject;
        }
        const int pageId = static_cast<int>(m_graph->WikiPageId(candidate));
        const std::string title = ArticleTitle(candidate);
        // 一覧記事はリンクだらけで「どこへでも行ける」ので、スタートには使わない。
        if (IsUnsuitableChallengeTitle(title, pageId) ||
            title.find("一覧") != std::string::npos) {
          return DailyStartFit::Reject;
        }
        return fit;
      },
      &attempts);
  if (article >= m_graph->ArticleCount()) {
    LOG_WARN("WikiShortestPath",
             "Daily start not found: targetId={} seed={} attempts={}",
             targetId, seed, attempts);
    return {"", -1};
  }
  const int pageId = static_cast<int>(m_graph->WikiPageId(article));
  const std::string title = ArticleTitle(article);
  if (hops) {
    *hops = table->hops[article];
  }
  LOG_INFO("WikiShortestPath",
           "Daily start selected: {} (ID: {}) hops={} incoming={} attempts={} "
           "elapsed={}ms",
           title, pageId, static_cast<int>(table->hops[article]),
           m_graph->IncomingCount(article), attempts, ElapsedMs(startedAt));
  return {title, pageId};
}

std::pair<std::string, int>
WikiShortestPath::FetchPopularPageTitleWithIndexSelector(
    const std::function<std::size_t(std::size_t)> &selectIndex) {
  if (!m_graph) {
    return {"", -1};
  }
  if (m_popularArticles.empty()) {
    LOG_WARN("WikiShortestPath", "No popular pages cached.");
    return {"", -1};
  }

  // 10回リトライ（フィルタリング用）
  for (int i = 0; i < 10; ++i) {
    const std::size_t index = selectIndex(m_popularArticles.size());
    const std::uint32_t article = m_popularArticles[index];
    const int pageId = static_cast<int>(m_graph->WikiPageId(article));
    const std::string title = ArticleTitle(article);

    const bool isIgnored = IsUnsuitableChallengeTitle(title, pageId);

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
