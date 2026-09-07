/**
 * @file WikiPageLoaderLinkPlacement.cpp
 * @brief テクスチャリンクからホール候補を配置します。
*/

#include "WikiPageLoader.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../controllers/MinimapController.h"

namespace game::scenes {

/**
 * @brief テクスチャのリンク領域からホールを一括配置する
*/
void WikiPageLoader::CreateLinksFromTexture(core::GameContext& ctx)
{
    if (!m_wikiTexture) return;

    const auto* gameplayLinks = &m_wikiTexture->links;
    if (!m_buildGameplayLinks.empty()) {
        gameplayLinks = &m_buildGameplayLinks;
    }
    std::vector<HolePlacementCandidate> candidates;
    candidates.reserve(gameplayLinks->size());
    for (size_t i = 0; i < gameplayLinks->size(); ++i) {
        candidates.push_back(
            m_holePlacementPlanner.BuildCandidate(
                (*gameplayLinks)[i], i, m_wikiTexture->width,
                m_wikiTexture->height, m_fieldWidth, m_fieldDepth));
    }

    m_buildMapHoleCandidates =
        m_holePlacementPlanner.SelectMapCandidates(candidates);
    m_buildHoleCandidates = candidates;
    m_buildPathCandidates = m_buildHoleCandidates;
    for (auto& candidate : m_buildPathCandidates) {
        EvaluateCandidatePath(ctx, candidate);
    }
    ApplyPathEvaluationResults(m_buildPathCandidates);
    for (auto& candidate : m_buildHoleCandidates) {
        candidate.isPlayable = true;
    }

    if (m_buildMinimap) {
        for (const auto& candidate : m_buildMapHoleCandidates) {
            m_buildMinimap->AddHoleIcon(ctx, candidate.x, candidate.z,
                                        candidate.linkTarget,
                                        candidate.isTarget,
                                        IsPlayableCandidate(candidate),
                                        candidate.hopsToTarget);
        }
    }

    for (const auto& candidate : m_buildHoleCandidates) {
        CreateHole(ctx, candidate.x, candidate.z, candidate.linkTarget,
                   candidate.isTarget, candidate.hopsToTarget, false);
    }

    LOG_INFO("WikiPageLoader", "Total holes created: {}", m_buildHoleCandidates.size());
}

} // namespace game::scenes

