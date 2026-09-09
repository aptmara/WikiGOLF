/**
 * @file WikiPageLoaderBuildSync.cpp
 * @brief 同期ページ構築処理を実装します。
*/

#include "../../graphics/GraphicsDevice.h"
#include "WikiPageLoader.h"
#include "HtmlCourseSizing.h"
#include "HtmlHoleSpacing.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/EnvironmentPresets.h"
#include "../components/PhysicsComponents.h"
#include "../components/Skybox.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../controllers/MinimapController.h"
#include "../systems/PostProcessSystem.h"
#include "../systems/ParticleSystem.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/ParRules.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <chrono>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

namespace {
constexpr float kFieldScale = 4.0f;
constexpr float kMinFieldWidth = 20.0f * kFieldScale;
constexpr float kMinFieldDepth = 30.0f * kFieldScale;
constexpr float kMaxSafeDepth = 20000.0f;
constexpr float kMaxSafeWidth = 20000.0f;

long long ElapsedMs(const std::chrono::steady_clock::time_point& startedAt) {
    if (startedAt == std::chrono::steady_clock::time_point::min()) {
        return 0;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - startedAt)
        .count();
}
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
    std::vector<graphics::PendingWikiImage> pendingImages = std::move(asyncData.pendingImages);


    const auto linkSelection = m_pageLinkSelector.Select(
        allLinks, articleText, state->targetPage);
    std::vector<std::pair<std::string, std::wstring>> validLinks =
        linkSelection.links;
    if (linkSelection.targetAdded) {
        LOG_INFO("WikiPageLoader", "Target '{}' added to links",
                 state->targetPage);
    }

    // 記事本文の長さに応じてフィールドサイズを計算します。
    float articleLengthFactor =
        std::max(1.0f, (float)articleText.length() / 1500.0f);
    float fieldWidth = kMinFieldWidth * std::pow(articleLengthFactor, 0.45f);
    fieldWidth = std::clamp(fieldWidth, kMinFieldWidth, kMinFieldWidth * 4.0f);
    float fieldDepth = kMinFieldDepth;
    if (m_tutorialMode && m_tutorialCourseLayout.IsPresetPage(pageName)) {
        fieldWidth = TutorialCourseLayout::GetFieldWidth();
        fieldDepth = TutorialCourseLayout::GetFieldDepth();
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
        linkPairs, state->targetPage, texWidth, texHeight,
        std::move(pendingImages), m_tutorialMode ? std::string{} : asyncData.articleHtml);
    LOG_INFO("WikiPageLoader",
             "BuildPageSync texture generated size={}x{} links={} elapsed={}ms",
             texResult.width, texResult.height, texResult.links.size(),
             ElapsedMs(textureStartedAt));

    if (!texResult.width || !texResult.height || texResult.tiles.empty()) return {};

    // 実際のピクセル数からフィールドサイズを逆算
    float actualFieldDepth = (float)texResult.height / (100.0f * texScale);
    float actualFieldWidth = (float)texResult.width  / (100.0f * texScale);

    // アスペクト比を維持しつつ最小サイズを保証
    float scaleFix = 1.0f;
    if (actualFieldWidth < kMinFieldWidth)
        scaleFix = std::max(scaleFix, kMinFieldWidth / actualFieldWidth);
    if (actualFieldDepth < kMinFieldDepth)
        scaleFix = std::max(scaleFix, kMinFieldDepth / actualFieldDepth);

    if (m_tutorialMode && m_tutorialCourseLayout.IsPresetPage(pageName)) {
        fieldWidth = TutorialCourseLayout::GetFieldWidth();
        fieldDepth = TutorialCourseLayout::GetFieldDepth();
    } else {
        fieldWidth = std::min(actualFieldWidth * scaleFix, kMaxSafeWidth);
        fieldDepth = std::min(actualFieldDepth * scaleFix, kMaxSafeDepth);
    }

    if (texResult.layoutWidth > 0) {
        const auto htmlField = CalculateHtmlCourseFieldSize(
            texResult.layoutWidth, texResult.layoutHeight);
        fieldWidth = htmlField.width;
        fieldDepth = htmlField.depth;
    }
    m_wikiTexture =
        std::make_unique<graphics::WikiTextureResult>(std::move(texResult));
    m_buildGameplayLinks.clear();
    if (m_tutorialMode && m_tutorialCourseLayout.IsPresetPage(pageName)) {
        m_buildGameplayLinks =
            m_tutorialCourseLayout.BuildGameplayLinks(*m_wikiTexture,
                                                     state->targetPage);
    }

    if (m_wikiTexture->layoutWidth > 0)
        m_buildGameplayLinks = SelectSpacedHtmlLinks(m_wikiTexture->links,
            static_cast<float>(m_wikiTexture->width),static_cast<float>(m_wikiTexture->height),fieldWidth,fieldDepth);

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
            if (ctx.postProcess) {
                ctx.postProcess->UpdateFromEnvironment(preset, ctx.time);
            }
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
        float terrainHeight = 0.0f;
        if (m_terrainSystem) {
            terrainHeight =
                m_terrainSystem->GetHeight(0.0f, -fieldDepth * 0.4f);
        }
        ballT->position = {
            0.0f,
            game::physics::ToVisualSurfaceHeight(terrainHeight) +
                game::physics::kBallRadius,
            -fieldDepth * 0.4f};
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
        int par = game::utils::CalculateWikiGolfPar(-1, validLinks.size());
        if (calculatedPar >= 0) {
            par = std::max(1, calculatedPar);
        }
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


} // namespace game::scenes
