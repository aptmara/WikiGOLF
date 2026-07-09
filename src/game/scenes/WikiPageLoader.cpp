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
#include "../../core/Profiler.h"
#include "../../core/GameContext.h"
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
#include "../systems/WikiClient.h"
#include "../utils/ParRules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
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
constexpr float kFieldScale     = 4.0f;
constexpr float kMinFieldWidth  = 20.0f * kFieldScale;
constexpr float kMinFieldDepth  = 30.0f * kFieldScale;
constexpr float kMaxSafeDepth   = 20000.0f;
constexpr float kMaxSafeWidth   = 20000.0f;
constexpr float kMinLinkHoleDistance = 4.0f;
constexpr float kMinMapIconDistance = 8.0f;
constexpr size_t kMaxMapHoleIcons = 160;
constexpr int kPathEvaluationMaxDepth = 4;
constexpr int kLoadPathEvaluationMaxDepth = 2;
constexpr size_t kPathEvaluationDepthProgressUnits = 100;
constexpr auto kLongBuildStepLogInterval = std::chrono::seconds(2);
constexpr float kTutorialFieldWidth = 96.0f;
constexpr float kTutorialFieldDepth = 144.0f;

struct TutorialLinkPlacement {
    const char* page;
    float worldX;
    float worldZ;
};

constexpr TutorialLinkPlacement kTutorialLinkPlacements[] = {
    {"フェアウェイ", 0.0f, -28.0f},
    {"ラフ", -30.0f, -12.0f},
    {"バンカー", 24.0f, -3.0f},
    {"ウォーターハザード", -24.0f, 20.0f},
    {"グリーン", 12.0f, 42.0f},
    {"ゴール", 0.0f, 56.0f},
};

/**
 * @brief チュートリアル固定コースを使うページか判定します。 山内陽
 */
bool IsTutorialPresetPage(const std::string& pageName) {
    return pageName == "チュートリアル";
}

/**
 * @brief ワールド座標をテクスチャ上のリンク矩形へ変換します。 山内陽
 */
void PlaceLinkAtWorld(graphics::LinkRegion& link, const graphics::WikiTextureResult& texture,
                      float fieldWidth, float fieldDepth, float worldX, float worldZ) {
    if (texture.width == 0 || texture.height == 0) {
        return;
    }

    const float cx = (worldX / fieldWidth + 0.5f) * static_cast<float>(texture.width);
    const float cy = (0.5f - worldZ / fieldDepth) * static_cast<float>(texture.height);
    const float maxCenteredWidth =
        std::max(1.0f, std::min(cx, static_cast<float>(texture.width) - cx) * 2.0f);
    const float maxCenteredHeight =
        std::max(1.0f, std::min(cy, static_cast<float>(texture.height) - cy) * 2.0f);
    const float linkWidth =
        std::min(std::min(260.0f, static_cast<float>(texture.width) * 0.18f),
                 maxCenteredWidth);
    const float linkHeight = std::min(72.0f, maxCenteredHeight);
    link.width = linkWidth;
    link.height = linkHeight;
    link.x = std::clamp(cx - link.width * 0.5f, 0.0f,
                        std::max(0.0f, static_cast<float>(texture.width) - link.width));
    link.y = std::clamp(cy - link.height * 0.5f, 0.0f,
                        std::max(0.0f, static_cast<float>(texture.height) - link.height));
}

/**
 * @brief チュートリアル用にホール配置だけを教材コースへ固定します。 山内陽
 */
std::vector<graphics::LinkRegion>
BuildTutorialPresetGameplayLinks(const graphics::WikiTextureResult& texture,
                                 const std::string& targetPage) {
    std::vector<graphics::LinkRegion> links;
    links.reserve(std::size(kTutorialLinkPlacements));
    for (const auto& placement : kTutorialLinkPlacements) {
        graphics::LinkRegion link;
        if (const auto sourceLink = std::find_if(
                texture.links.begin(), texture.links.end(),
                [&placement](const graphics::LinkRegion& candidate) {
                    return candidate.targetPage == placement.page;
                });
            sourceLink != texture.links.end()) {
            link = *sourceLink;
        } else {
            link.targetPage = placement.page;
        }

        link.isTarget = (link.targetPage == targetPage);
        PlaceLinkAtWorld(link, texture, kTutorialFieldWidth,
                         kTutorialFieldDepth, placement.worldX,
                         placement.worldZ);
        links.push_back(link);
    }
    return links;
}

/**
 * @brief 開始時刻からの経過時間をミリ秒で返します。 山内陽
 */
long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    if (startedAt == std::chrono::steady_clock::time_point::min()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}

/**
 * @brief ホップ数に応じたホール色を返します。 山内陽
 */
XMFLOAT4 GetHoleColor(bool isTargetHole, int hopsToTarget) {
    if (isTargetHole) {
        return {1.0f, 0.2f, 0.2f, 1.0f};
    }
    if (hopsToTarget == 1) {
        return {1.0f, 0.85f, 0.0f, 1.0f};
    }
    if (hopsToTarget == 2) {
        return {1.0f, 0.6f, 0.2f, 1.0f};
    }
    if (hopsToTarget >= 3 && hopsToTarget <= 5) {
        return {0.95f, 0.95f, 0.95f, 1.0f};
    }
    if (hopsToTarget > 5) {
        return {0.6f, 0.6f, 0.6f, 1.0f};
    }
    return {0.25f, 0.65f, 1.0f, 1.0f};
}

/**
 * @brief ホール本体用に少し暗くした色を返します。 山内陽
 */
XMFLOAT4 GetHoleBodyColor(bool isTargetHole, int hopsToTarget) {
    XMFLOAT4 color = GetHoleColor(isTargetHole, hopsToTarget);
    color.x *= 0.45f;
    color.y *= 0.45f;
    color.z *= 0.45f;
    color.w = 1.0f;
    return color;
}
} // namespace

/**
 * @brief 生成済みページオブジェクトを破棄します。 山内陽
 */
void WikiPageLoader::ClearGeneratedPageObjects(
    core::GameContext&              ctx,
    controllers::MinimapController* minimapController)
{
    GolfGameState* state = ctx.world.GetGlobal<GolfGameState>();

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
                                      std::string                 extract)
{
    m_preloadedLinks   = std::move(links);
    m_preloadedExtract = std::move(extract);
    m_hasPreloadedData = true;
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

void WikiPageLoader::RetireActivePathEvaluationTask()
{
    if (!m_pathEvaluationTask.valid()) {
        return;
    }

    if (m_pathEvaluationTask.wait_for(std::chrono::milliseconds(0)) ==
        std::future_status::ready) {
        (void)m_pathEvaluationTask.get();
        return;
    }

    LOG_WARN("WikiPageLoader",
             "Path evaluation task retired without blocking: loadId={}",
             m_buildLoadId);
    m_retiredPathEvaluationTasks.push_back(std::move(m_pathEvaluationTask));
}

void WikiPageLoader::StartAsyncPathEvaluation(int targetPageId, int maxDepth)
{
    if (m_pathEvaluationStarted) {
        return;
    }

    m_pathEvaluationStarted = true;
    m_nextPathIndex = 0;
    m_pathEvaluationProgress = std::make_shared<std::atomic<size_t>>(0);
    m_pathEvaluationTotal = std::make_shared<std::atomic<size_t>>(
        m_buildPathCandidates.size() +
        std::max(0, maxDepth) * kPathEvaluationDepthProgressUnits);
    m_pathEvaluationPartialMutex = std::make_shared<std::mutex>();
    m_pathEvaluationPartialResults =
        std::make_shared<std::vector<HolePlacementCandidate>>();
    m_pathEvaluationConsumedResults = 0;

    const uint64_t loadId = m_buildLoadId;
    auto candidates = m_buildPathCandidates;
    std::unordered_map<std::string, std::vector<HolePlacementCandidate>>
        candidatesByTarget;
    for (const auto& candidate : candidates) {
        if (!candidate.linkTarget.empty()) {
            candidatesByTarget[candidate.linkTarget].push_back(candidate);
        }
    }
    auto progress = m_pathEvaluationProgress;
    auto totalUnits = m_pathEvaluationTotal;
    auto partialMutex = m_pathEvaluationPartialMutex;
    auto partialResults = m_pathEvaluationPartialResults;
    m_pathEvaluationTask = std::async(
        std::launch::async,
        [candidates = std::move(candidates), targetPageId, progress,
         totalUnits, loadId, maxDepth, partialMutex,
         partialResults,
         candidatesByTarget = std::move(candidatesByTarget)]() mutable {
            const auto taskStartedAt = std::chrono::steady_clock::now();
            LOG_INFO("WikiPageLoader",
                     "Path evaluation task started: loadId={} candidates={} targetId={} maxDepth={}",
                     loadId, candidates.size(), targetPageId, maxDepth);
            if (targetPageId == -1 || candidates.empty()) {
                progress->store(totalUnits->load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
                LOG_WARN("WikiPageLoader",
                         "Path evaluation task skipped: loadId={} candidates={} targetId={} maxDepth={}",
                         loadId, candidates.size(), targetPageId, maxDepth);
                return candidates;
            }

            game::systems::WikiShortestPath pathSystem;
            const auto dbStartedAt = std::chrono::steady_clock::now();
            if (!pathSystem.Initialize("Assets/data/jawiki_sdow-001.sqlite",
                                       false)) {
                progress->store(totalUnits->load(std::memory_order_relaxed),
                                std::memory_order_relaxed);
                LOG_ERROR("WikiPageLoader",
                          "Path evaluation DB initialize failed: loadId={} elapsed={}ms",
                          loadId, ElapsedMs(dbStartedAt));
                return candidates;
            }
            LOG_INFO("WikiPageLoader",
                     "Path evaluation DB initialized: loadId={} elapsed={}ms",
                     loadId, ElapsedMs(dbStartedAt));

            std::vector<std::string> linkTargets;
            std::unordered_set<std::string> seenTargets;
            linkTargets.reserve(candidates.size());
            size_t emptyTargets = 0;
            size_t duplicateTargets = 0;
            for (size_t i = 0; i < candidates.size(); ++i) {
                const auto& candidate = candidates[i];
                if (candidate.linkTarget.empty()) {
                    ++emptyTargets;
                    progress->store(i + 1, std::memory_order_relaxed);
                    continue;
                }
                if (!seenTargets.insert(candidate.linkTarget).second) {
                    ++duplicateTargets;
                    progress->store(i + 1, std::memory_order_relaxed);
                    continue;
                }
                linkTargets.push_back(candidate.linkTarget);
                progress->store(i + 1, std::memory_order_relaxed);
            }

            totalUnits->store(
                candidates.size() + linkTargets.size() +
                    std::max(0, maxDepth) * kPathEvaluationDepthProgressUnits,
                std::memory_order_relaxed);
            LOG_INFO("WikiPageLoader",
                     "Path evaluation targets prepared: loadId={} candidates={} unique={} "
                     "empty={} duplicates={} totalUnits={}",
                     loadId, candidates.size(), linkTargets.size(), emptyTargets,
                     duplicateTargets,
                     totalUnits->load(std::memory_order_relaxed));
            const auto distanceStartedAt = std::chrono::steady_clock::now();
            const auto distances = pathSystem.ComputeDistancesToTarget(
                linkTargets, targetPageId, maxDepth, progress.get(),
                candidates.size(),
                [&](const std::string& resolvedTitle, int hopsToTarget) {
                    if (!partialMutex || !partialResults) {
                        return;
                    }

                    const auto targetIt = candidatesByTarget.find(resolvedTitle);
                    if (targetIt == candidatesByTarget.end()) {
                        return;
                    }

                    std::lock_guard<std::mutex> lock(*partialMutex);
                    for (const auto& candidate : targetIt->second) {
                        auto resolved = candidate;
                        resolved.hopsToTarget = hopsToTarget;
                        resolved.isPlayable = true;
                        partialResults->push_back(std::move(resolved));
                    }
                });
            LOG_INFO("WikiPageLoader",
                     "Path evaluation distance map ready: loadId={} uniqueTargets={} "
                     "resolved={} maxDepth={} elapsed={}ms",
                     loadId, linkTargets.size(), distances.size(), maxDepth,
                     ElapsedMs(distanceStartedAt));
            size_t assignedDistances = 0;
            for (auto& candidate : candidates) {
                const auto distance = distances.find(candidate.linkTarget);
                candidate.hopsToTarget =
                    distance != distances.end() ? distance->second : -1;
                if (distance != distances.end()) {
                    ++assignedDistances;
                }
            }
            progress->store(totalUnits->load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
            LOG_INFO("WikiPageLoader",
                     "Path evaluation task finished: loadId={} candidates={} assigned={} "
                     "unresolved={} elapsed={}ms",
                     loadId, candidates.size(), assignedDistances,
                     candidates.size() - assignedDistances,
                     ElapsedMs(taskStartedAt));
            return candidates;
        });

    LOG_INFO("WikiPageLoader",
             "Path evaluation launched in background: loadId={} targets={} maxDepth={}",
             m_buildLoadId, m_buildPathCandidates.size(), maxDepth);
}

bool WikiPageLoader::TryConsumePathEvaluation(core::GameContext& ctx,
                                              bool updateWorld)
{
    ConsumePartialPathEvaluation(ctx, updateWorld);

    for (auto it = m_retiredPathEvaluationTasks.begin();
         it != m_retiredPathEvaluationTasks.end();) {
        if (it->valid() &&
            it->wait_for(std::chrono::milliseconds(0)) ==
                std::future_status::ready) {
            (void)it->get();
            it = m_retiredPathEvaluationTasks.erase(it);
        } else {
            ++it;
        }
    }

    if (!m_pathEvaluationTask.valid() ||
        m_pathEvaluationTask.wait_for(std::chrono::milliseconds(0)) !=
            std::future_status::ready) {
        return false;
    }

    m_buildPathCandidates = m_pathEvaluationTask.get();
    ApplyPathEvaluationResults(m_buildPathCandidates);
    for (auto& candidate : m_buildHoleCandidates) {
        candidate.isPlayable = true;
    }
    if (updateWorld) {
        ApplyPathEvaluationToWorld(ctx, m_buildPathCandidates);
        RefreshParFromPathEvaluation(ctx, false);
    }
    LOG_INFO("WikiPageLoader",
             "Path evaluation consumed: loadId={} evaluated={} updateWorld={}",
             m_buildLoadId, m_buildPathCandidates.size(),
             updateWorld ? "true" : "false");
    return true;
}

bool WikiPageLoader::UpdateAsyncPathEvaluation(core::GameContext& ctx)
{
    return TryConsumePathEvaluation(ctx, true);
}

size_t WikiPageLoader::ConsumePartialPathEvaluation(core::GameContext& ctx,
                                                    bool updateWorld)
{
    if (!m_pathEvaluationPartialMutex || !m_pathEvaluationPartialResults) {
        return 0;
    }

    std::vector<HolePlacementCandidate> partial;
    {
        std::lock_guard<std::mutex> lock(*m_pathEvaluationPartialMutex);
        if (m_pathEvaluationConsumedResults >=
            m_pathEvaluationPartialResults->size()) {
            return 0;
        }

        partial.assign(
            m_pathEvaluationPartialResults->begin() +
                static_cast<std::vector<HolePlacementCandidate>::difference_type>(
                    m_pathEvaluationConsumedResults),
            m_pathEvaluationPartialResults->end());
        m_pathEvaluationConsumedResults = m_pathEvaluationPartialResults->size();
    }

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
    LOG_DEBUG("WikiPageLoader",
             "Path evaluation partial consumed: loadId={} count={} updateWorld={}",
             m_buildLoadId, partial.size(), updateWorld ? "true" : "false");
    return partial.size();
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
                mr->color = GetHoleBodyColor(hole.isTarget, hole.hopsToTarget);
            }
            if (hole.labelEntity != 0 &&
                ctx.world.IsAlive(static_cast<ecs::Entity>(hole.labelEntity))) {
                if (auto* label = ctx.world.Get<UIText>(
                        static_cast<ecs::Entity>(hole.labelEntity))) {
                    label->style.color =
                        GetHoleColor(hole.isTarget, hole.hopsToTarget);
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
                mr->color = GetHoleColor(hole->isTarget, it->second);
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
    LOG_INFO("WikiPageLoader",
             "LoadPage sync finished loadId={} page='{}' success={} elapsed={}ms",
             loadId, pageName, result.success ? "true" : "false",
             ElapsedMs(loadStartedAt));
    return result;
}

PageDataAsyncResult WikiPageLoader::FetchPageDataAsync(const std::string& pageName) {
    const auto loadStartedAt = std::chrono::steady_clock::now();
    PageDataAsyncResult res;
    res.pageName = pageName;
    game::systems::WikiClient wikiClient;

    if (m_hasPreloadedData) {
        LOG_INFO("WikiPageLoader", "Using preloaded data for async: {}", pageName);
        res.allLinks    = std::move(m_preloadedLinks);
        res.articleText = std::move(m_preloadedExtract);
        m_hasPreloadedData = false;
        const auto categoryStartedAt = std::chrono::steady_clock::now();
        res.pageCategories = wikiClient.FetchPageCategories(pageName);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData category fetch page='{}' categories={} elapsed={}ms",
                 pageName, res.pageCategories.size(), ElapsedMs(categoryStartedAt));
    } else {
        LOG_INFO("WikiPageLoader", "Fetching live data async for: {}", pageName);
        const auto linksStartedAt = std::chrono::steady_clock::now();
        res.allLinks    = wikiClient.FetchPageLinks(pageName, 0);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData links page='{}' count={} elapsed={}ms",
                 pageName, res.allLinks.size(), ElapsedMs(linksStartedAt));
        const auto extractStartedAt = std::chrono::steady_clock::now();
        res.articleText = wikiClient.FetchPageExtract(pageName, 5000);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData extract page='{}' bytes={} elapsed={}ms",
                 pageName, res.articleText.size(), ElapsedMs(extractStartedAt));
        const auto categoryStartedAt = std::chrono::steady_clock::now();
        res.pageCategories = wikiClient.FetchPageCategories(pageName);
        LOG_INFO("WikiPageLoader",
                 "FetchPageData category fetch page='{}' categories={} elapsed={}ms",
                 pageName, res.pageCategories.size(), ElapsedMs(categoryStartedAt));
    }
    res.hasData = true;
    LOG_INFO("WikiPageLoader",
             "FetchPageData finished page='{}' links={} extractBytes={} "
             "categories={} elapsed={}ms",
             pageName, res.allLinks.size(), res.articleText.size(),
             res.pageCategories.size(), ElapsedMs(loadStartedAt));
    return res;
}

PageLoadResult WikiPageLoader::BuildPageSync(
    core::GameContext&              ctx,
    PageDataAsyncResult             asyncData,
    ecs::Entity                     ballEntity,
    ecs::Entity                     cameraEntity,
    ecs::Entity                     skyboxEntity,
    controllers::MinimapController* minimapController)
{
    const auto buildStartedAt = std::chrono::steady_clock::now();
    PageLoadResult result;
    const std::string& pageName = asyncData.pageName;
    m_buildMinimap = minimapController;
    m_pathHopCache.clear();

    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) {
        LOG_ERROR("WikiPageLoader", "BuildPageSync: GameState not found!");
        return result;
    }

    LOG_INFO("WikiPageLoader",
             "BuildPageSync started page='{}' links={} extractBytes={} "
             "categories={}",
             pageName, asyncData.allLinks.size(), asyncData.articleText.size(),
             asyncData.pageCategories.size());

    const auto clearStartedAt = std::chrono::steady_clock::now();
    ClearGeneratedPageObjects(ctx, minimapController);
    LOG_INFO("WikiPageLoader", "BuildPageSync clear old objects elapsed={}ms",
             ElapsedMs(clearStartedAt));
    m_pathHopCache.clear();

    // 記事の情報を取得します。
    std::vector<game::WikiLink> allLinks = std::move(asyncData.allLinks);
    std::string articleText = std::move(asyncData.articleText);
    std::vector<std::string> pageCategories = std::move(asyncData.pageCategories);


    // 関連性の高いリンクのみを抽出します。
    // 年・月・日・数値のみを除外
    auto isIgnored = [](const std::string& t) {
        if (t.empty()) return true;
        if (t.size() >= 3) {
            std::string suffix = t.substr(t.size() - 3);
            if (suffix == "年" || suffix == "月" || suffix == "日") return true;
        }
        if (std::all_of(t.begin(), t.end(),
                        [](unsigned char c) { return std::isdigit(c); }))
            return true;
        return false;
    };

    std::vector<std::pair<std::string, std::wstring>> validLinks;
    std::vector<std::tuple<size_t, std::string, std::wstring>> articleLinks;
    for (const auto& link : allLinks) {
        if (isIgnored(link.title)) continue;
        if (link.title == state->targetPage) continue; // ターゲットは後で追加
        const size_t pos = articleText.find(link.title);
        if (pos != std::string::npos) {
            articleLinks.push_back({pos, link.title, core::ToWString(link.title)});
        }
    }
    std::sort(articleLinks.begin(), articleLinks.end(),
              [](const auto& a, const auto& b) {
                  return std::get<0>(a) < std::get<0>(b);
              });
    for (const auto& link : articleLinks) {
        validLinks.push_back({std::get<1>(link), std::get<2>(link)});
    }

    // ターゲットページが本文に含まれていれば無条件追加
    if (!state->targetPage.empty() &&
        articleText.find(state->targetPage) != std::string::npos) {
        bool exists = std::any_of(validLinks.begin(), validLinks.end(),
            [&](const auto& v) { return v.first == state->targetPage; });
        if (!exists) {
            validLinks.push_back({state->targetPage,
                                  core::ToWString(state->targetPage)});
            LOG_INFO("WikiPageLoader", "Target '{}' added to links",
                     state->targetPage);
        }
    }

    // リンク不足時の補充（最低 3 件 → 最大 5 件）
    if (validLinks.size() < 3) {
        for (const auto& link : allLinks) {
            bool exists = std::any_of(validLinks.begin(), validLinks.end(),
                [&](const auto& v) { return v.first == link.title; });
            if (!exists && !isIgnored(link.title)) {
                validLinks.push_back({link.title, core::ToWString(link.title)});
                if (validLinks.size() >= 5) break;
            }
        }
    }

    // 記事本文の長さに応じてフィールドサイズを計算します。
    float articleLengthFactor =
        std::max(1.0f, (float)articleText.length() / 1500.0f);
    float fieldWidth = kMinFieldWidth * std::pow(articleLengthFactor, 0.45f);
    fieldWidth = std::clamp(fieldWidth, kMinFieldWidth, kMinFieldWidth * 4.0f);
    float fieldDepth = kMinFieldDepth;
    if (m_tutorialMode && IsTutorialPresetPage(pageName)) {
        fieldWidth = kTutorialFieldWidth;
        fieldDepth = kTutorialFieldDepth;
    }

    // 地形生成用のテクスチャ解像度を計算します。
    const uint32_t kMaxTexWidth = 16384;
    float texScale = 1.0f;
    uint32_t texWidth  = static_cast<uint32_t>(fieldWidth  * 100.0f);
    uint32_t texHeight = static_cast<uint32_t>(fieldDepth  * 100.0f);
    if (texWidth > kMaxTexWidth) {
        texScale   = (float)kMaxTexWidth / (float)texWidth;
        texWidth   = kMaxTexWidth;
        texHeight  = (uint32_t)(texHeight * texScale);
        LOG_INFO("WikiPageLoader", "Tex width capped to {}. Scale={:.2f}",
                 kMaxTexWidth, texScale);
    }

    std::vector<std::pair<std::wstring, std::string>> linkPairs;
    for (const auto& link : validLinks) {
        linkPairs.push_back({link.second, link.first});
    }

    const auto textureStartedAt = std::chrono::steady_clock::now();
    auto texResult = m_textureGenerator->GenerateTexture(
        core::ToWString(pageName), core::ToWString(articleText),
        linkPairs, state->targetPage, texWidth, texHeight);
    LOG_INFO("WikiPageLoader",
             "BuildPageSync texture generated size={}x{} links={} elapsed={}ms",
             texResult.width, texResult.height, texResult.links.size(),
             ElapsedMs(textureStartedAt));

    // 実際のピクセル数からフィールドサイズを逆算
    float actualFieldDepth = (float)texResult.height / (100.0f * texScale);
    float actualFieldWidth = (float)texResult.width  / (100.0f * texScale);

    // アスペクト比を維持しつつ最小サイズを保証
    float scaleFix = 1.0f;
    if (actualFieldWidth < kMinFieldWidth)
        scaleFix = std::max(scaleFix, kMinFieldWidth / actualFieldWidth);
    if (actualFieldDepth < kMinFieldDepth)
        scaleFix = std::max(scaleFix, kMinFieldDepth / actualFieldDepth);

    if (m_tutorialMode && IsTutorialPresetPage(pageName)) {
        fieldWidth = kTutorialFieldWidth;
        fieldDepth = kTutorialFieldDepth;
    } else {
        fieldWidth = std::min(actualFieldWidth * scaleFix, kMaxSafeWidth);
        fieldDepth = std::min(actualFieldDepth * scaleFix, kMaxSafeDepth);
    }

    m_wikiTexture =
        std::make_unique<graphics::WikiTextureResult>(std::move(texResult));
    m_buildGameplayLinks.clear();
    if (m_tutorialMode && IsTutorialPresetPage(pageName)) {
        m_buildGameplayLinks =
            BuildTutorialPresetGameplayLinks(*m_wikiTexture, state->targetPage);
    }

    // 記事のテーマに応じたスカイボックスを適用します。
    auto* skyboxComp = ctx.world.Get<components::Skybox>(skyboxEntity);
    if (skyboxComp && m_skyboxGenerator) {
        const auto skyboxStartedAt = std::chrono::steady_clock::now();
        graphics::SkyboxTheme theme =
            m_skyboxGenerator->DetermineTheme(pageName, articleText);
        std::wstring themeName =
            graphics::SkyboxTextureGenerator::GetThemeFileName(theme);
        std::wstring skyboxBasePath =
            L"Assets/textures/runtime_skybox/skybox_" + themeName;

        if (m_skyboxGenerator->LoadCubemapFromFiles(
                ctx.graphics.GetDevice(), skyboxBasePath,
                skyboxComp->cubemapSRV)) {
            LOG_INFO("WikiPageLoader", "Skybox loaded: {}",
                     core::ToString(themeName));
            skyboxComp->isVisible = true;

            // 環境プリセット適用
            auto preset = game::components::GetEnvironmentPreset(theme);
            auto particleConfig =
                game::systems::GetParticleConfig(preset.particlePreset);
            // 注: ParticleSystem は WikiGolfScene 側のメンバを使用するため
            //     コールバック or ポインタ渡しで連携する（現状はログのみ）
            LOG_DEBUG("WikiPageLoader",
                      "Particle preset index={}", (int)preset.particlePreset);
        } else {
            // Fallback to Default
            std::wstring defaultPath =
                L"Assets/textures/runtime_skybox/skybox_Default";
            if (m_skyboxGenerator->LoadCubemapFromFiles(
                    ctx.graphics.GetDevice(), defaultPath,
                    skyboxComp->cubemapSRV)) {
                skyboxComp->isVisible = true;
                LOG_INFO("WikiPageLoader", "Skybox fallback to Default");
            } else {
                skyboxComp->isVisible = false;
                LOG_WARN("WikiPageLoader", "Failed to load any skybox");
            }
        }
        LOG_INFO("WikiPageLoader", "BuildPageSync skybox step elapsed={}ms",
                 ElapsedMs(skyboxStartedAt));
    }

    // 計算された最終フィールドサイズをゲーム状態に保存します。
    m_fieldWidth = fieldWidth;
    m_fieldDepth = fieldDepth;
    state->fieldWidth = fieldWidth;
    state->fieldDepth = fieldDepth;
    LOG_INFO("WikiPageLoader", "Final field size: {}x{}", fieldWidth, fieldDepth);

    // カメラ描画距離を拡張
    auto* cam = ctx.world.Get<components::Camera>(cameraEntity);
    if (cam) cam->farZ = std::max(1000.0f, fieldDepth * 2.5f);

    // 地形の構築処理を行います。
    if (m_terrainSystem) {
        const auto terrainStartedAt = std::chrono::steady_clock::now();
        auto terrainTexture = *m_wikiTexture;
        if (!m_buildGameplayLinks.empty()) {
            terrainTexture.links = m_buildGameplayLinks;
        }
        m_terrainSystem->BuildField(ctx, pageName, terrainTexture,
                                    fieldWidth, fieldDepth, pageCategories);
        LOG_INFO("WikiPageLoader", "BuildPageSync terrain built elapsed={}ms",
                 ElapsedMs(terrainStartedAt));
    }

    // ボールをティーグラウンド位置に再配置します。
    auto* ballT  = ctx.world.Get<Transform>(ballEntity);
    auto* ballRB = ctx.world.Get<RigidBody>(ballEntity);
    if (ballT) {
        ballT->position = {0.0f, 1.0f, -fieldDepth * 0.4f};
        LOG_DEBUG("WikiPageLoader", "Ball repositioned to ({}, {}, {})",
                  ballT->position.x, ballT->position.y, ballT->position.z);
        if (ballRB) ballRB->velocity = {0.0f, 0.0f, 0.0f};
        if (minimapController)
            minimapController->SyncMapCenterToBall(
                ctx, 0.0f, m_fieldWidth, m_fieldDepth, true);
    }

    const auto holesStartedAt = std::chrono::steady_clock::now();
    CreateLinksFromTexture(ctx);
    LOG_INFO("WikiPageLoader", "BuildPageSync links/holes created elapsed={}ms",
             ElapsedMs(holesStartedAt));

    // 風向および風速を決定します。
    {
        float windSpeed = 0.0f;
        if (articleText.length() > 2000)
            windSpeed = 3.0f + (float)(rand() % 20) / 10.0f;
        else if (articleText.length() > 500)
            windSpeed = 1.0f + (float)(rand() % 20) / 10.0f;

        float windAngle    = (float)(rand() % 360) * 3.14159f / 180.0f;
        state->windSpeed   = windSpeed;
        state->windDirection = {cosf(windAngle), sinf(windAngle)};
    }

    // 現在のページ履歴を保存します。
    state->currentPage = pageName;
    state->pathHistory.push_back(pageName);

    // 規定打数となるParを算出します。
    {
        int calculatedPar = -1;
        if (m_shortestPath) {
            game::systems::ShortestPathResult r;
            if (state->targetPageId != -1)
                r = m_shortestPath->FindShortestPath(pageName,
                                                      state->targetPageId, 20);
            else
                r = m_shortestPath->FindShortestPath(pageName,
                                                      state->targetPage, 20);
            if (r.success) calculatedPar = r.degrees;
        }
        int par = (calculatedPar >= 0)
                      ? std::max(1, calculatedPar)
                      : game::utils::CalculateWikiGolfPar(
                            -1, validLinks.size());
        state->par = par;
        result.calculatedPar = calculatedPar;
    }

    result.fieldWidth = fieldWidth;
    result.fieldDepth = fieldDepth;
    result.success    = true;
    LOG_INFO("WikiPageLoader",
             "BuildPageSync finished page='{}' elapsed={}ms field={:.1f}x{:.1f} "
             "validLinks={} par={}",
             pageName, ElapsedMs(buildStartedAt), fieldWidth, fieldDepth,
             validLinks.size(), state->par);
    return result;
}

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
    RetireActivePathEvaluationTask();
    m_pathEvaluationProgress.reset();
    m_pathEvaluationTotal.reset();
    m_pathEvaluationPartialMutex.reset();
    m_pathEvaluationPartialResults.reset();
    m_pathEvaluationConsumedResults = 0;
    m_pathEvaluationStarted = false;
    m_nextHoleIndex = 0;
    m_nextMapIconIndex = 0;
    m_nextPathIndex = 0;
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

bool WikiPageLoader::StepBuildPage(core::GameContext& ctx)
{
    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) {
        LOG_ERROR("WikiPageLoader", "BuildPage loadId={} aborted: GameState not found",
                  m_buildLoadId);
        return true;
    }

    if (m_buildStep != BuildStep::EvaluateHolePaths) {
        TryConsumePathEvaluation(ctx, false);
    }
    LogBuildStepTransition();

    PROFILE_SCOPE(std::string("BuildStep_") + BuildStepName(m_buildStep));

    switch (m_buildStep) {
    case BuildStep::ClearOldHoles:
    {
        ClearGeneratedPageObjects(ctx, m_buildMinimap);

        m_buildStep = BuildStep::PrepareLinks;
        m_buildProgress = 0.10f;
        return false;
    }

    case BuildStep::PrepareLinks:
    {
        const auto prepareStartedAt = std::chrono::steady_clock::now();
        // リンクフィルタリング
        auto isIgnored = [](const std::string& t) {
            if (t.empty()) return true;
            if (t.size() >= 3) {
                std::string suffix = t.substr(t.size() - 3);
                if (suffix == "年" || suffix == "月" || suffix == "日") return true;
            }
            if (std::all_of(t.begin(), t.end(),
                            [](unsigned char c) { return std::isdigit(c); }))
                return true;
            return false;
        };

        m_buildValidLinks.clear();
        std::vector<std::tuple<size_t, std::string, std::wstring>> articleLinks;
        for (const auto& link : m_buildData.allLinks) {
            if (isIgnored(link.title)) continue;
            if (link.title == state->targetPage) continue;
            const size_t pos = m_buildData.articleText.find(link.title);
            if (pos != std::string::npos) {
                articleLinks.push_back({pos, link.title, core::ToWString(link.title)});
            }
        }
        std::sort(articleLinks.begin(), articleLinks.end(),
                  [](const auto& a, const auto& b) {
                      return std::get<0>(a) < std::get<0>(b);
                  });
        for (const auto& link : articleLinks) {
            m_buildValidLinks.push_back({std::get<1>(link), std::get<2>(link)});
        }

        if (!state->targetPage.empty() &&
            m_buildData.articleText.find(state->targetPage) != std::string::npos) {
            bool exists = std::any_of(m_buildValidLinks.begin(), m_buildValidLinks.end(),
                [&](const auto& v) { return v.first == state->targetPage; });
            if (!exists) {
                m_buildValidLinks.push_back({state->targetPage,
                                      core::ToWString(state->targetPage)});
            }
        }

        if (m_buildValidLinks.size() < 3) {
            for (const auto& link : m_buildData.allLinks) {
                bool exists = std::any_of(m_buildValidLinks.begin(), m_buildValidLinks.end(),
                    [&](const auto& v) { return v.first == link.title; });
                if (!exists && !isIgnored(link.title)) {
                    m_buildValidLinks.push_back({link.title, core::ToWString(link.title)});
                    if (m_buildValidLinks.size() >= 5) break;
                }
            }
        }

        // フィールドサイズ計算
        float articleLengthFactor = std::max(1.0f, (float)m_buildData.articleText.length() / 1500.0f);
        if (m_tutorialMode && IsTutorialPresetPage(m_buildData.pageName)) {
            m_buildFieldWidth = kTutorialFieldWidth;
            m_buildFieldDepth = kTutorialFieldDepth;
        } else {
            m_buildFieldWidth = kMinFieldWidth * std::pow(articleLengthFactor, 0.45f);
            m_buildFieldWidth = std::clamp(m_buildFieldWidth, kMinFieldWidth, kMinFieldWidth * 4.0f);
            m_buildFieldDepth = kMinFieldDepth;
        }

        // テクスチャサイズ
        const uint32_t kMaxTexWidth = 16384;
        m_buildTexScale = 1.0f;
        m_buildTexWidth  = static_cast<uint32_t>(m_buildFieldWidth  * 100.0f);
        m_buildTexHeight = static_cast<uint32_t>(m_buildFieldDepth  * 100.0f);
        if (m_buildTexWidth > kMaxTexWidth) {
            m_buildTexScale   = (float)kMaxTexWidth / (float)m_buildTexWidth;
            m_buildTexWidth   = kMaxTexWidth;
            m_buildTexHeight  = (uint32_t)(m_buildTexHeight * m_buildTexScale);
        }

        m_buildLinkPairs.clear();
        for (const auto& link : m_buildValidLinks) {
            m_buildLinkPairs.push_back({link.second, link.first});
        }

        m_buildStep = BuildStep::BeginTexture;
        m_buildProgress = 0.20f;
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} prepared links valid={} all={} "
                 "articleBytes={} tex={}x{} elapsed={}ms",
                 m_buildLoadId, m_buildValidLinks.size(),
                 m_buildData.allLinks.size(), m_buildData.articleText.size(),
                 m_buildTexWidth, m_buildTexHeight, ElapsedMs(prepareStartedAt));
        return false;
    }

    case BuildStep::BeginTexture:
    {
        const auto textureBeginStartedAt = std::chrono::steady_clock::now();
        if (m_textureGenerator) {
            m_textureGenerator->BeginGenerateTexture(
                m_textureState,
                core::ToWString(m_buildData.pageName),
                core::ToWString(m_buildData.articleText),
                m_buildLinkPairs,
                state->targetPage,
                m_buildTexWidth,
                m_buildTexHeight
            );
        }
        m_buildStep = BuildStep::GenerateTextureTiles;
        m_buildProgress = 0.25f;
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} texture generation initialized elapsed={}ms",
                 m_buildLoadId, ElapsedMs(textureBeginStartedAt));
        return false;
    }

    case BuildStep::GenerateTextureTiles:
    {
        if (m_textureGenerator) {
            bool textureDone = m_textureGenerator->GenerateNextTile(m_textureState);
            
            float textureProgress = 0.0f;
            if (m_textureState.totalHeight > 0) {
                textureProgress = (float)m_textureState.currentOffsetY / (float)m_textureState.totalHeight;
            }
            m_buildProgress = 0.25f + 0.35f * textureProgress;

            if (textureDone) {
                // 実際のピクセル数からフィールドサイズを逆算
                float actualFieldDepth = (float)m_textureState.result.height / (100.0f * m_buildTexScale);
                float actualFieldWidth = (float)m_textureState.result.width  / (100.0f * m_buildTexScale);

                float scaleFix = 1.0f;
                if (actualFieldWidth < kMinFieldWidth)
                    scaleFix = std::max(scaleFix, kMinFieldWidth / actualFieldWidth);
                if (actualFieldDepth < kMinFieldDepth)
                    scaleFix = std::max(scaleFix, kMinFieldDepth / actualFieldDepth);

                if (m_tutorialMode && IsTutorialPresetPage(m_buildData.pageName)) {
                    m_buildFieldWidth = kTutorialFieldWidth;
                    m_buildFieldDepth = kTutorialFieldDepth;
                } else {
                    m_buildFieldWidth = std::min(actualFieldWidth * scaleFix, kMaxSafeWidth);
                    m_buildFieldDepth = std::min(actualFieldDepth * scaleFix, kMaxSafeDepth);
                }

                m_wikiTexture = std::make_unique<graphics::WikiTextureResult>(std::move(m_textureState.result));
                if (m_tutorialMode && IsTutorialPresetPage(m_buildData.pageName)) {
                    m_buildGameplayLinks =
                        BuildTutorialPresetGameplayLinks(*m_wikiTexture, state->targetPage);
                }
                LOG_INFO("WikiPageLoader",
                         "BuildPage loadId={} texture generation finished "
                         "size={}x{} links={} gameplayLinks={} field={:.1f}x{:.1f}",
                         m_buildLoadId, m_wikiTexture->width,
                         m_wikiTexture->height, m_wikiTexture->links.size(),
                         m_buildGameplayLinks.empty() ? m_wikiTexture->links.size()
                                                      : m_buildGameplayLinks.size(),
                         m_buildFieldWidth, m_buildFieldDepth);
                
                m_buildStep = BuildStep::ApplySkybox;
                m_buildProgress = 0.60f;
            }
        } else {
            m_buildStep = BuildStep::ApplySkybox;
        }
        return false;
    }

    case BuildStep::ApplySkybox:
    {
        const auto skyboxStartedAt = std::chrono::steady_clock::now();
        auto* skyboxComp = ctx.world.Get<components::Skybox>(m_buildSkybox);
        if (skyboxComp && m_skyboxGenerator) {
            graphics::SkyboxTheme theme =
                m_skyboxGenerator->DetermineTheme(m_buildData.pageName, m_buildData.articleText);
            std::wstring themeName =
                graphics::SkyboxTextureGenerator::GetThemeFileName(theme);
            std::wstring skyboxBasePath =
                L"Assets/textures/runtime_skybox/skybox_" + themeName;

            if (!m_skyboxGenerator->LoadCubemapFromFiles(
                    ctx.graphics.GetDevice(), skyboxBasePath,
                    skyboxComp->cubemapSRV)) {
                std::wstring defaultPath = L"Assets/textures/runtime_skybox/skybox_Default";
                m_skyboxGenerator->LoadCubemapFromFiles(ctx.graphics.GetDevice(), defaultPath, skyboxComp->cubemapSRV);
            }
            skyboxComp->isVisible = true;
        }
        
        m_fieldWidth = m_buildFieldWidth;
        m_fieldDepth = m_buildFieldDepth;
        state->fieldWidth = m_buildFieldWidth;
        state->fieldDepth = m_buildFieldDepth;

        auto* cam = ctx.world.Get<components::Camera>(m_buildCamera);
        if (cam) cam->farZ = std::max(1000.0f, m_buildFieldDepth * 2.5f);

        m_buildStep = BuildStep::BeginTerrain;
        m_buildProgress = 0.65f;
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} skybox applied field={:.1f}x{:.1f} "
                 "elapsed={}ms",
                 m_buildLoadId, m_buildFieldWidth, m_buildFieldDepth,
                 ElapsedMs(skyboxStartedAt));
        return false;
    }

    case BuildStep::BeginTerrain:
    {
        const auto terrainBeginStartedAt = std::chrono::steady_clock::now();
        // 地形生成システムを非同期で開始
        if (m_terrainSystem && m_wikiTexture) {
            auto terrainTexture = *m_wikiTexture;
            if (!m_buildGameplayLinks.empty()) {
                terrainTexture.links = m_buildGameplayLinks;
            }
            m_terrainSystem->BeginBuildField(
                m_buildData.pageName,
                terrainTexture,
                m_buildFieldWidth,
                m_buildFieldDepth,
                m_buildData.pageCategories);
        }
        m_buildStep = BuildStep::BuildTerrainStep;
        m_buildProgress = 0.68f;
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} terrain build started elapsed={}ms",
                 m_buildLoadId, ElapsedMs(terrainBeginStartedAt));
        return false;
    }

    case BuildStep::BuildTerrainStep:
    {
        // インクリメンタルに1ステップ進める（毎フレーム）
        if (m_terrainSystem) {
            bool done = m_terrainSystem->StepBuildField(ctx);
            float terrainProg = m_terrainSystem->GetBuildProgress();
            m_buildProgress = 0.68f + 0.17f * terrainProg;
            if (!done) {
                return false; // 次フレームへ
            }
            LOG_INFO("WikiPageLoader",
                     "BuildPage loadId={} terrain build finished terrainProgress={:.1f}%",
                     m_buildLoadId, terrainProg * 100.0f);
        }
        m_buildStep = BuildStep::RepositionBall;
        m_buildProgress = 0.85f;
        return false;
    }

    case BuildStep::RepositionBall:
    {
        auto* ballT  = ctx.world.Get<Transform>(m_buildBall);
        auto* ballRB = ctx.world.Get<RigidBody>(m_buildBall);
        if (ballT) {
            ballT->position = {0.0f, 1.0f, -m_buildFieldDepth * 0.4f};
            if (ballRB) ballRB->velocity = {0.0f, 0.0f, 0.0f};
            if (m_buildMinimap)
                m_buildMinimap->SyncMapCenterToBall(ctx, 0.0f, m_buildFieldWidth, m_buildFieldDepth, true);
        }
        
        m_nextHoleIndex = 0;
        m_nextPathIndex = 0;
        m_nextMapIconIndex = 0;
        m_buildHoleCandidates.clear();
        m_buildPathCandidates.clear();
        m_buildMapHoleCandidates.clear();
        m_buildStep = BuildStep::EvaluateHoles;
        m_buildProgress = 0.85f;
        return false;
    }

    case BuildStep::EvaluateHoles:
    {
        if (!m_wikiTexture) {
            m_buildStep = BuildStep::SetupWind;
            return false;
        }

        const auto& gameplayLinks =
            m_buildGameplayLinks.empty() ? m_wikiTexture->links : m_buildGameplayLinks;
        constexpr size_t kHoleEvaluationsPerFrame = 1000;
        for (size_t i = 0; i < kHoleEvaluationsPerFrame &&
                           m_nextHoleIndex < gameplayLinks.size();
             ++i, ++m_nextHoleIndex) {
            if (std::chrono::steady_clock::now() >= m_buildDeadline) {
                break;
            }

            const auto& linkRegion = gameplayLinks[m_nextHoleIndex];
            m_buildHoleCandidates.push_back(
                BuildHolePlacementCandidate(linkRegion, m_nextHoleIndex));
        }

        if (gameplayLinks.empty() ||
            m_nextHoleIndex >= gameplayLinks.size()) {
            m_buildMapHoleCandidates =
                SelectMapHoleIconCandidates(m_buildHoleCandidates);
            m_buildPathCandidates = m_buildHoleCandidates;
            m_nextPathIndex = 0;
            m_pathEvaluationStarted = false;
            StartAsyncPathEvaluation(state ? state->targetPageId : -1,
                                     kLoadPathEvaluationMaxDepth);
            m_nextMapIconIndex = 0;
            m_buildStep = BuildStep::EvaluateHolePaths;
            m_buildProgress = 0.88f;
            LOG_INFO("WikiPageLoader",
                     "Hole candidates staged: textureLinks={}, mapIcons={}, "
                     "pathTargets={}/{} pathEvaluation=load-depth-2",
                     gameplayLinks.size(), m_buildMapHoleCandidates.size(),
                     m_buildPathCandidates.size(), m_buildHoleCandidates.size());
        } else {
            float holeProgress =
                (float)m_nextHoleIndex / (float)gameplayLinks.size();
            m_buildProgress = 0.85f + 0.03f * holeProgress;
        }
        return false;
    }

    case BuildStep::EvaluateHolePaths:
    {
        // future が既に別コードパス（StepBuildPageWithinFrameBudget 先頭の
        // TryConsumePathEvaluation 呼び出し）で消費済みの場合、
        // !valid() のまま永遠に return false するバグを防ぐ。
        // m_pathEvaluationStarted で「開始済みかつ消費済み」を完了扱いにする。
        const bool alreadyConsumed =
            m_pathEvaluationStarted && !m_pathEvaluationTask.valid();

        if (!alreadyConsumed && !TryConsumePathEvaluation(ctx, false)) {
            if (m_pathEvaluationProgress && m_pathEvaluationTotal) {
                const float doneUnits = static_cast<float>(
                    m_pathEvaluationProgress->load(std::memory_order_relaxed));
                const float totalUnits = std::max(
                    1.0f,
                    static_cast<float>(
                        m_pathEvaluationTotal->load(std::memory_order_relaxed)));
                m_buildProgress = 0.88f + 0.08f *
                                             std::clamp(doneUnits / totalUnits,
                                                        0.0f, 1.0f);
            }
            return false;
        }
        if (alreadyConsumed) {
            LOG_INFO("WikiPageLoader",
                     "BuildPage loadId={} EvaluateHolePaths: task already consumed "
                     "by pre-step poll, skipping wait",
                     m_buildLoadId);
        }
        m_pathEvaluationStarted = false;
        m_nextMapIconIndex = 0;
        m_buildStep = BuildStep::CreateMapIcons;
        m_buildProgress = 0.96f;
        return false;
    }

    case BuildStep::CreateMapIcons:
    {
        constexpr size_t kMapIconsPerFrame = 12;
        if (!m_buildMinimap) {
            m_buildStep = BuildStep::CreateHoles;
            m_nextHoleIndex = 0;
            return false;
        }

        for (size_t i = 0; i < kMapIconsPerFrame &&
                           m_nextMapIconIndex < m_buildMapHoleCandidates.size();
             ++i, ++m_nextMapIconIndex) {
            const auto& candidate = m_buildMapHoleCandidates[m_nextMapIconIndex];
            const bool isPlayable = IsPlayableCandidate(candidate);
            m_buildMinimap->AddHoleIcon(ctx, candidate.x, candidate.z,
                                        candidate.linkTarget,
                                        candidate.isTarget, isPlayable,
                                        candidate.hopsToTarget);
        }

        if (m_buildMapHoleCandidates.empty() ||
            m_nextMapIconIndex >= m_buildMapHoleCandidates.size()) {
            m_nextHoleIndex = 0;
            m_buildStep = BuildStep::CreateHoles;
        } else {
            float iconProgress =
                (float)m_nextMapIconIndex / (float)m_buildMapHoleCandidates.size();
            m_buildProgress = 0.96f + 0.01f * iconProgress;
        }
        return false;
    }

    case BuildStep::CreateHoles:
    {
        constexpr size_t kMaxHolesPerStep = 256;
        if (m_lastCreateHolesProgressLogAt ==
            std::chrono::steady_clock::time_point::min()) {
            m_lastCreateHolesProgressLogAt = std::chrono::steady_clock::now();
            LOG_INFO("WikiPageLoader",
                     "CreateHoles started: loadId={} total={} maxPerStep={}",
                     m_buildLoadId, m_buildHoleCandidates.size(), kMaxHolesPerStep);
        }
        for (size_t i = 0; i < kMaxHolesPerStep &&
                           m_nextHoleIndex < m_buildHoleCandidates.size();
             ++i, ++m_nextHoleIndex) {
            if (std::chrono::steady_clock::now() >= m_buildDeadline) {
                break;
            }
            const auto& candidate = m_buildHoleCandidates[m_nextHoleIndex];
            CreateHole(ctx, candidate.x, candidate.z, candidate.linkTarget,
                       candidate.isTarget, candidate.hopsToTarget, false);
        }

        const auto now = std::chrono::steady_clock::now();
        if (!m_buildHoleCandidates.empty() &&
            now - m_lastCreateHolesProgressLogAt >= kLongBuildStepLogInterval) {
            LOG_INFO("WikiPageLoader",
                     "CreateHoles progress: loadId={} created={}/{} progress={:.1f}%",
                     m_buildLoadId, m_nextHoleIndex, m_buildHoleCandidates.size(),
                     100.0f * static_cast<float>(m_nextHoleIndex) /
                         static_cast<float>(m_buildHoleCandidates.size()));
            m_lastCreateHolesProgressLogAt = now;
        }

        if (m_buildHoleCandidates.empty() ||
            m_nextHoleIndex >= m_buildHoleCandidates.size()) {
            LOG_INFO("WikiPageLoader",
                     "Link holes created: loadId={} textureLinks={}, physicalHoles={}, "
                     "mapIcons={}, pathEvaluated={}",
                     m_buildLoadId,
                     m_wikiTexture ? m_wikiTexture->links.size() : 0,
                     m_buildHoleCandidates.size(), m_buildMapHoleCandidates.size(),
                     m_buildPathCandidates.size());
            m_pathEvaluationStarted = false;
            StartAsyncPathEvaluation(state ? state->targetPageId : -1,
                                     kPathEvaluationMaxDepth);
            m_buildStep = BuildStep::SetupWind;
            m_buildProgress = 0.995f;
        } else {
            float holeProgress =
                (float)m_nextHoleIndex / (float)m_buildHoleCandidates.size();
            m_buildProgress = 0.97f + 0.025f * holeProgress;
        }
        return false;
    }

    case BuildStep::SetupWind:
    {
        float windSpeed = 0.0f;
        if (m_buildData.articleText.length() > 2000)
            windSpeed = 3.0f + (float)(rand() % 20) / 10.0f;
        else if (m_buildData.articleText.length() > 500)
            windSpeed = 1.0f + (float)(rand() % 20) / 10.0f;

        float windAngle    = (float)(rand() % 360) * 3.14159f / 180.0f;
        state->windSpeed   = windSpeed;
        state->windDirection = {cosf(windAngle), sinf(windAngle)};

        state->currentPage = m_buildData.pageName;
        state->pathHistory.push_back(m_buildData.pageName);

        /**
         * @brief 99%付近で描画スレッドを止めないため、Parは即時計算可能な値だけで決定します。山内陽
         * @details 最短パスDB探索はロード初期化とホール距離評価の非同期処理に寄せています。
         */
        TryConsumePathEvaluation(ctx, true);
        RefreshParFromPathEvaluation(ctx, true);

        m_buildStep = BuildStep::Finish;
        m_buildProgress = 0.998f;
        return false;
    }

    case BuildStep::Finish:
        LOG_INFO("WikiPageLoader",
                 "BuildPage loadId={} finished page='{}' total={}ms "
                 "field={:.1f}x{:.1f} holes={} mapIcons={} par={}",
                 m_buildLoadId, m_buildData.pageName, ElapsedMs(m_buildStartedAt),
                 m_buildFieldWidth, m_buildFieldDepth,
                 m_buildHoleCandidates.size(), m_buildMapHoleCandidates.size(),
                 state->par);
        m_buildProgress = 1.0f;
        m_buildStep = BuildStep::None;
        LogBuildStepTransition();
        return true;

    default:
        return true;
    }
}

/**
 * @brief ホールを生成する
 */
void WikiPageLoader::CreateHole(core::GameContext& ctx, float x, float z,
                                const std::string& linkTarget,
                                bool isTargetHole, int hopsToTarget,
                                bool addMapIcon)
{
    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) return;

    // 地形高さ取得
    float terrainH = 0.0f;
    if (m_terrainSystem) terrainH = m_terrainSystem->GetHeight(x, z);

    // ホール本体を生成
    auto e  = ctx.world.CreateEntity();
    auto& t = ctx.world.Add<Transform>(e);
    t.position = {x, terrainH + 0.05f, z};
    t.scale    = {0.5f, 0.08f, 0.5f};

    auto& mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh   = ctx.resource.LoadMesh("builtin/cylinder");
    mr.shader = ctx.resource.LoadShader(
        "Basic", L"Assets/shaders/BasicVS.hlsl",
        L"Assets/shaders/BasicPS.hlsl");
    mr.color = GetHoleBodyColor(isTargetHole, hopsToTarget);

    auto& h      = ctx.world.Add<GolfHole>(e);
    h.radius     = 2.0f;
    h.gravity    = 0.0f;
    h.linkTarget  = linkTarget;
    h.isTarget   = isTargetHole;
    h.hopsToTarget = hopsToTarget;

    // 目的地、または1〜2ホップの近いホールのみプロシージャル旗を生成する（ドローコール削減とFPS向上のため）
    if (isTargetHole || (hopsToTarget >= 1 && hopsToTarget <= 2)) {
        game::utils::ProceduralFlagOptions options;
        options.holeEntity = static_cast<uint32_t>(e);
        options.large = isTargetHole;
        options.createParticles = (isTargetHole || hopsToTarget == 1);
        options.animationWeight = isTargetHole ? 1.0f : 0.72f;

        auto flagColor = GetHoleColor(isTargetHole, hopsToTarget);
        auto flagResult = game::utils::CreateProceduralFlag(
            ctx, {x, terrainH + 0.05f, z}, flagColor, options);

        // 生成されたすべてのパーティクルエンティティをホールのリストに追加
        for (auto flagEntity : flagResult.particleEntities) {
            h.particleEntities.push_back(static_cast<uint32_t>(flagEntity));
        }
    }

    // ラベルを生成
    auto labelE  = ctx.world.CreateEntity();
    auto& labelUI = ctx.world.Add<UIText>(labelE);
    labelUI.text  = isTargetHole ? L"🎯" : L"";
    labelUI.style = graphics::TextStyle::Guide();
    labelUI.style.fontSize = 24.0f;
    labelUI.style.color = GetHoleColor(isTargetHole, hopsToTarget);
    labelUI.visible = false;
    labelUI.layer   = 60;

    auto& label3D     = ctx.world.Add<World3DLabel>(labelE);
    label3D.worldPos  = {x, terrainH + 3.0f, z};
    label3D.uiTextEntity = labelE;
    label3D.offsetY   = 3.0f;
    label3D.visible   = true;
    h.labelEntity     = labelE;

    // ターゲットまたは1ホップの場合は光柱を生成
    if (isTargetHole || hopsToTarget == 1) {
        auto pillarE  = ctx.world.CreateEntity();
        auto& pillarT = ctx.world.Add<Transform>(pillarE);
        float pillarH = isTargetHole ? 15.0f : 8.0f;
        pillarT.position = {x, terrainH + pillarH * 0.5f, z};
        pillarT.scale    = {0.3f, pillarH, 0.3f};

        auto& pillarMr = ctx.world.Add<MeshRenderer>(pillarE);
        pillarMr.mesh   = ctx.resource.LoadMesh("builtin/cylinder");
        pillarMr.shader = ctx.resource.LoadShader(
            "Basic", L"Assets/shaders/BasicVS.hlsl",
            L"Assets/shaders/BasicPS.hlsl");
        pillarMr.color = isTargetHole
                             ? XMFLOAT4{1.0f, 0.3f, 0.3f, 0.4f}
                             : XMFLOAT4{1.0f, 0.85f, 0.2f, 0.3f};
        pillarMr.isTransparent = true;
        h.pillarEntity = static_cast<uint32_t>(pillarE);
    }

    state->holes.push_back(e);
    if (addMapIcon && m_buildMinimap) {
        m_buildMinimap->AddHoleIcon(ctx, x, z, linkTarget, isTargetHole,
                                    true, hopsToTarget);
    }
    LOG_DEBUG("WikiPageLoader",
              "Hole created at ({:.1f},{:.1f}) target='{}' isTarget={} hops={}",
              x, z, linkTarget, isTargetHole, hopsToTarget);
}

/**
 * @brief リンク領域から配置候補を作り、価値計算に使う距離も求めます。 山内陽
 */
WikiPageLoader::HolePlacementCandidate
WikiPageLoader::BuildHolePlacementCandidate(const graphics::LinkRegion& link,
                                            size_t originalIndex) const
{
    HolePlacementCandidate candidate;
    if (!m_wikiTexture || m_wikiTexture->width == 0 || m_wikiTexture->height == 0) {
        return candidate;
    }

    const float texW = static_cast<float>(m_wikiTexture->width);
    const float texH = static_cast<float>(m_wikiTexture->height);
    const float cx = link.x + link.width * 0.5f;
    const float cy = link.y + link.height * 0.5f;

    candidate.x = (cx / texW - 0.5f) * m_fieldWidth;
    candidate.z = (0.5f - cy / texH) * m_fieldDepth;
    candidate.linkTarget = link.targetPage;
    candidate.isTarget = link.isTarget;
    candidate.originalIndex = originalIndex;

    return candidate;
}

/**
 * @brief 距離を保って候補を追加できるか確認します。 山内陽
 */
bool WikiPageLoader::IsFarEnoughFromSelected(
    const std::vector<HolePlacementCandidate>& selected,
    const HolePlacementCandidate& candidate,
    float minDistance) const
{
    const float minDistanceSq = minDistance * minDistance;
    return std::none_of(
        selected.begin(), selected.end(),
        [&](const HolePlacementCandidate& existing) {
            const float dx = candidate.x - existing.x;
            const float dz = candidate.z - existing.z;
            return dx * dx + dz * dz < minDistanceSq;
        });
}

/**
 * @brief マップビューに残す軽量リンク候補を選びます。 山内陽
 */
std::vector<WikiPageLoader::HolePlacementCandidate>
WikiPageLoader::SelectMapHoleIconCandidates(
    const std::vector<HolePlacementCandidate>& candidates) const
{
    std::vector<HolePlacementCandidate> sorted = candidates;
    std::stable_sort(sorted.begin(), sorted.end(),
        [](const HolePlacementCandidate& lhs,
           const HolePlacementCandidate& rhs) {
            if (lhs.isTarget != rhs.isTarget) {
                return lhs.isTarget;
            }
            return lhs.originalIndex < rhs.originalIndex;
        });

    std::vector<HolePlacementCandidate> selected;
    selected.reserve(std::min(sorted.size(), kMaxMapHoleIcons));
    for (const auto& candidate : sorted) {
        if (candidate.isTarget ||
            IsFarEnoughFromSelected(selected, candidate, kMinMapIconDistance)) {
            selected.push_back(candidate);
        }
        if (selected.size() >= kMaxMapHoleIcons) {
            break;
        }
    }

    std::sort(selected.begin(), selected.end(),
        [](const HolePlacementCandidate& lhs,
           const HolePlacementCandidate& rhs) {
            return lhs.originalIndex < rhs.originalIndex;
        });

    if (selected.size() != candidates.size()) {
        LOG_INFO("WikiPageLoader",
                 "Map link icons filtered: candidates={}, selected={}, "
                 "minDistance={:.1f}",
                 candidates.size(), selected.size(), kMinMapIconDistance);
    }
    return selected;
}

/**
 * @brief 経路評価結果を全ホール候補とマップ候補へ反映します。 山内陽
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
        const int hops = candidate.isTarget ? 0 : candidate.hopsToTarget;
        if (hops < 0) {
            continue;
        }
        minResolvedHops =
            (minResolvedHops < 0) ? hops : std::min(minResolvedHops, hops);
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
    m_buildResult.calculatedPar =
        (minResolvedHops >= 0) ? std::max(1, minResolvedHops + 1) : -1;
}

/**
 * @brief 候補のリンク距離をページ内キャッシュ付きで評価します。 山内陽
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

    candidate.hopsToTarget = r.success ? r.degrees : -1;
    m_pathHopCache[cacheKey] = candidate.hopsToTarget;
}

/**
 * @brief マップ候補がプレイ可能ホールとして選ばれたかを判定します。 山内陽
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

/**
 * @brief テクスチャのリンク領域からホールを一括配置する
 */
void WikiPageLoader::CreateLinksFromTexture(core::GameContext& ctx)
{
    if (!m_wikiTexture) return;

    const auto& gameplayLinks =
        m_buildGameplayLinks.empty() ? m_wikiTexture->links : m_buildGameplayLinks;
    std::vector<HolePlacementCandidate> candidates;
    candidates.reserve(gameplayLinks.size());
    for (size_t i = 0; i < gameplayLinks.size(); ++i) {
        candidates.push_back(
            BuildHolePlacementCandidate(gameplayLinks[i], i));
    }

    m_buildMapHoleCandidates = SelectMapHoleIconCandidates(candidates);
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
