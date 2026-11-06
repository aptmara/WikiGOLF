#pragma once
/**
 * @file WikiShortestPathInternals.h
 * @brief WikiShortestPath が共有する、ゴールごとの手数表のキャッシュです。
 *
 * 1回のプレイ中はゴールが変わらないため、ゴールから逆向きに BFS した手数表を
 * 1つ作れば、どのページのどのリンクの手数も表を引くだけで答えられます。
 * 手数表は（グラフ, ゴール, セーフモードかどうか）ごとに作り、直近の数個だけ残します。
 * 同じゴールの表を別スレッドが計算中なら、その完了を待って同じ表を使います。
*/

#include "ContentFilter.h"
#include "WikiLinkGraph.h"
#include "WikiShortestPath.h"
#include "../../core/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace game::systems::wiki_shortest_path_detail {

inline std::atomic<bool> g_cancelRequested{false};

/** @brief 開始時刻からの経過時間をミリ秒で返します。*/
inline long long ElapsedMs(const std::chrono::steady_clock::time_point &startedAt) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now() - startedAt)
      .count();
}

using DistancesPtr = std::shared_ptr<const WikiLinkGraph::Distances>;

struct DistanceCacheEntry {
  const WikiLinkGraph *graph = nullptr;
  std::uint32_t target = WikiLinkGraph::kInvalid;
  bool safeMode = false;
  std::shared_future<DistancesPtr> result;
};

struct DistanceCache {
  /** 残す手数表の数（1つ約 7.6MB）。ゴールの再抽選で数個を行き来するため少し持つ*/
  static constexpr std::size_t kCapacity = 4;
  std::mutex mutex;
  std::vector<DistanceCacheEntry> entries; // 新しいものが末尾
};

inline DistanceCache &GetDistanceCache() {
  static DistanceCache cache;
  return cache;
}

/** @brief 通さない記事（記事番号ごとに 0/1）を、今のセーフモード設定で作ります。*/
inline std::vector<std::uint8_t> BuildBlockedArticles(const WikiLinkGraph &graph) {
  std::vector<std::uint8_t> blocked(graph.ArticleCount(), 0);
  ContentFilter::Instance().ForEachBlockedPageId([&](int pageId) {
    if (pageId < 0) {
      return;
    }
    const std::uint32_t article = graph.FromWikiPageId(static_cast<std::uint32_t>(pageId));
    if (article != WikiLinkGraph::kInvalid) {
      blocked[article] = 1;
    }
  });
  return blocked;
}

/**
 * @brief ゴールまでの手数表を返します（キャッシュになければ計算）。
 * @return 中断した場合は nullptr
*/
inline DistancesPtr GetDistances(const std::shared_ptr<const WikiLinkGraph> &graph,
                                 std::uint32_t target) {
  if (!graph || target >= graph->ArticleCount()) {
    return nullptr;
  }
  const bool safeMode = ContentFilter::Instance().IsSafeMode();
  auto &cache = GetDistanceCache();
  std::promise<DistancesPtr> promise;
  std::shared_future<DistancesPtr> existing;
  {
    std::lock_guard<std::mutex> lock(cache.mutex);
    for (auto it = cache.entries.begin(); it != cache.entries.end(); ++it) {
      if (it->graph == graph.get() && it->target == target && it->safeMode == safeMode) {
        // 最近使ったものとして末尾へ
        DistanceCacheEntry entry = *it;
        cache.entries.erase(it);
        cache.entries.push_back(entry);
        existing = entry.result;
        break;
      }
    }
    if (!existing.valid()) {
      if (cache.entries.size() >= DistanceCache::kCapacity) {
        cache.entries.erase(cache.entries.begin());
      }
      cache.entries.push_back({graph.get(), target, safeMode, promise.get_future().share()});
    }
  }
  if (existing.valid()) {
    // 別スレッドが計算中なら、ロックの外で完了を待つ
    return existing.get();
  }

  const auto startedAt = std::chrono::steady_clock::now();
  const auto blocked = BuildBlockedArticles(*graph);
  std::shared_ptr<WikiLinkGraph::Distances> distances =
      graph->ComputeDistances(target, blocked, 0, &g_cancelRequested);
  if (distances->cancelled) {
    promise.set_value(nullptr);
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.entries.erase(
        std::remove_if(cache.entries.begin(), cache.entries.end(),
                       [&](const DistanceCacheEntry &e) {
                         return e.graph == graph.get() && e.target == target &&
                                e.safeMode == safeMode;
                       }),
        cache.entries.end());
    LOG_WARN("WikiShortestPath", "Distance table cancelled: target={}", target);
    return nullptr;
  }
  LOG_INFO("WikiShortestPath",
           "Distance table built: target={} reached={}/{} safeMode={} elapsed={}ms",
           target, distances->reached, graph->ArticleCount(), safeMode,
           ElapsedMs(startedAt));
  DistancesPtr result = distances;
  promise.set_value(result);
  return result;
}

inline void ClearDistanceCache() {
  auto &cache = GetDistanceCache();
  std::lock_guard<std::mutex> lock(cache.mutex);
  cache.entries.clear();
}

} // namespace game::systems::wiki_shortest_path_detail
