/**
 * @file WikiPageLoaderBuildStep.cpp
 * @brief 段階構築の状態遷移を実装します。
*/

#include "../../graphics/GraphicsDevice.h"
#include "WikiPageLoader.h"
#include "HtmlHoleSpacing.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/Profiler.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../controllers/MinimapController.h"
#include "../utils/GameplayPhysicsConstants.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#undef min
#undef max

namespace game::scenes {

using namespace game::components;

namespace {
constexpr float kFieldScale = 4.0f;
constexpr float kMinFieldWidth = 20.0f * kFieldScale;
constexpr float kMinFieldDepth = 30.0f * kFieldScale;
constexpr float kMaxSafeDepth = 20000.0f;
constexpr float kMaxSafeWidth = 20000.0f;
constexpr int kPathEvaluationMaxDepth = 4;
constexpr int kLoadPathEvaluationMaxDepth = 2;
constexpr auto kLongBuildStepLogInterval = std::chrono::seconds(2);

long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    if (startedAt == std::chrono::steady_clock::time_point::min()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}
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
        const auto linkSelection = m_pageLinkSelector.Select(
            m_buildData.allLinks,
            m_buildData.articleText,
            state->targetPage);
        m_buildValidLinks = linkSelection.links;

        // フィールドサイズ計算
        float articleLengthFactor = std::max(1.0f, (float)m_buildData.articleText.length() / 1500.0f);
        if (m_tutorialMode &&
            m_tutorialCourseLayout.IsPresetPage(m_buildData.pageName)) {
            m_buildFieldWidth = TutorialCourseLayout::GetFieldWidth();
            m_buildFieldDepth = TutorialCourseLayout::GetFieldDepth();
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
            if (!m_textureGenerator->BeginGenerateTexture(
                m_textureState,
                core::ToWString(m_buildData.pageName),
                core::ToWString(m_buildData.articleText),
                m_buildLinkPairs,
                state->targetPage,
                m_buildTexWidth,
                m_buildTexHeight,
                m_buildData.pendingImages,
                m_tutorialMode ? std::string{} : m_buildData.articleHtml
            )) {
                m_buildStep = BuildStep::None;
                m_buildResult.success = false;
                return true;
            }
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

            if (textureDone && m_textureState.failed) {
                if (m_buildData.articleHtml.empty()) {
                    m_buildStep = BuildStep::None;
                    m_buildResult.success = false;
                    LOG_ERROR("WikiPageLoader", "Texture generation failed for {}", m_buildData.pageName);
                    return true;
                }
                m_buildData.articleHtml.clear();
                m_buildStep = BuildStep::BeginTexture;
                return false;
            }
            if (textureDone) {
                // 実際のピクセル数からフィールドサイズを逆算
                float actualFieldDepth = (float)m_textureState.result.height / (100.0f * m_buildTexScale);
                float actualFieldWidth = (float)m_textureState.result.width  / (100.0f * m_buildTexScale);

                float scaleFix = 1.0f;
                if (actualFieldWidth < kMinFieldWidth)
                    scaleFix = std::max(scaleFix, kMinFieldWidth / actualFieldWidth);
                if (actualFieldDepth < kMinFieldDepth)
                    scaleFix = std::max(scaleFix, kMinFieldDepth / actualFieldDepth);

                if (m_tutorialMode &&
                    m_tutorialCourseLayout.IsPresetPage(m_buildData.pageName)) {
                    m_buildFieldWidth = TutorialCourseLayout::GetFieldWidth();
                    m_buildFieldDepth = TutorialCourseLayout::GetFieldDepth();
                } else {
                    m_buildFieldWidth = std::min(actualFieldWidth * scaleFix, kMaxSafeWidth);
                    m_buildFieldDepth = std::min(actualFieldDepth * scaleFix, kMaxSafeDepth);
                }

                if (m_textureState.result.layoutWidth > 0) {
                    m_buildFieldWidth = std::clamp(kMinFieldWidth * std::pow(
                        std::max(1.0f, static_cast<float>(m_buildData.articleText.size()) / 1500.0f),0.45f),
                        kMinFieldWidth,kMinFieldWidth*4.0f);
                    m_buildFieldDepth = std::clamp(m_buildFieldWidth *
                        m_textureState.result.layoutHeight / m_textureState.result.layoutWidth,
                        kMinFieldDepth, kMaxSafeDepth);
                }
                m_buildData.pendingImages.clear();
                m_wikiTexture = std::make_unique<graphics::WikiTextureResult>(std::move(m_textureState.result));
                if (m_tutorialMode &&
                    m_tutorialCourseLayout.IsPresetPage(m_buildData.pageName)) {
                    m_buildGameplayLinks =
                        m_tutorialCourseLayout.BuildGameplayLinks(
                            *m_wikiTexture, state->targetPage);
                }
                if (m_wikiTexture->layoutWidth > 0)
                    m_buildGameplayLinks = SelectSpacedHtmlLinks(m_wikiTexture->links,
                        static_cast<float>(m_wikiTexture->width),static_cast<float>(m_wikiTexture->height),
                        m_buildFieldWidth,m_buildFieldDepth);
                size_t gameplayLinkCount = m_wikiTexture->links.size();
                if (!m_buildGameplayLinks.empty()) {
                    gameplayLinkCount = m_buildGameplayLinks.size();
                }
            LOG_INFO("WikiPageLoader",
                     "BuildPage loadId={} texture generation finished "
                     "size={}x{} links={} gameplayLinks={} field={:.1f}x{:.1f}",
                     m_buildLoadId, m_wikiTexture->width,
                     m_wikiTexture->height, m_wikiTexture->links.size(),
                     gameplayLinkCount, m_buildFieldWidth, m_buildFieldDepth);

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
            const float ballZ = -m_buildFieldDepth * 0.4f;
            float terrainHeight = 0.0f;
            if (m_terrainSystem) {
                terrainHeight = m_terrainSystem->GetHeight(0.0f, ballZ);
            }
            ballT->position = {
                0.0f,
                game::physics::ToVisualSurfaceHeight(terrainHeight) +
                    game::physics::kBallRadius,
                ballZ};
            if (ballRB) ballRB->velocity = {0.0f, 0.0f, 0.0f};
            if (m_buildMinimap)
                m_buildMinimap->SyncMapCenterToBall(ctx, 0.0f, m_buildFieldWidth, m_buildFieldDepth, true);
        }

        m_nextHoleIndex = 0;
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

        const auto* gameplayLinks = &m_wikiTexture->links;
        if (!m_buildGameplayLinks.empty()) {
            gameplayLinks = &m_buildGameplayLinks;
        }
        constexpr size_t kHoleEvaluationsPerFrame = 1000;
        for (size_t i = 0; i < kHoleEvaluationsPerFrame &&
                           m_nextHoleIndex < gameplayLinks->size();
             ++i, ++m_nextHoleIndex) {
            if (std::chrono::steady_clock::now() >= m_buildDeadline) {
                break;
            }

            const auto& linkRegion = (*gameplayLinks)[m_nextHoleIndex];
            m_buildHoleCandidates.push_back(
                m_holePlacementPlanner.BuildCandidate(
                    linkRegion, m_nextHoleIndex, m_wikiTexture->width,
                    m_wikiTexture->height, m_fieldWidth, m_fieldDepth));
        }

        if (gameplayLinks->empty() ||
            m_nextHoleIndex >= gameplayLinks->size()) {
            m_buildMapHoleCandidates =
                m_holePlacementPlanner.SelectMapCandidates(
                    m_buildHoleCandidates);
            m_buildPathCandidates = m_buildHoleCandidates;
            m_pathEvaluator.PrepareNextEvaluation();
            int targetPageId = -1;
            if (state) {
                targetPageId = state->targetPageId;
            }
            m_pathEvaluator.Start(m_buildPathCandidates, targetPageId,
                                  kLoadPathEvaluationMaxDepth,
                                  m_buildLoadId);
            m_nextMapIconIndex = 0;
            m_buildStep = BuildStep::EvaluateHolePaths;
            m_buildProgress = 0.88f;
            LOG_INFO("WikiPageLoader",
                     "Hole candidates staged: textureLinks={}, mapIcons={}, "
                     "pathTargets={}/{} pathEvaluation=load-depth-2",
                     gameplayLinks->size(), m_buildMapHoleCandidates.size(),
                     m_buildPathCandidates.size(), m_buildHoleCandidates.size());
        } else {
            float holeProgress =
                (float)m_nextHoleIndex / (float)gameplayLinks->size();
            m_buildProgress = 0.85f + 0.03f * holeProgress;
        }
        return false;
    }

    case BuildStep::EvaluateHolePaths:
    {
        // future が既に別コードパス（StepBuildPageWithinFrameBudget 先頭の
        // TryConsumePathEvaluation 呼び出し）で消費済みの場合、
        // !valid() のまま永遠に return false するバグを防ぐ。
        // evaluator の開始済み状態で「開始済みかつ消費済み」を完了扱いにする。
        const bool alreadyConsumed =
            m_pathEvaluator.HasStarted() && !m_pathEvaluator.HasActiveTask();

        if (!alreadyConsumed && !TryConsumePathEvaluation(ctx, false)) {
            m_buildProgress =
                0.88f + 0.08f * m_pathEvaluator.GetProgress();
            return false;
        }
        if (alreadyConsumed) {
            LOG_INFO("WikiPageLoader",
                     "BuildPage loadId={} EvaluateHolePaths: task already consumed "
                     "by pre-step poll, skipping wait",
                     m_buildLoadId);
        }
        m_pathEvaluator.PrepareNextEvaluation();
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
            size_t textureLinkCount = 0;
            if (m_wikiTexture) {
                textureLinkCount = m_wikiTexture->links.size();
            }
            LOG_INFO("WikiPageLoader",
                     "Link holes created: loadId={} textureLinks={}, physicalHoles={}, "
                     "mapIcons={}, pathEvaluated={}",
                     m_buildLoadId, textureLinkCount,
                     m_buildHoleCandidates.size(), m_buildMapHoleCandidates.size(),
                     m_buildPathCandidates.size());
            m_pathEvaluator.PrepareNextEvaluation();
            int targetPageId = -1;
            if (state) {
                targetPageId = state->targetPageId;
            }
            m_pathEvaluator.Start(m_buildPathCandidates, targetPageId,
                                  kPathEvaluationMaxDepth, m_buildLoadId);
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
         * @brief 99%付近で描画スレッドを止めないため、Parは即時計算可能な値だけで決定します。
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

} // namespace game::scenes

