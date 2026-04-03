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
#include "../controllers/MinimapController.h"
#include "../systems/ParticleSystem.h"
#include "../systems/WikiClient.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
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
constexpr float kMinHoleDistance = 0.2f;
} // namespace

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

void WikiPageLoader::SetPreloadedData(std::vector<game::WikiLink> links,
                                      std::string                 extract)
{
    m_preloadedLinks   = std::move(links);
    m_preloadedExtract = std::move(extract);
    m_hasPreloadedData = true;
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
    auto asyncData = FetchPageDataAsync(pageName);
    return BuildPageSync(ctx, std::move(asyncData), ballEntity, cameraEntity, skyboxEntity, minimapController);
}

PageDataAsyncResult WikiPageLoader::FetchPageDataAsync(const std::string& pageName) {
    PageDataAsyncResult res;
    res.pageName = pageName;
    game::systems::WikiClient wikiClient;

    if (m_hasPreloadedData) {
        LOG_INFO("WikiPageLoader", "Using preloaded data for async: {}", pageName);
        res.allLinks    = std::move(m_preloadedLinks);
        res.articleText = std::move(m_preloadedExtract);
        m_hasPreloadedData = false;
        res.pageCategories = wikiClient.FetchPageCategories(pageName);
    } else {
        LOG_INFO("WikiPageLoader", "Fetching live data async for: {}", pageName);
        res.allLinks    = wikiClient.FetchPageLinks(pageName, 0);
        res.articleText = wikiClient.FetchPageExtract(pageName, 5000);
        res.pageCategories = wikiClient.FetchPageCategories(pageName);
    }
    res.hasData = true;
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
    PageLoadResult result;
    const std::string& pageName = asyncData.pageName;

    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) {
        LOG_ERROR("WikiPageLoader", "BuildPageSync: GameState not found!");
        return result;
    }

    LOG_INFO("WikiPageLoader", "Building page: {}", pageName);

    // ---- 1. 古いホールを削除 ----
    {
        std::vector<ecs::Entity> holesToDelete;
        std::vector<ecs::Entity> relatedToDelete;
        ctx.world.Query<GolfHole>().Each(
            [&](ecs::Entity e, GolfHole& hole) {
                holesToDelete.push_back(e);
                if (hole.labelEntity  != 0) relatedToDelete.push_back(ecs::Entity(hole.labelEntity));
                if (hole.pillarEntity != 0) relatedToDelete.push_back(ecs::Entity(hole.pillarEntity));
            });
        for (auto e : relatedToDelete) {
            if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e);
        }
        for (auto e : holesToDelete) {
            if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e);
        }
        std::vector<ecs::Entity> flagsToDelete;
        ctx.world.Query<HoleFlag>().Each(
            [&](ecs::Entity e, HoleFlag&) { flagsToDelete.push_back(e); });
        for (auto e : flagsToDelete) {
            if (ctx.world.IsAlive(e)) ctx.world.DestroyEntity(e);
        }
        state->holes.clear();
    }
    LOG_DEBUG("WikiPageLoader", "Holes cleared");

    // ---- 2. 記事データ取得 (Fetch済) ----
    std::vector<game::WikiLink> allLinks = std::move(asyncData.allLinks);
    std::string articleText = std::move(asyncData.articleText);
    std::vector<std::string> pageCategories = std::move(asyncData.pageCategories);


    // ---- 3. リンクのフィルタリング ----
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

    // 本文長に応じた上限（2000 文字につき 5 リンク、上限 50）
    const size_t kLinksPerChars = 5;
    const size_t kCharsPerUnit  = 2000;
    const size_t kMaxLinks      = 50;
    size_t dynamicLimit = (articleText.length() / kCharsPerUnit + 1) * kLinksPerChars;
    size_t linkLimit    = (dynamicLimit < kMaxLinks) ? dynamicLimit : kMaxLinks;

    std::vector<std::pair<std::string, std::wstring>> validLinks;
    for (const auto& link : allLinks) {
        if (isIgnored(link.title)) continue;
        if (link.title == state->targetPage) continue; // ターゲットは後で追加
        if (articleText.find(link.title) != std::string::npos) {
            validLinks.push_back({link.title, core::ToWString(link.title)});
        }
        if (validLinks.size() >= linkLimit) break;
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

    // ---- 4. フィールドサイズ計算 ----
    float articleLengthFactor =
        std::max(1.0f, (float)articleText.length() / 1500.0f);
    float fieldWidth = kMinFieldWidth * std::pow(articleLengthFactor, 0.45f);
    fieldWidth = std::clamp(fieldWidth, kMinFieldWidth, kMinFieldWidth * 4.0f);
    float fieldDepth = kMinFieldDepth;

    // ---- 5. テクスチャ生成 ----
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

    auto texResult = m_textureGenerator->GenerateTexture(
        core::ToWString(pageName), core::ToWString(articleText),
        linkPairs, state->targetPage, texWidth, texHeight);

    // 実際のピクセル数からフィールドサイズを逆算
    float actualFieldDepth = (float)texResult.height / (100.0f * texScale);
    float actualFieldWidth = (float)texResult.width  / (100.0f * texScale);

    // アスペクト比を維持しつつ最小サイズを保証
    float scaleFix = 1.0f;
    if (actualFieldWidth < kMinFieldWidth)
        scaleFix = std::max(scaleFix, kMinFieldWidth / actualFieldWidth);
    if (actualFieldDepth < kMinFieldDepth)
        scaleFix = std::max(scaleFix, kMinFieldDepth / actualFieldDepth);

    fieldWidth = std::min(actualFieldWidth * scaleFix, kMaxSafeWidth);
    fieldDepth = std::min(actualFieldDepth * scaleFix, kMaxSafeDepth);

    m_wikiTexture =
        std::make_unique<graphics::WikiTextureResult>(std::move(texResult));

    // ---- 6. スカイボックス適用 ----
    auto* skyboxComp = ctx.world.Get<components::Skybox>(skyboxEntity);
    if (skyboxComp && m_skyboxGenerator) {
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
    }

    // ---- 7. GolfGameState へフィールドサイズ保存 ----
    m_fieldWidth = fieldWidth;
    m_fieldDepth = fieldDepth;
    state->fieldWidth = fieldWidth;
    state->fieldDepth = fieldDepth;
    LOG_INFO("WikiPageLoader", "Final field size: {}x{}", fieldWidth, fieldDepth);

    // カメラ描画距離を拡張
    auto* cam = ctx.world.Get<components::Camera>(cameraEntity);
    if (cam) cam->farZ = std::max(1000.0f, fieldDepth * 2.5f);

    // ---- 8. 地形構築 ----
    if (m_terrainSystem) {
        m_terrainSystem->BuildField(ctx, pageName, *m_wikiTexture,
                                    fieldWidth, fieldDepth, pageCategories);
    }

    // ---- 9. ボール再配置 ----
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

    // ---- 10. ホール配置 ----
    {
        std::vector<std::pair<float, float>> createdPositions;
        const float texW = (float)m_wikiTexture->width;
        const float texH = (float)m_wikiTexture->height;

        LOG_INFO("WikiPageLoader",
                 "Hole placement: texLinks={}, validLinks={}, field={}x{}",
                 m_wikiTexture->links.size(), validLinks.size(),
                 fieldWidth, fieldDepth);

        for (const auto& linkRegion : m_wikiTexture->links) {
            float cx = linkRegion.x + linkRegion.width  * 0.5f;
            float cy = linkRegion.y + linkRegion.height * 0.5f;
            float worldX = (cx / texW - 0.5f) * fieldWidth;
            float worldZ = (0.5f - cy / texH) * fieldDepth;

            // 近接チェック
            bool tooClose = false;
            for (const auto& pos : createdPositions) {
                float dx = worldX - pos.first;
                float dz = worldZ - pos.second;
                if (dx * dx + dz * dz < kMinHoleDistance * kMinHoleDistance) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;
            createdPositions.push_back({worldX, worldZ});

            // SDOW 距離計算
            int hops = -1;
            if (m_shortestPath && m_shortestPath->IsAvailable() &&
                state->targetPageId != -1) {
                auto r = m_shortestPath->FindShortestPath(
                    linkRegion.targetPage, state->targetPageId, 6);
                if (r.success) hops = r.degrees;
            }

            CreateHole(ctx, worldX, worldZ, linkRegion.targetPage,
                       linkRegion.isTarget, hops);
        }

        LOG_INFO("WikiPageLoader", "Total holes created: {}",
                 createdPositions.size());
    }

    // ---- 11. 風の設定 ----
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

    // ---- 12. 現在ページ更新 ----
    state->currentPage = pageName;
    state->pathHistory.push_back(pageName);

    // ---- 13. Par 計算 ----
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
        int par = (calculatedPar > 0)
                      ? calculatedPar
                      : (int)validLinks.size() / 2 + 2;
        state->par = par;
        result.calculatedPar = calculatedPar;
    }

    result.fieldWidth = fieldWidth;
    result.fieldDepth = fieldDepth;
    result.success    = true;
    return result;
}

// ============================================================
// CreateHole
// ============================================================
void WikiPageLoader::CreateHole(core::GameContext& ctx, float x, float z,
                                const std::string& linkTarget,
                                bool isTargetHole, int hopsToTarget)
{
    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) return;

    // 地形高さ取得
    float terrainH = 0.0f;
    if (m_terrainSystem) terrainH = m_terrainSystem->GetHeight(x, z);

    // --- ホール本体 ---
    auto e  = ctx.world.CreateEntity();
    auto& t = ctx.world.Add<Transform>(e);
    t.position = {x, terrainH + 0.05f, z};
    t.scale    = {0.5f, 0.08f, 0.5f};

    auto& mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh   = ctx.resource.LoadMesh("builtin/cylinder");
    mr.shader = ctx.resource.LoadShader(
        "Basic", L"Assets/shaders/BasicVS.hlsl",
        L"Assets/shaders/BasicPS.hlsl");
    mr.color = isTargetHole
                   ? XMFLOAT4{0.8f, 0.0f, 0.0f, 1.0f}
                   : XMFLOAT4{0.0f, 0.0f, 0.0f, 1.0f};

    auto& h      = ctx.world.Add<GolfHole>(e);
    h.radius     = 2.0f;
    h.gravity    = 0.0f;
    h.linkTarget  = linkTarget;
    h.isTarget   = isTargetHole;
    h.hopsToTarget = hopsToTarget;

    // --- 旗モデル ---
    auto flagE  = ctx.world.CreateEntity();
    auto& flagT = ctx.world.Add<Transform>(flagE);
    flagT.position = {x, terrainH + 0.15f, z};
    flagT.scale    = {1.2f, 1.2f, 1.2f};

    auto& flagMr = ctx.world.Add<MeshRenderer>(flagE);
    flagMr.mesh   = ctx.resource.LoadMesh("Assets/models/flag.obj");
    flagMr.shader = ctx.resource.LoadShader(
        "Basic", L"Assets/shaders/BasicVS.hlsl",
        L"Assets/shaders/BasicPS.hlsl");

    if      (isTargetHole)              flagMr.color = {1.0f, 0.2f, 0.2f, 1.0f};
    else if (hopsToTarget == 1)         flagMr.color = {1.0f, 0.85f, 0.0f, 1.0f};
    else if (hopsToTarget == 2)         flagMr.color = {1.0f, 0.6f, 0.2f, 1.0f};
    else if (hopsToTarget >= 3 && hopsToTarget <= 5) flagMr.color = {0.95f, 0.95f, 0.95f, 1.0f};
    else if (hopsToTarget > 5)          flagMr.color = {0.6f, 0.6f, 0.6f, 1.0f};
    else                                flagMr.color = {0.5f, 0.5f, 0.5f, 1.0f};

    auto& flagTag = ctx.world.Add<HoleFlag>(flagE);
    flagTag.holeEntity = e;

    // --- ラベル ---
    auto labelE  = ctx.world.CreateEntity();
    auto& labelUI = ctx.world.Add<UIText>(labelE);
    labelUI.text  = isTargetHole ? L"🎯" : L"";
    labelUI.style = graphics::TextStyle::Guide();
    labelUI.style.fontSize = 24.0f;
    if      (isTargetHole)      labelUI.style.color = {1.0f, 0.3f, 0.3f, 1.0f};
    else if (hopsToTarget == 1) labelUI.style.color = {1.0f, 0.85f, 0.0f, 1.0f};
    else if (hopsToTarget == 2) labelUI.style.color = {1.0f, 0.6f, 0.2f, 1.0f};
    else                        labelUI.style.color = {0.9f, 0.9f, 0.9f, 1.0f};
    labelUI.visible = false;
    labelUI.layer   = 60;

    auto& label3D     = ctx.world.Add<World3DLabel>(labelE);
    label3D.worldPos  = {x, terrainH + 3.0f, z};
    label3D.uiTextEntity = labelE;
    label3D.offsetY   = 3.0f;
    label3D.visible   = true;
    h.labelEntity     = labelE;

    // --- 光柱（ターゲット・1ホップのみ） ---
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
        h.pillarEntity = static_cast<uint32_t>(pillarE);
    }

    state->holes.push_back(e);
    LOG_DEBUG("WikiPageLoader",
              "Hole created at ({:.1f},{:.1f}) target='{}' isTarget={} hops={}",
              x, z, linkTarget, isTargetHole, hopsToTarget);
}

// ============================================================
// CreateLinksFromTexture（将来的な直接呼び出し用）
// ============================================================
void WikiPageLoader::CreateLinksFromTexture(core::GameContext& ctx)
{
    if (!m_wikiTexture) return;

    auto* state = ctx.world.GetGlobal<GolfGameState>();
    if (!state) return;

    float texW = (float)m_wikiTexture->width;
    float texH = (float)m_wikiTexture->height;

    for (const auto& link : m_wikiTexture->links) {
        float cx = link.x + link.width  * 0.5f;
        float cy = link.y + link.height * 0.5f;
        float worldX = (cx / texW - 0.5f) * m_fieldWidth;
        float worldZ = (0.5f - cy / texH) * m_fieldDepth;

        bool isTarget = (link.targetPage == state->targetPage);

        int hops = -1;
        if (m_shortestPath && m_shortestPath->IsAvailable() &&
            state->targetPageId != -1) {
            auto r = m_shortestPath->FindShortestPath(
                link.targetPage, state->targetPageId, 6);
            if (r.success) hops = r.degrees;
        }
        CreateHole(ctx, worldX, worldZ, link.targetPage, isTarget, hops);
    }
}

} // namespace game::scenes
