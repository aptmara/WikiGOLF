/**
 * @file WikiShortestPath.cpp
 * @brief 日本語Wikipedia最短経路計算（初期化とタイトルの解決）
*/
#include "WikiShortestPath.h"
#include "WikiShortestPathInternals.h"
#include "WikiLinkGraph.h"
#include "WikiTitleLookup.h"
#include "../../core/Logger.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>

namespace game::systems {

using namespace wiki_shortest_path_detail;

WikiShortestPath::~WikiShortestPath() = default;

void WikiShortestPath::RequestCancelAll() {
  g_cancelRequested.store(true, std::memory_order_relaxed);
}

bool WikiShortestPath::IsCancelRequested() {
  return g_cancelRequested.load(std::memory_order_relaxed);
}

void WikiShortestPath::ClearProcessCaches() { ClearDistanceCache(); }

bool WikiShortestPath::Initialize(const std::string &graphPath,
                                  bool cachePopularPages) {
  if (IsCancelRequested()) {
    LOG_WARN("WikiShortestPath", "Initialize skipped: cancel already requested");
    return false;
  }
  m_popularArticles.clear();
  const auto startedAt = std::chrono::steady_clock::now();
  m_graph = WikiLinkGraph::Shared(graphPath, &g_cancelRequested);
  if (!m_graph) {
    LOG_ERROR("WikiShortestPath", "Link graph load failed: {}", graphPath);
    return false;
  }
  LOG_INFO("WikiShortestPath",
           "Link graph ready: {} source={} articles={} links={} memory={}MB elapsed={}ms",
           graphPath, m_graph->SourceName(), m_graph->ArticleCount(),
           m_graph->LinkCount(), m_graph->MemoryBytes() >> 20, ElapsedMs(startedAt));

  if (!cachePopularPages) {
    return true;
  }
  // 被リンクの多い記事（1万以上、上位2000件まで）をゴールの候補にする
  constexpr std::uint32_t kPopularMinIncoming = 10000;
  constexpr std::size_t kPopularLimit = 2000;
  for (const auto &[article, incoming] : m_graph->Popular()) {
    if (incoming < kPopularMinIncoming || m_popularArticles.size() >= kPopularLimit) {
      break;
    }
    m_popularArticles.push_back(article);
  }
  LOG_INFO("WikiShortestPath", "Cached {} popular pages.", m_popularArticles.size());

  // 乱数シード初期化（ターゲット選択用）
  srand(static_cast<unsigned int>(time(nullptr)));
  return true;
}

std::uint32_t WikiShortestPath::FindArticle(const std::string &title) const {
  if (!m_graph) {
    return WikiLinkGraph::kInvalid;
  }
  std::string normalized = title;
  std::replace(normalized.begin(), normalized.end(), ' ', '_');
  for (const auto &candidate : wiki_title_lookup::TitleCandidates(normalized)) {
    const std::uint32_t article = m_graph->FindTitle(candidate);
    if (article != WikiLinkGraph::kInvalid) {
      return article;
    }
  }
  return WikiLinkGraph::kInvalid;
}

int WikiShortestPath::ResolvePageId(const std::string &title) {
  return FetchPageId(title);
}

int WikiShortestPath::FetchPageId(const std::string &title) {
  const std::uint32_t article = FindArticle(title);
  if (article == WikiLinkGraph::kInvalid) {
    return -1;
  }
  return static_cast<int>(m_graph->WikiPageId(article));
}

std::string WikiShortestPath::ArticleTitle(std::uint32_t article) const {
  if (!m_graph) {
    return {};
  }
  std::string title = m_graph->Title(article);
  std::replace(title.begin(), title.end(), '_', ' ');
  return title;
}

} // namespace game::systems
