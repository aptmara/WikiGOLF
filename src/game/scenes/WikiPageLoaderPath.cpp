/**
 * @file WikiPageLoaderPath.cpp
 * @brief ホール候補の経路評価結果反映を実装します。
*/

#include "WikiPageLoader.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/WikiComponents.h"
#include "../utils/ParRules.h"
#include <algorithm>
#include <chrono>

namespace game::scenes {

using namespace game::components;

namespace {
constexpr int kPathEvaluationMaxDepth = 4;
}

/**
 * @brief 経路評価結果を全ホール候補とマップ候補へ反映します。
*/
void WikiPageLoader::ApplyPathEvaluationResults(
    const std::vector<HolePlacementCandidate>& evaluatedCandidates)
{
    for (const auto& evaluated : evaluatedCandidates) {
        auto applyResult = [&](HolePlacementCandidate& candidate) {
            if (candidate.originalIndex == evaluated.originalIndex) {
                candidate.hopsToTarget = evaluated.hopsToTarget;
                candidate.isPlayable = true;
                return true;
            }
            return false;
        };

        for (auto& candidate : m_buildHoleCandidates) {
            if (applyResult(candidate)) {
                break;
            }
        }

        for (auto& candidate : m_buildMapHoleCandidates) {
            if (applyResult(candidate)) {
                break;
            }
        }
    }
}

int WikiPageLoader::FindMinResolvedHopsToTarget() const
{
    int minResolvedHops = -1;
    for (const auto& candidate : m_buildPathCandidates) {
        int hops = candidate.hopsToTarget;
        if (candidate.isTarget) {
            hops = 0;
        }
        if (hops < 0) {
            continue;
        }
        if (minResolvedHops < 0) {
            minResolvedHops = hops;
        } else {
            minResolvedHops = std::min(minResolvedHops, hops);
        }
    }
    return minResolvedHops;
}

void WikiPageLoader::RefreshParFromPathEvaluation(core::GameContext& ctx,
                                                  bool allowFallback)
{
    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) {
        return;
    }

    const int minResolvedHops = FindMinResolvedHopsToTarget();
    if (minResolvedHops < 0 && !allowFallback) {
        return;
    }

    const int par = game::utils::CalculateWikiGolfPar(
        minResolvedHops, m_buildValidLinks.size());
    if (state->par != par) {
        LOG_INFO("WikiPageLoader",
                 "Par refreshed: loadId={} old={} new={} minHops={} links={}",
                 m_buildLoadId, state->par, par, minResolvedHops,
                 m_buildValidLinks.size());
    }
    state->par = par;
    m_buildResult.calculatedPar = -1;
    if (minResolvedHops >= 0) {
        m_buildResult.calculatedPar = std::max(1, minResolvedHops + 1);
    }
}

/**
 * @brief 候補のリンク距離をページ内キャッシュ付きで評価します。
*/
void WikiPageLoader::EvaluateCandidatePath(core::GameContext& ctx,
                                           HolePlacementCandidate& candidate)
{
    const auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!m_shortestPath || !m_shortestPath->IsAvailable() || !state ||
        state->targetPageId == -1 || candidate.linkTarget.empty()) {
        return;
    }

    const std::string cacheKey =
        candidate.linkTarget + "\x1f" + std::to_string(state->targetPageId);
    if (auto it = m_pathHopCache.find(cacheKey); it != m_pathHopCache.end()) {
        candidate.hopsToTarget = it->second;
        return;
    }

    const auto startedAt = std::chrono::steady_clock::now();
    auto r = m_shortestPath->FindShortestPath(
        candidate.linkTarget, state->targetPageId, kPathEvaluationMaxDepth);
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startedAt).count();
    if (elapsedMs > 100) {
        LOG_WARN("WikiPageLoader",
                 "Path candidate evaluation took {} ms: target='{}'",
                 elapsedMs, candidate.linkTarget);
    }

    candidate.hopsToTarget = -1;
    if (r.success) {
        candidate.hopsToTarget = r.degrees;
    }
    m_pathHopCache[cacheKey] = candidate.hopsToTarget;
}

/**
 * @brief マップ候補がプレイ可能ホールとして選ばれたかを判定します。
*/
bool WikiPageLoader::IsPlayableCandidate(
    const HolePlacementCandidate& candidate) const
{
    return std::any_of(
        m_buildHoleCandidates.begin(), m_buildHoleCandidates.end(),
        [&](const HolePlacementCandidate& playable) {
            return playable.originalIndex == candidate.originalIndex;
        });
}


} // namespace game::scenes
