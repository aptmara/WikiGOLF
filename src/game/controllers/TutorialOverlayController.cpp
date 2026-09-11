/**
 * @file TutorialOverlayController.cpp
 * @brief WikiGolf チュートリアル進行管理実装
 *
 * 入力: GameContext・各コントローラーへのポインタ
 * 変更: チュートリアルステップ進行・UI 更新・STEP 5 イベントカメラ制御
 * 出力: IsDone()/IsInputLocked() の状態変化・カメラ Transform の強制更新
*/

#include "TutorialOverlayController.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "CameraController.h"
#include "ClubController.h"
#include "ShotController.h"
#include "MinimapController.h"
#include "hud/HudStyles.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../components/PhysicsComponents.h"
#include "../components/Camera.h"
#include "../utils/UIConstants.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../audio/AudioSystem.h"
#include "../../ecs/World.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>


namespace game::controllers {

using namespace DirectX;

// -------------------------------------------------------
// Initialize
// -------------------------------------------------------
void TutorialOverlayController::Initialize(core::GameContext& ctx) {
    LOG_INFO("TutorialOverlay", "Initialize");

    m_step                = TutorialStep::Intro;
    m_terrainEventStarted = false;
    m_terrainCardIndex    = 0;
    m_terrainCardTimer    = 0.0f;
    m_initialCameraYaw    = 0.0f;
    m_initialCameraDistance = 0.0f;
    m_initialClubIndex    = 0;
    m_inputLocked         = false;
    m_visible             = true;
    m_eventCamLerpTimer   = 0.0f;
    m_eventCamDisplayTimer = 0.0f;
    m_cupInWaitTimer      = 0.0f;
    m_checkMarkEntity     = UINT32_MAX;
    m_checkMarkShown      = false;
    m_checkMarkTimer      = 0.0f;
    m_stepClearPending    = false;

    // チュートリアル中はゲージ速度を下げて操作しやすくする
    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed = 0.7f;
        shotState->impactGaugeSpeed = 0.9f;
    }

    // オーバーレイ背景
    m_overlayBgEntity = m_entityOwner.Create(ctx.world);
    auto& bg = ctx.world.Add<components::UIText>(m_overlayBgEntity);
    bg.text = L"";
    bg.x = 310.0f; bg.y = 22.0f;
    bg.width = 660.0f; bg.height = 172.0f;
    hud::ApplySurfaceStyle(bg.style);
    bg.layer   = game::ui::kLayerOverlay;
    bg.visible = true;

    // オーバーレイテキスト
    m_overlayTextEntity = m_entityOwner.Create(ctx.world);
    auto& txt = ctx.world.Add<components::UIText>(m_overlayTextEntity);
    txt.text  = L"WIKIGOLF ガイド";
    txt.x = 338.0f; txt.y = 42.0f;
    txt.width = 604.0f;
    txt.style.fontSize = 20.0f;
    txt.style.color    = game::ui::kColorTextPrimary;
    txt.style.align    = graphics::TextAlign::Left;
    txt.layer   = game::ui::kLayerOverlay + 1;
    txt.visible = true;

    // 現在行う操作。スキップとは別の青い操作チップとして表示する。
    m_actionTextEntity = m_entityOwner.Create(ctx.world);
    auto& action = ctx.world.Add<components::UIText>(m_actionTextEntity);
    action.text = L"[ ENTER ] はじめる";
    action.x = 338.0f; action.y = 108.0f;
    action.width = 410.0f; action.height = 34.0f;
    action.style.fontSize = 15.0f;
    action.style.color = game::ui::kColorAccent;
    action.style.align = graphics::TextAlign::Center;
    hud::ApplyActiveRowStyle(action.style);
    action.layer = game::ui::kLayerOverlay + 1;
    action.visible = true;

    // 任意のスキップヒント
    m_skipTextEntity = m_entityOwner.Create(ctx.world);
    auto& skip = ctx.world.Add<components::UIText>(m_skipTextEntity);
    skip.text  = L"ENTER  次へ / スキップ";
    skip.x = 338.0f; skip.y = 150.0f;
    skip.width = 604.0f;
    skip.style.fontSize = 13.0f;
    skip.style.color    = game::ui::kColorAccent;
    skip.style.align    = graphics::TextAlign::Right;
    skip.layer   = game::ui::kLayerOverlay + 1;
    skip.visible = true;

    // 旧動作フォールバック用地形カード（m_eventCamTargets 未設定時に使う）
    m_terrainCards = {
        { L"Fairway (フェアウェイ)", L"ボールが転がりやすい標準的な地形です。" },
        { L"Rough (ラフ)",           L"草が深く、ボールの転がりが少し悪くなります。" },
        { L"Bunker (バンカー)",       L"砂地です。転がりにくく、パワーも落ちやすくなります。" },
        { L"Green (グリーン)",        L"カップ周りの滑らかな地形です。よく転がります。" },
        { L"OB / Water",             L"コース外や水に入るとOBです。1打加算され、打つ前の位置へ戻ります。" }
    };

    UpdateUI(ctx);
}

void TutorialOverlayController::SetVisible(core::GameContext& ctx,
                                           bool visible) {
    m_visible = visible;
    UpdateUI(ctx);
    if (auto* check = ctx.world.Get<components::UIImage>(m_checkMarkEntity)) {
        check->visible = visible;
    }
}

// -------------------------------------------------------
// Shutdown
// -------------------------------------------------------
void TutorialOverlayController::Shutdown(core::GameContext& ctx) {
    LOG_INFO("TutorialOverlay", "Shutdown");

    m_inputLocked = false;

    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed  = 1.5f;
        shotState->impactGaugeSpeed = 2.0f * (2.0f / 3.0f);
    }

    m_entityOwner.DestroyAll(ctx.world);
    m_overlayBgEntity = UINT32_MAX;
    m_overlayTextEntity = UINT32_MAX;
    m_actionTextEntity = UINT32_MAX;
    m_skipTextEntity = UINT32_MAX;
    m_checkMarkEntity = UINT32_MAX;
}

// -------------------------------------------------------
// SetEventCameraTargets
// -------------------------------------------------------
void TutorialOverlayController::SetEventCameraTargets(
    ecs::Entity cameraEntity,
    std::vector<EventCameraTarget> terrainTargets,
    std::vector<EventCameraTarget> flagTargets)
{
    m_cameraEntity          = cameraEntity;
    m_eventCamTargets       = std::move(terrainTargets);
    m_flagEventCamTargets   = std::move(flagTargets);
    LOG_INFO("TutorialOverlay", "EventCamera targets set: terrain={}, flag={}",
             m_eventCamTargets.size(), m_flagEventCamTargets.size());
}

} // namespace game::controllers
