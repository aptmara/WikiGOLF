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

    m_step                = TutorialStep::Intro;
    m_terrainEventStarted = false;
    m_terrainCardIndex    = 0;
    m_initialCameraYaw    = 0.0f;
    m_initialCameraDistance = 0.0f;
    m_initialClubIndex    = 0;
    m_inputLocked         = false;
    m_visible             = true;
    m_eventCamLerpTimer   = 0.0f;
    m_eventCamDisplayTimer = 0.0f;
    m_cupInWaitTimer      = 0.0f;
    m_stepClearTimer      = 0.0f;
    m_stepClearPending    = false;
    m_spotlight           = game::utils::TutorialSpotlight::None;

    // チュートリアル中はゲージ速度を下げて操作しやすくする
    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed = 0.7f;
        shotState->impactGaugeSpeed = 0.9f;
    }

    m_panel.Initialize(ctx);
    UpdateUI(ctx);
}

void TutorialOverlayController::SetVisible(core::GameContext& ctx,
                                           bool visible) {
    m_visible = visible;
    m_panel.SetVisible(ctx, visible);
}

// -------------------------------------------------------
// Shutdown
// -------------------------------------------------------
void TutorialOverlayController::Shutdown(core::GameContext& ctx) {
    LOG_INFO("TutorialOverlay", "Shutdown");

    m_inputLocked = false;

    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed  = 1.0f;
        shotState->impactGaugeSpeed = 1.0f;
    }

    m_panel.Shutdown(ctx);
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
