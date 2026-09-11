/**
 * @file WikiShortestPathPopular.cpp
 * @brief WikiShortestPathの責務別実装です。
*/

#include "WikiShortestPath.h"
#include "DailyChallengeRules.h"
#include "WikiShortestPathInternals.h"
#include "../../core/Logger.h"
#include <algorithm>
#include <queue>
#include <sqlite3.h>
#include <sstream>

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

std::pair<std::string, int>
WikiShortestPath::FetchPopularPageTitleWithIndexSelector(
    const std::function<std::size_t(std::size_t)> &selectIndex) {
  if (!m_db)
    return {"", -1};

  // キャッシュからランダムに選ぶ
  if (m_popularPageIds.empty()) {
    LOG_WARN("WikiShortestPath", "No popular pages cached.");
    return {"", -1};
  }

  // 10回リトライ（フィルタリング用）
  for (int i = 0; i < 10; ++i) {
    const std::size_t index = selectIndex(m_popularPageIds.size());
    int pageId = m_popularPageIds[index];

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

