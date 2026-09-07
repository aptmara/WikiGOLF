/**
 * @file WikiShortestPathDistances.cpp
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

std::unordered_map<std::string, int> WikiShortestPath::ComputeDistancesToTarget(
    const std::vector<std::string> &sourceTitles, int targetId, int maxDepth,
    std::atomic<size_t> *progressUnits, size_t progressBase,
    const std::function<void(const std::string&, int)>& onResolved,
    const std::atomic<bool>* cancelRequested) {
  const auto computeStartedAt = std::chrono::steady_clock::now();
  std::unordered_map<std::string, int> distances;
  if (!m_db || targetId < 0 || sourceTitles.empty() || maxDepth < 0) {
    LOG_WARN("WikiShortestPath",
             "ComputeDistancesToTarget skipped: db={}, targetId={}, sources={}, maxDepth={}",
             m_db ? "ready" : "null", targetId, sourceTitles.size(), maxDepth);
    return distances;
  }

  LOG_INFO("WikiShortestPath",
           "ComputeDistancesToTarget started: sources={}, targetId={}, maxDepth={}",
           sourceTitles.size(), targetId, maxDepth);

  const auto storeProgress = [&](size_t units) {
    if (progressUnits) {
      progressUnits->store(progressBase + units, std::memory_order_relaxed);
    }
  };
  const auto isCancelled = [&]() {
    return IsCancelRequested() ||
           (cancelRequested &&
            cancelRequested->load(std::memory_order_relaxed));
  };

  std::unordered_map<int, std::vector<std::string>> titlesByPageId;
  std::unordered_set<int> unresolvedPageIds;
  size_t missingPageIds = 0;
  const auto resolveStartedAt = std::chrono::steady_clock::now();

  // --- バッチタイトル解決 ---
  // 1件ずつ FetchPageId を呼ぶ代わりに IN句バッチクエリで一括解決します。
  // FetchLinks と同じ kLinkChunkSize（512件）のチャンク方式を採用します。

  // スペース→アンダースコア正規化と重複排除を行います。
  // normalizedToOriginals: 正規化タイトル → 元タイトル列
  std::unordered_map<std::string, std::vector<std::string>> normalizedToOriginals;
  std::vector<std::string> uniqueNormalized;
  uniqueNormalized.reserve(sourceTitles.size());

  for (const auto& title : sourceTitles) {
    if (title.empty() || distances.find(title) != distances.end()) {
      continue; // 解決済み・空はスキップ
    }
    std::string normalized = title;
    std::replace(normalized.begin(), normalized.end(), ' ', '_');
    if (normalizedToOriginals.find(normalized) == normalizedToOriginals.end()) {
      uniqueNormalized.push_back(normalized);
    }
    normalizedToOriginals[normalized].push_back(title);
  }

  // バッチSQLでページIDを一括取得します。
  // チャンク完了コールバック内で storeProgress を呼び、
  // 1チャンク処理ごとに進捗バーが連続的に動くようにします。
  const size_t srcSize = sourceTitles.size();
  const auto batchIdMap = FetchPageIdsBatch(
      uniqueNormalized,
      [&](size_t processed, size_t total) {
        // バッチ内の処理済み割合を sourceTitles.size() 分にスケールして報告します。
        const size_t units =
            total > 0 ? (processed * srcSize + total - 1) / total : srcSize;
        storeProgress(units);
      },
      cancelRequested);

  if (isCancelled()) {
    LOG_WARN("WikiShortestPath",
             "ComputeDistancesToTarget cancelled during title resolution");
    return distances;
  }

  // バッチ結果を titlesByPageId / distances / unresolvedPageIds に展開します。
  for (const auto& [normalizedTitle, pageId] : batchIdMap) {
    const auto origIt = normalizedToOriginals.find(normalizedTitle);
    if (origIt == normalizedToOriginals.end()) {
      continue; // DB側タイトルとクエリタイトルが異なるケース（通常は発生しない）
    }
    for (const auto& origTitle : origIt->second) {
      titlesByPageId[pageId].push_back(origTitle);
      if (pageId == targetId) {
        distances[origTitle] = 0;
        if (onResolved) {
          onResolved(origTitle, 0);
        }
      } else {
        unresolvedPageIds.insert(pageId);
      }
    }
    normalizedToOriginals.erase(origIt); // 解決済みを除去してmissing集計に使います
  }

  // batchIdMap に含まれなかったタイトルは DB 未登録です。
  for (const auto& [_, originals] : normalizedToOriginals) {
    missingPageIds += originals.size();
  }

  // ビッグバンプ起笪にコールバックで progress が未更新のままの場合は、
  // バッチ完了後に srcSize まで確実に更新します。
  storeProgress(srcSize);

  LOG_INFO("WikiShortestPath",
           "ComputeDistancesToTarget title resolution: sources={} uniquePageIds={} "
           "directHits={} unresolved={} missing={} elapsed={}ms",
           sourceTitles.size(), titlesByPageId.size(), distances.size(),
           unresolvedPageIds.size(), missingPageIds, ElapsedMs(resolveStartedAt));

  if (unresolvedPageIds.empty()) {
    storeProgress(sourceTitles.size() +
                  static_cast<size_t>(maxDepth) *
                      kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget completed without BFS: resolved={} elapsed={}ms",
             distances.size(), ElapsedMs(computeStartedAt));
    return distances;
  }

  // 双方向BFS: ターゲット1点から incoming_links だけで外側へ広げると、
  // ターゲットが入次数の大きいハブ記事（都道府県・国等）の場合、2〜3ホップ目で
  // グラフのほぼ全体（実測で数百万〜億単位のエッジ）に達してしまい、致命的に遅い。
  // そのため、未解決の複数ソース側からも outgoing_links で広げ、毎ラウンド
  // 「フロンティアが小さい側」だけを1段広げる（単一ペア用の FindShortestPath と
  // 同じ発想）。forward側は複数ソースの合流探索になるため、各ノードへ
  // 「最初に到達した起点ソース」を記録しておき、backward側と出会った時点で
  // そのソースの距離を確定させる。
  struct ForwardNode {
    int originSourceId; /**< このノードへ最初に到達した起点ソースのページID*/
    int depth;          /**< 起点ソースからの距離*/
  };

  const auto resolveSource = [&](int sourceId, int depth) {
    if (unresolvedPageIds.find(sourceId) == unresolvedPageIds.end()) {
      return;
    }
    if (auto titleIt = titlesByPageId.find(sourceId);
        titleIt != titlesByPageId.end()) {
      for (const auto &title : titleIt->second) {
        distances[title] = depth;
        if (onResolved) {
          onResolved(title, depth);
        }
      }
    }
    unresolvedPageIds.erase(sourceId);

    // このページからターゲットまでの距離を確定できたので、同じターゲットを
    // 目指している間は別のページに同じリンクが出てきても即答できるように憶えておく。
    auto &resolvedCache = GetResolvedDistanceCache();
    std::lock_guard<std::mutex> resolvedCacheLock(resolvedCache.mutex);
    if (resolvedCache.targetId == targetId) {
      resolvedCache.distanceByPageId[sourceId] = depth;
    }
  };

  // 確定済み距離キャッシュ: 同じリンク先ページは複数のページに何度も登場するため、
  // 一度解決した（ページID, 距離）を憶えておき、再登場時はBFSを一切行わず即答する。
  // 注意: resolveSource() 自体が内部で resolvedCache.mutex をロックするため、
  // ここでロックを保持したまま resolveSource() を呼ぶと同一スレッドによる
  // 二重ロック（std::mutex は非再帰なので未定義動作）になる。そのため
  // ヒット候補の収集とロック解放を先に済ませてから resolveSource() を呼ぶ。
  {
    const size_t distancesBeforeResolvedCache = distances.size();
    std::vector<std::pair<int, int>> cacheHits; // (pageId, depth)
    size_t cachedEntryCount = 0;
    {
      auto &resolvedCache = GetResolvedDistanceCache();
      std::lock_guard<std::mutex> resolvedCacheLock(resolvedCache.mutex);
      if (resolvedCache.targetId != targetId) {
        resolvedCache.targetId = targetId;
        resolvedCache.distanceByPageId.clear();
      }
      for (int srcId : unresolvedPageIds) {
        if (auto it = resolvedCache.distanceByPageId.find(srcId);
            it != resolvedCache.distanceByPageId.end()) {
          cacheHits.emplace_back(srcId, it->second);
        }
      }
      cachedEntryCount = resolvedCache.distanceByPageId.size();
    }
    for (const auto &[srcId, depth] : cacheHits) {
      resolveSource(srcId, depth);
    }
    if (const size_t resolvedCacheHits =
            distances.size() - distancesBeforeResolvedCache;
        resolvedCacheHits > 0) {
      LOG_INFO("WikiShortestPath",
               "ComputeDistancesToTarget resolved-distance cache reused: "
               "cachedEntries={} immediateHits={}",
               cachedEntryCount, resolvedCacheHits);
    }
  }
  if (unresolvedPageIds.empty()) {
    storeProgress(sourceTitles.size() + static_cast<size_t>(maxDepth) *
                                            kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget completed via resolved-distance cache: "
             "resolved={} elapsed={}ms",
             distances.size(), ElapsedMs(computeStartedAt));
    return distances;
  }

  // ターゲット起点のBFSキャッシュから引き継ぐ。同じゲーム内で同じターゲットを
  // 目指している間はページ移動のたびに incoming_links を再探索せずに済む
  // （ターゲットが変わっていればここでリセットされる）。
  std::vector<int> frontierBackward;
  std::unordered_map<int, int> backwardDepth;
  {
    auto &cache = GetBackwardBfsCache();
    std::lock_guard<std::mutex> cacheLock(cache.mutex);
    if (cache.targetId != targetId) {
      cache.targetId = targetId;
      cache.depthByPageId = {{targetId, 0}};
      cache.frontier = {targetId};
    }
    backwardDepth = cache.depthByPageId;
    frontierBackward = cache.frontier;
  }
  const size_t backwardCacheHitNodes = backwardDepth.size();
  const size_t distancesBeforeCacheHits = distances.size();

  // キャッシュに既に含まれているソースはDBに触れずその場で解決する。
  {
    const std::vector<int> unresolvedSnapshot(unresolvedPageIds.begin(),
                                              unresolvedPageIds.end());
    for (int srcId : unresolvedSnapshot) {
      if (auto it = backwardDepth.find(srcId); it != backwardDepth.end()) {
        resolveSource(srcId, it->second);
      }
    }
  }
  if (const size_t cachedHits = distances.size() - distancesBeforeCacheHits;
      cachedHits > 0) {
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget backward cache reused: cachedNodes={} "
             "immediateHits={}",
             backwardCacheHitNodes, cachedHits);
  }

  std::vector<int> frontierForward;
  frontierForward.reserve(unresolvedPageIds.size());
  std::unordered_map<int, ForwardNode> forwardInfo;
  forwardInfo.reserve(unresolvedPageIds.size() * 2);
  for (int srcId : unresolvedPageIds) {
    forwardInfo[srcId] = ForwardNode{srcId, 0};
    frontierForward.push_back(srcId);
  }

  for (int depth = 1; depth <= maxDepth && !unresolvedPageIds.empty() &&
                      !frontierForward.empty() && !frontierBackward.empty();
       ++depth) {
    if (isCancelled()) {
      LOG_WARN("WikiShortestPath",
               "ComputeDistancesToTarget cancelled before depth={}",
               depth);
      break;
    }

    const bool expandForward =
        frontierForward.size() <= frontierBackward.size();
    const auto &expandingFrontier =
        expandForward ? frontierForward : frontierBackward;
    const auto depthStartedAt = std::chrono::steady_clock::now();
    const size_t frontierBefore = expandingFrontier.size();
    const size_t unresolvedBefore = unresolvedPageIds.size();
    const size_t resolvedBefore = distances.size();
    const auto fetchStartedAt = std::chrono::steady_clock::now();
    const size_t depthBase =
        sourceTitles.size() +
        static_cast<size_t>(depth - 1) * kPathEvaluationDepthProgressUnits;

    std::unordered_map<int, std::vector<int>> linkMap;
    if (!FetchLinks(
            m_db, expandingFrontier,
            expandForward ? "outgoing_links" : "incoming_links", linkMap,
            [&](size_t processedChunks, size_t totalChunks) {
              if (totalChunks == 0) {
                return;
              }
              const size_t depthUnits =
                  (processedChunks * kPathEvaluationDepthProgressUnits) /
                  totalChunks;
              storeProgress(depthBase +
                            std::min(depthUnits,
                                     kPathEvaluationDepthProgressUnits));
            },
            cancelRequested)) {
      LOG_ERROR("WikiShortestPath",
                "ComputeDistancesToTarget BFS failed: depth={} side={} "
                "frontier={} elapsed={}ms",
                depth, expandForward ? "forward" : "backward", frontierBefore,
                ElapsedMs(depthStartedAt));
      return distances;
    }
    const long long fetchMs = ElapsedMs(fetchStartedAt);

    size_t traversedLinks = 0;
    bool cancelledDuringTraversal = false;
    size_t traversedEdges = 0;

    if (expandForward) {
      std::vector<int> next;
      for (int pageId : frontierForward) {
        if (isCancelled()) {
          cancelledDuringTraversal = true;
          break;
        }
        const auto parentIt = forwardInfo.find(pageId);
        const auto linkIt = linkMap.find(pageId);
        if (parentIt == forwardInfo.end() || linkIt == linkMap.end()) {
          continue;
        }
        const ForwardNode parent = parentIt->second;
        const int nextDepth = parent.depth + 1;
        traversedLinks += linkIt->second.size();
        for (int nb : linkIt->second) {
          if ((traversedEdges++ & 4095U) == 0U && isCancelled()) {
            cancelledDuringTraversal = true;
            break;
          }
          const auto [it, inserted] = forwardInfo.try_emplace(
              nb, ForwardNode{parent.originSourceId, nextDepth});
          if (inserted) {
            next.push_back(nb);
          }
          if (const auto bIt = backwardDepth.find(nb);
              bIt != backwardDepth.end()) {
            resolveSource(it->second.originSourceId,
                          it->second.depth + bIt->second);
          }
        }
        if (cancelledDuringTraversal) {
          break;
        }
      }
      frontierForward = std::move(next);
    } else {
      std::vector<int> next;
      for (int pageId : frontierBackward) {
        if (isCancelled()) {
          cancelledDuringTraversal = true;
          break;
        }
        const auto depthIt = backwardDepth.find(pageId);
        const auto linkIt = linkMap.find(pageId);
        if (depthIt == backwardDepth.end() || linkIt == linkMap.end()) {
          continue;
        }
        const int nextDepth = depthIt->second + 1;
        traversedLinks += linkIt->second.size();
        for (int nb : linkIt->second) {
          if ((traversedEdges++ & 4095U) == 0U && isCancelled()) {
            cancelledDuringTraversal = true;
            break;
          }
          const auto [it, inserted] = backwardDepth.try_emplace(nb, nextDepth);
          if (inserted) {
            next.push_back(nb);
          }
          resolveSource(nb, it->second);
          if (const auto fIt = forwardInfo.find(nb); fIt != forwardInfo.end()) {
            resolveSource(fIt->second.originSourceId,
                          fIt->second.depth + it->second);
          }
        }
        if (cancelledDuringTraversal) {
          break;
        }
      }
      frontierBackward = std::move(next);
    }

    if (cancelledDuringTraversal) {
      LOG_WARN("WikiShortestPath",
               "ComputeDistancesToTarget cancelled while traversing depth={}",
               depth);
      break;
    }

    storeProgress(sourceTitles.size() + static_cast<size_t>(depth) *
                                            kPathEvaluationDepthProgressUnits);
    LOG_INFO("WikiShortestPath",
             "ComputeDistancesToTarget depth={} side={} frontier={} rows={} "
             "links={} forwardFrontier={} backwardFrontier={} resolvedDelta={} "
             "resolved={} unresolvedDelta={} unresolved={} fetch={}ms elapsed={}ms",
             depth, expandForward ? "forward" : "backward", frontierBefore,
             linkMap.size(), traversedLinks, frontierForward.size(),
             frontierBackward.size(), distances.size() - resolvedBefore,
             distances.size(), unresolvedBefore - unresolvedPageIds.size(),
             unresolvedPageIds.size(), fetchMs, ElapsedMs(depthStartedAt));
  }

  storeProgress(sourceTitles.size() + static_cast<size_t>(maxDepth) *
                                          kPathEvaluationDepthProgressUnits);

  // backward側で新たに到達したノードをキャッシュへ書き戻す。次に同じ
  // ターゲットで呼ばれたときはこの続きから再開できる。ターゲットが
  // 既に切り替わっていれば（別ホールの呼び出しが割り込んでいれば）書き戻さない。
  if (backwardDepth.size() > backwardCacheHitNodes) {
    auto &cache = GetBackwardBfsCache();
    std::lock_guard<std::mutex> cacheLock(cache.mutex);
    if (cache.targetId == targetId &&
        backwardDepth.size() > cache.depthByPageId.size()) {
      cache.depthByPageId = backwardDepth;
      cache.frontier = frontierBackward;
    }
  }

  LOG_INFO("WikiShortestPath",
           "Computed target distances: sources={}, resolved={}, targetId={}, "
           "maxDepth={}, elapsed={}ms",
           sourceTitles.size(), distances.size(), targetId, maxDepth,
           ElapsedMs(computeStartedAt));
  return distances;
}

} // namespace game::systems

