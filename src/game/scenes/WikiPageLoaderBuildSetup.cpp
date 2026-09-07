/**
 * @file WikiPageLoaderBuildSetup.cpp
 * @brief 段階構築の初期化とフレーム予算処理を実装します。
 */

#include "WikiPageLoader.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/WikiComponents.h"
#include <chrono>

namespace game::scenes {

using namespace game::components;

/**
 * @brief インクリメンタルな構築を開始する
 */
void WikiPageLoader::BeginBuildPage(core::GameContext& ctx,
                                    PageDataAsyncResult asyncData,
                                    ecs::Entity ballEntity,
                                    ecs::Entity cameraEntity,
                                    ecs::Entity skyboxEntity,
                                    controllers::MinimapController* minimapController)
{
    m_buildLoadId = s_nextBuildLoadId.fetch_add(1, std::memory_order_relaxed);
    m_buildStartedAt = std::chrono::steady_clock::now();
    m_stepStartedAt = std::chrono::steady_clock::time_point::min();
    m_lastLongStepLogAt = std::chrono::steady_clock::time_point::min();
    m_lastCreateHolesProgressLogAt = std::chrono::steady_clock::time_point::min();
    m_buildStep = BuildStep::ClearOldHoles;
    m_loggedBuildStep = BuildStep::None;
    m_buildData = std::move(asyncData);
    m_pathHopCache.clear();
    m_buildBall = ballEntity;
    m_buildCamera = cameraEntity;
    m_buildSkybox = skyboxEntity;
    m_buildMinimap = minimapController;
    m_buildProgress = 0.0f;
    m_buildResult = PageLoadResult();
    m_buildHoleCandidates.clear();
    m_buildPathCandidates.clear();
    m_buildMapHoleCandidates.clear();
    m_buildGameplayLinks.clear();
    m_pathHopCache.clear();
    m_pathEvaluator.ResetForNewLoad(m_buildLoadId);
    m_nextHoleIndex = 0;
    m_nextMapIconIndex = 0;
    LOG_INFO("WikiPageLoader",
             "BeginBuildPage loadId={} page='{}' links={} extractBytes={} "
             "categories={}",
             m_buildLoadId, m_buildData.pageName, m_buildData.allLinks.size(),
             m_buildData.articleText.size(), m_buildData.pageCategories.size());
}

/**
 * @brief 構築を 1 ステップ進める
 */
bool WikiPageLoader::StepBuildPageWithinFrameBudget(
    core::GameContext& ctx, std::chrono::milliseconds budget)
{
    m_buildDeadline = std::chrono::steady_clock::now() + budget;
    if (m_buildStep != BuildStep::EvaluateHolePaths) {
        TryConsumePathEvaluation(ctx, true);
    }

    bool done = false;
    while (std::chrono::steady_clock::now() < m_buildDeadline) {
        const float progressBefore = m_buildProgress;
        done = StepBuildPage(ctx);
        if (done) {
            break;
        }

        if (m_buildProgress <= progressBefore + 0.0001f) {
            break;
        }
    }

    m_buildDeadline = std::chrono::steady_clock::time_point::max();
    LogLongRunningBuildStep();
    return done;
}


} // namespace game::scenes

