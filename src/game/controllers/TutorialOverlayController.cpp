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
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../components/PhysicsComponents.h"
#include "../components/Camera.h"
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

    m_step                = TutorialStep::Camera;
    m_terrainEventStarted = false;
    m_terrainCardIndex    = 0;
    m_terrainCardTimer    = 0.0f;
    m_initialCameraYaw    = 0.0f;
    m_initialClubIndex    = 0;
    m_inputLocked         = false;
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
    bg.x = 240.0f; bg.y = 80.0f;
    bg.width = 800.0f; bg.height = 140.0f;
    bg.style.bgColor       = {0.05f, 0.1f, 0.15f, 0.85f};
    bg.style.cornerRadius  = 16.0f;
    bg.style.borderWidth   = 2.0f;
    bg.style.borderColor   = {0.8f, 0.7f, 0.3f, 1.0f};
    bg.layer   = 200;
    bg.visible = true;

    // オーバーレイテキスト
    m_overlayTextEntity = m_entityOwner.Create(ctx.world);
    auto& txt = ctx.world.Add<components::UIText>(m_overlayTextEntity);
    txt.text  = L"チュートリアル開始";
    txt.x = 240.0f; txt.y = 110.0f;
    txt.width = 800.0f;
    txt.style.fontSize = 24.0f;
    txt.style.color    = {1.0f, 1.0f, 1.0f, 1.0f};
    txt.style.align    = graphics::TextAlign::Center;
    txt.layer   = 201;
    txt.visible = true;

    // スキップヒント
    m_skipTextEntity = m_entityOwner.Create(ctx.world);
    auto& skip = ctx.world.Add<components::UIText>(m_skipTextEntity);
    skip.text  = L"Enterキーでスキップ";
    skip.x = 240.0f; skip.y = 180.0f;
    skip.width = 780.0f;
    skip.style.fontSize = 18.0f;
    skip.style.color    = {0.6f, 0.6f, 0.6f, 1.0f};
    skip.style.align    = graphics::TextAlign::Right;
    skip.layer   = 201;
    skip.visible = true;

    // 旧動作フォールバック用地形カード（m_eventCamTargets 未設定時に使う）
    m_terrainCards = {
        { L"Fairway (フェアウェイ)", L"ボールが転がりやすい標準的な地形です。" },
        { L"Rough (ラフ)",           L"草が深く、ボールの転がりが少し悪くなります。" },
        { L"Bunker (バンカー)",       L"砂地です。転がりにくく、パワーも落ちやすくなります。" },
        { L"Green (グリーン)",        L"カップ周りの滑らかな地形です。よく転がります。" },
        { L"OB / Water / Lava",      L"水や溶岩などの危険エリア。入るとペナルティで1打戻されます。" }
    };

    UpdateUI(ctx);
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
