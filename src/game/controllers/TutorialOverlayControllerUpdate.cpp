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
#include "../components/UIImage.h"
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
    switch (m_step) {
    case TutorialStep::Camera: policy.camera = true; break;
    case TutorialStep::Aim: policy.aimPin = true; break;
    case TutorialStep::Club: policy.club = true; break;
    case TutorialStep::Power:
    case TutorialStep::Impact: policy.shot = true; break;
    case TutorialStep::MapOpen:
    case TutorialStep::MapPan:
    case TutorialStep::MapZoom:
    case TutorialStep::MapAim:
    case TutorialStep::MapHelpOpen:
    case TutorialStep::MapHelpClose:
    case TutorialStep::MapClose:
        policy.map = true;
        if (m_step == TutorialStep::MapAim) policy.aimPin = true;
        break;
    case TutorialStep::LinkCup:
    case TutorialStep::GoalCup:
        policy.camera = true;
        policy.aimPin = true;
        policy.club = true;
        policy.shot = true;
        break;
    default: break;
    }
    return policy;
}

MinimapController::InputPermissions
TutorialOverlayController::GetMapInputPermissions() const {
    MinimapController::InputPermissions p;
    p.openWithM = false;
    p.closeWithEscape = false;
    p.panWithLeftDrag = false;
    p.panWithRightDrag = false;
    p.zoomWithWheel = false;
    p.zoomWithKeys = false;
    p.toggleHelp = false;
    p.recenter = false;
    p.fitCourse = false;
    p.resetZoom = false;
    p.openWithM = m_step == TutorialStep::MapOpen;
    p.panWithLeftDrag = m_step == TutorialStep::MapPan;
    p.zoomWithWheel = m_step == TutorialStep::MapZoom;
    p.toggleHelp = m_step == TutorialStep::MapHelpOpen ||
                   m_step == TutorialStep::MapHelpClose;
    p.closeWithEscape = m_step == TutorialStep::MapClose;
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
    if (m_step == TutorialStep::Done) return;

    if (m_stepClearPending) {
        m_checkMarkTimer += ctx.dt;
        UpdateStepClearAnim(ctx);
        if (m_checkMarkTimer >= 0.65f) {
            if (ctx.world.IsAlive(m_checkMarkEntity)) {
                ctx.world.DestroyEntity(m_checkMarkEntity);
                m_checkMarkEntity = UINT32_MAX;
            }
            m_stepClearPending = false;
            m_checkMarkShown = false;
            NextStep(ctx);
        }
        return;
    }

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
                TriggerStepClear(ctx);
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
            UpdateUI(ctx);
            return;
        }
        if (m_step == TutorialStep::GoalCup) {
            TriggerStepClear(ctx);
            return;
        }
        if (m_step >= TutorialStep::MapOpen && m_step <= TutorialStep::MapClose) {
            if (minimapCtrl && minimapCtrl->IsMapView()) {
                minimapCtrl->ToggleMapView(ctx, skyboxEntity);
            }
            m_step = TutorialStep::LinkCup;
            UpdateUI(ctx);
            return;
        }
        TriggerStepClear(ctx);
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
        if (cameraCtrl &&
            std::abs(cameraCtrl->GetYaw() - m_initialCameraYaw) > 0.25f &&
            std::abs(cameraCtrl->GetDistance() - m_initialCameraDistance) > 0.5f) {
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
    case TutorialStep::Impact:
        if (shot && shot->phase == components::ShotState::Phase::Idle) {
            m_step = TutorialStep::Power;
            UpdateUI(ctx);
        } else if (shot &&
                   (shot->phase == components::ShotState::Phase::Executing ||
                    shot->phase == components::ShotState::Phase::ShowResult)) {
            TriggerStepClear(ctx);
        }
        break;
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
    case TutorialStep::MapHelpOpen:
    case TutorialStep::MapHelpClose:
        if (minimapCtrl && minimapCtrl->GetInputActivity().helpToggled > m_mapActivityAtStep.helpToggled) {
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
    UpdateUI(ctx);
}

} // namespace game::controllers
