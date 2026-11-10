/**
 * @file TutorialOverlayControllerUpdate.cpp
 * @brief チュートリアルの状態遷移と入力許可を実装します。
 */

#include "TutorialOverlayController.h"
#include "CameraController.h"
#include "ClubController.h"
#include "ShotController.h"
#include "../components/Camera.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../../audio/AudioSystem.h"
#include "../../core/Input.h"
#include "../../ecs/World.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {
namespace {

void ResetPracticeShot(core::GameContext& ctx) {
    if (auto* shot = ctx.world.GetGlobal<components::ShotState>()) shot->Reset();
    if (auto* state = ctx.world.GetGlobal<components::GolfGameState>()) {
        state->canShoot = true;
        state->gameCleared = false;
        state->isBallGrounded = true;
        if (auto* tr = ctx.world.Get<components::Transform>(state->ballEntity)) {
            tr->position = {
                0.0f,
                game::physics::kTerrainVisualSurfaceOffset +
                    game::physics::kBallRadius,
                -32.0f};
        }
        if (auto* rb = ctx.world.Get<components::RigidBody>(state->ballEntity)) {
            rb->velocity = {0.0f, 0.0f, 0.0f};
            rb->angularVelocity = {0.0f, 0.0f, 0.0f};
        }
    }
    if (auto* pin = ctx.world.GetGlobal<components::AimPinState>()) {
        pin->active = false;
    }
}

} // namespace

TutorialInputPolicy TutorialOverlayController::GetInputPolicy() const {
    TutorialInputPolicy policy;
    // 地形・旗の説明中は、イベントカメラが説明する場所を映す。ここで操作を
    // 許すと、通常のカメラがボールの後ろへ書き戻してしまい、クリックで
    // ショットも始まってしまう。
    if (m_step == TutorialStep::Intro || m_step == TutorialStep::Done ||
        m_step == TutorialStep::TerrainInfo ||
        m_step == TutorialStep::FlagInfo) {
        return policy;
    }
    const int step = static_cast<int>(m_step);
    policy.camera = step >= static_cast<int>(TutorialStep::Camera);
    policy.aimPin = step >= static_cast<int>(TutorialStep::Aim);
    policy.club = step >= static_cast<int>(TutorialStep::Club);
    policy.shot = step >= static_cast<int>(TutorialStep::Power);
    policy.map = step >= static_cast<int>(TutorialStep::MapOpen);
    return policy;
}

TutorialPresentationPolicy
TutorialOverlayController::GetPresentationPolicy() const {
    TutorialPresentationPolicy policy;
    if (m_step == TutorialStep::Done) {
        return policy;
    }
    const int step = static_cast<int>(m_step);
    policy.club = step >= static_cast<int>(TutorialStep::Club);
    policy.minimap = step >= static_cast<int>(TutorialStep::Aim);
    policy.courseInfo = step >= static_cast<int>(TutorialStep::LinkCup);
    return policy;
}

MinimapController::InputPermissions
TutorialOverlayController::GetMapInputPermissions() const {
    MinimapController::InputPermissions p;
    p.openWithM = false;
    p.closeWithM = false;
    p.closeWithEscape = false;
    p.panWithLeftDrag = false;
    p.panWithRightDrag = false;
    p.zoomWithWheel = false;
    p.zoomWithKeys = false;
    p.toggleHelp = false;
    p.recenter = false;
    p.fitCourse = false;
    p.resetZoom = false;
    const int step = static_cast<int>(m_step);
    const bool mapIntroduced =
        step >= static_cast<int>(TutorialStep::MapOpen) &&
        m_step != TutorialStep::Done;
    p.openWithM = mapIntroduced;
    p.closeWithM = mapIntroduced;
    p.panWithLeftDrag = step >= static_cast<int>(TutorialStep::MapPan);
    p.panWithRightDrag = p.panWithLeftDrag;
    p.zoomWithWheel = step >= static_cast<int>(TutorialStep::MapZoom);
    p.toggleHelp = step >= static_cast<int>(TutorialStep::MapHelp);
    p.recenter = p.toggleHelp;
    p.fitCourse = p.recenter;
    p.resetZoom = p.recenter;
    p.closeWithEscape = step >= static_cast<int>(TutorialStep::MapClose);
    // 案内パネルの上で押したボタンでは、マップを動かさない。
    if (m_panel.IsCapturingPointer()) {
        p.panWithLeftDrag = false;
        p.panWithRightDrag = false;
    }
    return p;
}

bool TutorialOverlayController::CanAcceptCupIn(
    const std::string& linkTarget, bool isTarget) const {
    if (m_step == TutorialStep::LinkCup) {
        return !isTarget && linkTarget == "フェアウェイ";
    }
    return m_step == TutorialStep::GoalCup && isTarget;
}

void TutorialOverlayController::NotifyLinkCupIn(core::GameContext& ctx) {
    if (m_step == TutorialStep::LinkCup) TriggerStepClear(ctx);
}

void TutorialOverlayController::NotifyWrongCupIn(core::GameContext& ctx,
                                                 const std::wstring& name) {
    if (m_step != TutorialStep::LinkCup && m_step != TutorialStep::GoalCup) {
        return;
    }
    m_wrongCupName = name;
    UpdateUI(ctx);
    if (ctx.audio) ctx.audio->PlaySE(ctx, "se_cancel.mp3", 0.6f);
}

void TutorialOverlayController::UpdateEventCamera(core::GameContext& ctx) {
    if (m_cameraEntity == UINT32_MAX) return;
    const auto& targets = GetActiveEventCameraTargets();
    if (m_terrainCardIndex >= targets.size()) return;
    auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
    if (!camTr) return;

    const auto& target = targets[m_terrainCardIndex];
    m_eventCamLerpTimer = std::min(m_eventCamLerpTimer + ctx.dt * 1.5f, 1.0f);
    float t = m_eventCamLerpTimer;
    t = t * t * (3.0f - 2.0f * t);
    camTr->position.x = m_eventCamFromPos.x + (target.camPos.x - m_eventCamFromPos.x) * t;
    camTr->position.y = m_eventCamFromPos.y + (target.camPos.y - m_eventCamFromPos.y) * t;
    camTr->position.z = m_eventCamFromPos.z + (target.camPos.z - m_eventCamFromPos.z) * t;

    using namespace DirectX;
    const XMVECTOR pos = XMLoadFloat3(&camTr->position);
    const XMVECTOR at = XMLoadFloat3(&target.focusPos);
    const XMVECTOR dir = XMVectorSubtract(at, pos);
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 0.001f) return;
    const XMMATRIX view = XMMatrixLookAtLH(
        pos, at, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    const XMVECTOR rot = XMQuaternionRotationMatrix(XMMatrixTranspose(view));
    XMStoreFloat4(&camTr->rotation, rot);
    if (auto* cam = ctx.world.Get<components::Camera>(m_cameraEntity)) {
        cam->isMainCamera = true;
    }
}

bool TutorialOverlayController::IsInputLocked() const {
    const auto p = GetInputPolicy();
    return !p.camera && !p.aimPin && !p.club && !p.shot && !p.map;
}

void TutorialOverlayController::Update(core::GameContext& ctx,
                                       CameraController* cameraCtrl,
                                       ClubController* clubCtrl,
                                       ShotController* shotCtrl,
                                       MinimapController* minimapCtrl,
                                       ecs::Entity skyboxEntity) {
    m_panel.Update(ctx);
    if (m_step == TutorialStep::Done) return;

    if (m_stepClearPending) {
        m_stepClearTimer += ctx.dt;
        if (m_stepClearTimer >= hud::TutorialGuidePanel::kStepClearSeconds) {
            m_stepClearPending = false;
            NextStep(ctx);
        }
        return;
    }
    UpdateSpotlight(ctx, minimapCtrl);

    const bool info = m_step == TutorialStep::TerrainInfo ||
                      m_step == TutorialStep::FlagInfo;
    if (info && !m_terrainEventStarted) {
        m_terrainEventStarted = true;
        m_terrainCardIndex = 0;
        m_eventCamLerpTimer = 0.0f;
        if (m_cameraEntity != UINT32_MAX) {
            if (auto* tr = ctx.world.Get<components::Transform>(m_cameraEntity)) {
                m_eventCamFromPos = tr->position;
            }
        }
        UpdateUI(ctx);
    }
    if (info) UpdateEventCamera(ctx);

    if (ctx.input.GetKeyDown(VK_RETURN)) {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        if (info) {
            const auto& targets = GetActiveEventCameraTargets();
            ++m_terrainCardIndex;
            if (m_terrainCardIndex >= targets.size()) {
                TriggerStepClear(ctx, false);
            } else {
                if (m_cameraEntity != UINT32_MAX) {
                    if (auto* tr = ctx.world.Get<components::Transform>(m_cameraEntity)) {
                        m_eventCamFromPos = tr->position;
                    }
                }
                m_eventCamLerpTimer = 0.0f;
                UpdateUI(ctx);
            }
            return;
        }
        if (m_step == TutorialStep::Power || m_step == TutorialStep::Impact) {
            ResetPracticeShot(ctx);
            m_step = TutorialStep::TerrainInfo;
            m_terrainEventStarted = false;
            m_practiceShotLaunched = false;
            UpdateUI(ctx);
            return;
        }
        if (m_step == TutorialStep::GoalCup) {
            // 最後の画面は Enter で終えない（直前の画面を飛ばすつもりの
            // 2回押しで、ゴールに入れないまま終わってしまうため）。
            // やめるときは Esc のポーズから。
            return;
        }
        if (m_step >= TutorialStep::MapOpen && m_step <= TutorialStep::MapClose) {
            if (minimapCtrl && minimapCtrl->IsMapView()) {
                minimapCtrl->ToggleMapView(ctx, skyboxEntity);
            }
            m_step = TutorialStep::LinkCup;
            m_wrongCupName.clear();
            UpdateUI(ctx);
            return;
        }
        TriggerStepClear(ctx, false);
        return;
    }

    CheckCompletion(ctx, cameraCtrl, clubCtrl, shotCtrl, minimapCtrl);
}

void TutorialOverlayController::CheckCompletion(core::GameContext& ctx,
                                                CameraController* cameraCtrl,
                                                ClubController* clubCtrl,
                                                ShotController*,
                                                MinimapController* minimapCtrl) {
    auto* shot = ctx.world.GetGlobal<components::ShotState>();
    switch (m_step) {
    case TutorialStep::Camera:
        if (m_initialCameraDistance == 0.0f && cameraCtrl) {
            m_initialCameraYaw = cameraCtrl->GetYaw();
            m_initialCameraDistance = cameraCtrl->GetDistance();
        }
        // 回せば次へ進む（寄る・引くは試せることを伝えるだけにする）。
        if (cameraCtrl &&
            std::abs(cameraCtrl->GetYaw() - m_initialCameraYaw) > 0.25f) {
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::Aim: {
        const auto* pin = ctx.world.GetGlobal<components::AimPinState>();
        if (pin && pin->active) TriggerStepClear(ctx);
        break;
    }
    case TutorialStep::Club:
        if (m_initialClubIndex < 0 && clubCtrl)
            m_initialClubIndex = clubCtrl->GetCurrentClubIndex();
        if (clubCtrl && m_initialClubIndex >= 0 &&
            clubCtrl->GetCurrentClubIndex() != m_initialClubIndex)
            TriggerStepClear(ctx);
        break;
    case TutorialStep::Power:
        if (shot && shot->phase == components::ShotState::Phase::ImpactTiming)
            TriggerStepClear(ctx);
        break;
    case TutorialStep::Impact: {
        if (!shot) break;
        using Phase = components::ShotState::Phase;
        if (shot->phase == Phase::Executing || shot->phase == Phase::ShowResult) {
            m_practiceShotLaunched = true;
        }
        // 初めての1打は、球が止まって判定が出るまで見せてから次へ進む。
        const bool resultShown = shot->phase == Phase::RestoringCamera ||
                                 shot->phase == Phase::Idle;
        if (m_practiceShotLaunched && resultShown) {
            TriggerStepClear(ctx);
        } else if (!m_practiceShotLaunched && shot->phase == Phase::Idle) {
            // 右クリックで強さからやり直した
            m_step = TutorialStep::Power;
            UpdateUI(ctx);
        }
        break;
    }
    case TutorialStep::MapOpen:
        if (minimapCtrl && minimapCtrl->GetInputActivity().opened > m_mapActivityAtStep.opened) {
            m_mapActivityAtStep = minimapCtrl->GetInputActivity();
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::MapPan:
        if (minimapCtrl && minimapCtrl->GetInputActivity().panned > m_mapActivityAtStep.panned) {
            m_mapActivityAtStep = minimapCtrl->GetInputActivity();
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::MapZoom:
        if (minimapCtrl && minimapCtrl->GetInputActivity().wheelZoomed > m_mapActivityAtStep.wheelZoomed) {
            m_mapActivityAtStep = minimapCtrl->GetInputActivity();
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::MapAim: {
        const auto* pin = ctx.world.GetGlobal<components::AimPinState>();
        if (pin && pin->active) TriggerStepClear(ctx);
        break;
    }
    case TutorialStep::MapHelp:
        // 開いて、閉じたら次へ（2回切り替えたら）。
        if (minimapCtrl && minimapCtrl->GetInputActivity().helpToggled >=
                               m_mapActivityAtStep.helpToggled + 2) {
            m_mapActivityAtStep = minimapCtrl->GetInputActivity();
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::MapClose:
        if (minimapCtrl && minimapCtrl->GetInputActivity().closed > m_mapActivityAtStep.closed) {
            m_mapActivityAtStep = minimapCtrl->GetInputActivity();
            TriggerStepClear(ctx);
        }
        break;
    case TutorialStep::GoalCup: {
        const auto* state = ctx.world.GetGlobal<components::GolfGameState>();
        if (state && state->gameCleared) {
            m_cupInWaitTimer += ctx.dt;
            UpdateUI(ctx);
            if (m_cupInWaitTimer >= 2.0f) NextStep(ctx);
        }
        break;
    }
    default: break;
    }
}

void TutorialOverlayController::NextStep(core::GameContext& ctx) {
    m_step = static_cast<TutorialStep>(static_cast<int>(m_step) + 1);
    if (m_step == TutorialStep::TerrainInfo) {
        ResetPracticeShot(ctx);
    }
    m_terrainEventStarted = false;
    m_terrainCardIndex = 0;
    m_eventCamLerpTimer = 0.0f;
    m_initialCameraYaw = 0.0f;
    m_initialCameraDistance = 0.0f;
    m_initialClubIndex = -1;
    m_cupInWaitTimer = 0.0f;
    m_practiceShotLaunched = false;
    m_wrongCupName.clear();
    UpdateUI(ctx);
}

} // namespace game::controllers

namespace game::controllers {

void TutorialOverlayController::UpdateSpotlight(core::GameContext& ctx,
                                                MinimapController* minimapCtrl) {
    using game::utils::TutorialSpotlight;
    TutorialSpotlight target = m_spotlight;
    const bool mapView = minimapCtrl && minimapCtrl->IsMapView();
    // 全体マップ中は、ミニマップやクラブ表示の位置に部品が無い。
    if (mapView) {
        target = TutorialSpotlight::None;
    }
    // 定規はスイングを始めるまで出ていないので、出てから示す。
    if (target == TutorialSpotlight::ShotRuler) {
        const auto* shot = ctx.world.GetGlobal<components::ShotState>();
        const bool swinging =
            shot && (shot->phase == components::ShotState::Phase::PowerCharging ||
                     shot->phase == components::ShotState::Phase::ImpactTiming);
        if (!swinging) {
            target = TutorialSpotlight::None;
        }
    }
    m_panel.SetSpotlight(ctx, target);
}

} // namespace game::controllers
