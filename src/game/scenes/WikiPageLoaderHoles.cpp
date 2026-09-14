/**
 * @file WikiPageLoaderHoles.cpp
 * @brief ホールとリンク領域のECS生成を実装します。
*/

#include "../../graphics/GraphicsDevice.h"
#include "WikiPageLoader.h"
#include "HoleVisualRules.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/WikiComponents.h"
#include "../controllers/MinimapController.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/GolfCupPhysics.h"
#include "../utils/ProceduralFlag.h"
#include <algorithm>
#include <cmath>

#undef min
#undef max

namespace game::scenes {

using namespace DirectX;
using namespace game::components;

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

    // ホール（カップ）を生成する。
    // 地表の円盤は置かず、地形・記事オーバーレイ・芝をシェーダーで円形に
    // くり抜き、その下にカップ内壁を描く。Transform は縁の中心を表す。
    const float rimY = game::physics::ToVisualSurfaceHeight(terrainH);
    const float cupRadius = game::physics::kGolfCupRadius;
    const float cupDepth = game::physics::kGolfCupDepth;
    auto e  = m_pageEntityOwner.Create(ctx.world);
    auto& t = ctx.world.Add<Transform>(e);
    t.position = {x, rimY, z};
    t.scale    = {cupRadius * 2.0f, cupDepth, cupRadius * 2.0f};

    auto& mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh   = ctx.resource.LoadMesh("builtin/golf_cup");
    mr.shader = ctx.resource.LoadShader(
        "Basic", L"Assets/shaders/BasicVS.hlsl",
        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
    // 内壁は近づかないと見えないため、遠距離では描画しない
    mr.maxDrawDistance = 90.0f;

    auto& h      = ctx.world.Add<GolfHole>(e);
    h.radius     = cupRadius;
    h.gravity    = kGolfHoleSuctionStrength;
    h.linkTarget  = linkTarget;
    h.isTarget   = isTargetHole;
    h.hopsToTarget = hopsToTarget;

    // すべてのリンクホールにプロシージャル旗を生成する
    // （遠距離のパーツはmaxDrawDistanceで描画カリングされるため負荷は限定的）
    {
        game::utils::ProceduralFlagOptions options;
        options.holeEntity = static_cast<uint32_t>(e);
        options.large = isTargetHole || hopsToTarget == 1;
        options.createParticles = (isTargetHole || hopsToTarget == 1);
        options.animationWeight = 0.72f;
        if (isTargetHole) {
            options.animationWeight = 1.0f;
        }

        auto flagColor = HoleVisualRules::GetColor(isTargetHole, hopsToTarget);
        // 旗竿はカップの底まで差し込む（穴を覗くと竿が底に立って見える）
        options.poleSinkDepth = cupDepth;
        auto flagResult = game::utils::CreateProceduralFlag(
            ctx, {x, rimY, z}, flagColor, options);
        h.pinRadius = flagResult.poleRadius;
        h.pinHeight = flagResult.poleHeight;

        for (const ecs::Entity flagEntity : flagResult.allEntities) {
            m_pageEntityOwner.Track(flagEntity);
        }

        // 生成されたすべてのパーティクルエンティティをホールのリストに追加
        for (auto flagEntity : flagResult.particleEntities) {
            h.particleEntities.push_back(static_cast<uint32_t>(flagEntity));
        }
    }

    // 目的記事の代表サムネイルをビルボード看板として表示する
    // （近隣ホールの看板はUpdateNearbyHoleSignboardsが遅延ロードで反映する）
    if (isTargetHole && m_hasTargetThumbnail) {
        h.signboardEntity = static_cast<uint32_t>(CreateHoleSignboardEntity(
            ctx, x, z, terrainH, isTargetHole, hopsToTarget,
            m_targetThumbnailSRV.Get(), m_targetThumbnailAspect));
    }

    // ラベルを生成
    auto labelE  = m_pageEntityOwner.Create(ctx.world);
    auto& labelUI = ctx.world.Add<UIText>(labelE);
    labelUI.text = L"";
    if (isTargetHole) {
        labelUI.text = L"";
    }
    labelUI.style = graphics::TextStyle::Guide();
    labelUI.style.fontSize = 24.0f;
    labelUI.style.color = HoleVisualRules::GetColor(isTargetHole, hopsToTarget);
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
        auto pillarE  = m_pageEntityOwner.Create(ctx.world);
        auto& pillarT = ctx.world.Add<Transform>(pillarE);
        float pillarH = 8.0f;
        if (isTargetHole) {
            pillarH = 15.0f;
        }
        pillarT.position = {x, terrainH + pillarH * 0.5f, z};
        pillarT.scale    = {0.3f, pillarH, 0.3f};

        auto& pillarMr = ctx.world.Add<MeshRenderer>(pillarE);
        pillarMr.mesh   = ctx.resource.LoadMesh("builtin/cylinder");
        pillarMr.shader = ctx.resource.LoadShader(
            "Basic", L"Assets/shaders/BasicVS.hlsl",
            L"Assets/shaders/BasicPS.hlsl");
        pillarMr.color = XMFLOAT4{1.0f, 0.85f, 0.2f, 0.3f};
        if (isTargetHole) {
            pillarMr.color = XMFLOAT4{1.0f, 0.3f, 0.3f, 0.4f};
        }
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


} // namespace game::scenes

