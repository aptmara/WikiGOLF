/**
 * @file WikiPageLoader.cpp
 * @brief WikiPageLoader — 記事取得・テクスチャ生成・地形構築・ホール配置
 *
 * 入力: pageName、GameContext、各種システムポインタ（SetSystems で設定）
 * 変更: ECS エンティティ（ホール・地形）の作成／削除、GolfGameState の更新
 * 出力: PageLoadResult（フィールドサイズ・Par）、ECS への副作用
*/

// GraphicsDevice の完全定義を先に確保する（GameContext.h が前方宣言のみのため）
#include "../../graphics/GraphicsDevice.h"
#include "WikiPageLoader.h"
#include "HoleVisualRules.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../../core/Profiler.h"
#include "../../core/GameContext.h"
#include "../systems/PostProcessSystem.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/EnvironmentPresets.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../utils/ProceduralFlag.h"
#include "../controllers/MinimapController.h"
#include "../systems/ParticleSystem.h"
#include "../utils/ParRules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <utility>


// Windows マクロ対策
#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

// ============================================================
// 定数
// ============================================================
namespace {
constexpr auto kLongBuildStepLogInterval = std::chrono::seconds(2);
/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。
*/
long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    if (startedAt == std::chrono::steady_clock::time_point::min()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}

} // namespace

/**
 * @brief 生成済みページオブジェクトを破棄します。
*/
void WikiPageLoader::ClearGeneratedPageObjects(
    core::GameContext&              ctx,
    controllers::MinimapController* minimapController)
{
    GolfGameState* state = ctx.world.GetGlobal<GolfGameState>();

    m_pageEntityOwner.DestroyAll(ctx.world);

    std::vector<ecs::Entity> holesToDelete;
    std::vector<ecs::Entity> relatedToDelete;
    ctx.world.Query<GolfHole>().Each(
        [&](ecs::Entity e, GolfHole& hole) {
            holesToDelete.push_back(e);

            if (hole.labelEntity != 0 && hole.labelEntity != UINT32_MAX) {
                relatedToDelete.push_back(ecs::Entity(hole.labelEntity));
            }
            if (hole.pillarEntity != 0 && hole.pillarEntity != UINT32_MAX) {
                relatedToDelete.push_back(ecs::Entity(hole.pillarEntity));
            }
            if (hole.signboardEntity != 0 && hole.signboardEntity != UINT32_MAX) {
                relatedToDelete.push_back(ecs::Entity(hole.signboardEntity));
            }
        });

    for (auto e : relatedToDelete) {
        if (ctx.world.IsAlive(e)) {
            ctx.world.DestroyEntity(e);
        }
    }

    for (auto e : holesToDelete) {
        if (ctx.world.IsAlive(e)) {
            ctx.world.DestroyEntity(e);
        }
    }

    std::vector<ecs::Entity> flagsToDelete;
    ctx.world.Query<HoleFlag>().Each(
        [&](ecs::Entity e, HoleFlag&) { flagsToDelete.push_back(e); });
    for (auto e : flagsToDelete) {
        if (ctx.world.IsAlive(e)) {
            ctx.world.DestroyEntity(e);
        }
    }

    if (state) {
        state->holes.clear();
    }

    if (minimapController) {
        minimapController->ClearHoleIcons(ctx);
    }

    if (m_terrainSystem) {
        m_terrainSystem->Clear(ctx);
    }

    LOG_DEBUG("WikiPageLoader", "Generated page objects cleared");
}

// ============================================================
// 公開 API
// ============================================================

void WikiPageLoader::SetSystems(
    graphics::WikiTextureGenerator*   textureGen,
    game::systems::WikiTerrainSystem* terrainSys,
    graphics::SkyboxTextureGenerator* skyboxGen,
    game::systems::WikiShortestPath*  shortestPath)
{
    m_textureGenerator = textureGen;
    m_terrainSystem    = terrainSys;
    m_skyboxGenerator  = skyboxGen;
    m_shortestPath     = shortestPath;
}

void WikiPageLoader::SetTutorialMode(bool enabled)
{
    m_tutorialMode = enabled;
}

void WikiPageLoader::SetPreloadedData(std::vector<game::WikiLink> links,
                                      std::string                 extract,
                                      bool skipSupplementalFetch)
{
    m_pageDataFetcher.SetPreloadedData(std::move(links), std::move(extract),
                                      skipSupplementalFetch);
}

const char* WikiPageLoader::BuildStepName(BuildStep step)
{
    switch (step) {
    case BuildStep::None: return "None";
    case BuildStep::ClearOldHoles: return "ClearOldHoles";
    case BuildStep::PrepareLinks: return "PrepareLinks";
    case BuildStep::BeginTexture: return "BeginTexture";
    case BuildStep::GenerateTextureTiles: return "GenerateTextureTiles";
    case BuildStep::ApplySkybox: return "ApplySkybox";
    case BuildStep::BeginTerrain: return "BeginTerrain";
    case BuildStep::BuildTerrainStep: return "BuildTerrainStep";
    case BuildStep::RepositionBall: return "RepositionBall";
    case BuildStep::EvaluateHoles: return "EvaluateHoles";
    case BuildStep::EvaluateHolePaths: return "EvaluateHolePaths";
    case BuildStep::CreateMapIcons: return "CreateMapIcons";
    case BuildStep::CreateHoles: return "CreateHoles";
    case BuildStep::SetupWind: return "SetupWind";
    case BuildStep::Finish: return "Finish";
    default: return "Unknown";
    }
}

void WikiPageLoader::LogBuildStepTransition()
{
    if (m_loggedBuildStep == m_buildStep) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_loggedBuildStep != BuildStep::None) {
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} step={} finished elapsed={}ms total={}ms "
                 "progress={:.1f}%",
                 m_buildLoadId, BuildStepName(m_loggedBuildStep),
                 ElapsedMs(m_stepStartedAt), ElapsedMs(m_buildStartedAt),
                 m_buildProgress * 100.0f);
    }

    m_loggedBuildStep = m_buildStep;
    m_stepStartedAt = now;
    m_lastLongStepLogAt = now;

    if (m_buildStep != BuildStep::None) {
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} step={} started total={}ms progress={:.1f}%",
                 m_buildLoadId, BuildStepName(m_buildStep),
                 ElapsedMs(m_buildStartedAt), m_buildProgress * 100.0f);
    }
}

void WikiPageLoader::LogLongRunningBuildStep()
{
    if (m_loggedBuildStep == BuildStep::None) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastLongStepLogAt < kLongBuildStepLogInterval) {
        return;
    }

    LOG_INFO("WikiPageLoader",
             "BuildPage loadId={} step={} still-running elapsed={}ms total={}ms "
             "progress={:.1f}%",
             m_buildLoadId, BuildStepName(m_loggedBuildStep),
             ElapsedMs(m_stepStartedAt), ElapsedMs(m_buildStartedAt),
             m_buildProgress * 100.0f);
    m_lastLongStepLogAt = now;
}

void WikiPageLoader::CancelAsyncPathEvaluations()
{
    m_pathEvaluator.Cancel();
}

void WikiPageLoader::Shutdown(
    core::GameContext& ctx,
    controllers::MinimapController* minimapController)
{
    CancelAsyncPathEvaluations();
    ClearGeneratedPageObjects(ctx, minimapController);
    m_holeThumbnailCache.clear();
    m_activeThumbnailFetches = 0;
    m_wikiTexture.reset();
    m_textureState = graphics::WikiTextureGenerationState();
    m_buildData = PageDataAsyncResult();
    m_targetThumbnailSRV.Reset();
    m_hasTargetThumbnail = false;
}

bool WikiPageLoader::TryConsumePathEvaluation(core::GameContext& ctx,
                                              bool updateWorld)
{
    const auto partial = m_pathEvaluator.ConsumePartial();
    if (!partial.empty()) {
        ApplyPathEvaluationResults(partial);
        for (const auto& resolved : partial) {
            for (auto& candidate : m_buildHoleCandidates) {
                if (candidate.originalIndex == resolved.originalIndex) {
                    candidate.isPlayable = true;
                    break;
                }
            }
        }
        if (updateWorld) {
            ApplyPathEvaluationToWorld(ctx, partial);
            RefreshParFromPathEvaluation(ctx, false);
        }
        const char* updateWorldText = "false";
        if (updateWorld) {
            updateWorldText = "true";
        }
        LOG_DEBUG("WikiPageLoader",
                  "Path evaluation partial consumed: loadId={} count={} "
                  "updateWorld={}",
                  m_buildLoadId, partial.size(), updateWorldText);
    }

    auto completed = m_pathEvaluator.TryConsumeCompleted();
    if (!completed) {
        return false;
    }

    m_buildPathCandidates = std::move(*completed);
    ApplyPathEvaluationResults(m_buildPathCandidates);
    for (auto& candidate : m_buildHoleCandidates) {
        candidate.isPlayable = true;
    }
    if (updateWorld) {
        ApplyPathEvaluationToWorld(ctx, m_buildPathCandidates);
        RefreshParFromPathEvaluation(ctx, false);
    }
    const char* updateWorldText = "false";
    if (updateWorld) {
        updateWorldText = "true";
    }
    LOG_INFO("WikiPageLoader",
             "Path evaluation consumed: loadId={} evaluated={} "
             "updateWorld={}",
             m_buildLoadId, m_buildPathCandidates.size(), updateWorldText);
    return true;
}

bool WikiPageLoader::UpdateAsyncPathEvaluation(core::GameContext& ctx)
{
    return TryConsumePathEvaluation(ctx, true);
}

void WikiPageLoader::ApplyPathEvaluationToWorld(
    core::GameContext& ctx,
    const std::vector<HolePlacementCandidate>& evaluatedCandidates)
{
    std::unordered_map<std::string, int> hopsByTarget;
    hopsByTarget.reserve(evaluatedCandidates.size());
    for (const auto& candidate : evaluatedCandidates) {
        if (!candidate.linkTarget.empty()) {
            hopsByTarget[candidate.linkTarget] = candidate.hopsToTarget;
        }
    }
    if (hopsByTarget.empty()) {
        return;
    }

    std::unordered_map<uint32_t, int> updatedHoles;
    ctx.world.Query<Transform, GolfHole>().Each(
        [&](ecs::Entity e, Transform&, GolfHole& hole) {
            const auto it = hopsByTarget.find(hole.linkTarget);
            if (it == hopsByTarget.end()) {
                return;
            }

            hole.hopsToTarget = it->second;
            if (auto* mr = ctx.world.Get<MeshRenderer>(e)) {
                mr->color = HoleVisualRules::GetBodyColor(
                    hole.isTarget, hole.hopsToTarget);
            }
            if (hole.labelEntity != 0 &&
                ctx.world.IsAlive(static_cast<ecs::Entity>(hole.labelEntity))) {
                if (auto* label = ctx.world.Get<UIText>(
                        static_cast<ecs::Entity>(hole.labelEntity))) {
                    label->style.color =
                        HoleVisualRules::GetColor(hole.isTarget,
                                                 hole.hopsToTarget);
                }
            }
            updatedHoles[static_cast<uint32_t>(e)] = hole.hopsToTarget;
            if (m_buildMinimap) {
                m_buildMinimap->UpdateHoleIconEvaluation(
                    hole.linkTarget, true, hole.hopsToTarget);
            }
        });

    ctx.world.Query<HoleFlag>().Each([&](ecs::Entity e, HoleFlag& flag) {
        const auto it = updatedHoles.find(flag.holeEntity);
        if (it == updatedHoles.end()) {
            return;
        }

        if (auto* hole = ctx.world.Get<GolfHole>(
                static_cast<ecs::Entity>(flag.holeEntity))) {
            if (auto* mr = ctx.world.Get<MeshRenderer>(e)) {
                mr->color = HoleVisualRules::GetColor(hole->isTarget,
                                                      it->second);
            }
        }
    });

    LOG_DEBUG("WikiPageLoader",
             "Path evaluation applied to world: loadId={} holes={}",
             m_buildLoadId, updatedHoles.size());
}

// ============================================================
// LoadPage
// ============================================================
PageLoadResult WikiPageLoader::LoadPage(
    core::GameContext&              ctx,
    const std::string&              pageName,
    ecs::Entity                     ballEntity,
    ecs::Entity                     cameraEntity,
    ecs::Entity                     skyboxEntity,
    controllers::MinimapController* minimapController)
{
    const auto loadStartedAt = std::chrono::steady_clock::now();
    const uint64_t loadId = s_nextBuildLoadId.fetch_add(1, std::memory_order_relaxed);
    LOG_INFO("WikiPageLoader", "LoadPage sync started loadId={} page='{}'",
             loadId, pageName);
    auto asyncData = FetchPageDataAsync(pageName);
    auto result = BuildPageSync(ctx, std::move(asyncData), ballEntity,
                                cameraEntity, skyboxEntity, minimapController);
    const char* successText = "false";
    if (result.success) {
        successText = "true";
    }
    LOG_INFO("WikiPageLoader",
             "LoadPage sync finished loadId={} page='{}' success={} elapsed={}ms",
             loadId, pageName, successText, ElapsedMs(loadStartedAt));
    return result;
}

PageDataAsyncResult WikiPageLoader::FetchPageDataAsync(const std::string& pageName) {
    return m_pageDataFetcher.Fetch(pageName);
}

} // namespace game::scenes
