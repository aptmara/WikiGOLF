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
// UpdateEventCamera（STEP 5 中に毎フレーム呼ぶ）
// -------------------------------------------------------
void TutorialOverlayController::UpdateEventCamera(core::GameContext& ctx) {
    if (m_cameraEntity == UINT32_MAX) return;
    const auto& targets = GetActiveEventCameraTargets();
    if (m_terrainCardIndex >= targets.size()) return;

    auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
    if (!camTr) return;

    const auto& target = targets[m_terrainCardIndex];

    // ラープ進捗（1.5倍速 → 約0.67秒で完了）
    m_eventCamLerpTimer = std::min(m_eventCamLerpTimer + ctx.dt * 1.5f, 1.0f);
    float t = m_eventCamLerpTimer;
    t = t * t * (3.0f - 2.0f * t); // smooth step

    // カメラ位置ラープ
    camTr->position.x = m_eventCamFromPos.x + (target.camPos.x - m_eventCamFromPos.x) * t;
    camTr->position.y = m_eventCamFromPos.y + (target.camPos.y - m_eventCamFromPos.y) * t;
    camTr->position.z = m_eventCamFromPos.z + (target.camPos.z - m_eventCamFromPos.z) * t;

    // 注視点へのLookAt
    XMVECTOR pos = XMLoadFloat3(&camTr->position);
    XMVECTOR at  = XMLoadFloat3(&target.focusPos);
    XMVECTOR up  = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMVECTOR dir = XMVectorSubtract(at, pos);
    if (XMVectorGetX(XMVector3LengthSq(dir)) < 0.001f) return;

    // LookAt から逆行列でカメラ回転を求める（LH座標系）
    XMMATRIX view = XMMatrixLookAtLH(pos, at, up);
    XMVECTOR rot  = XMQuaternionRotationMatrix(XMMatrixTranspose(view));
    XMStoreFloat4(&camTr->rotation, rot);

    // メインカメラであることを維持
    if (auto* cam = ctx.world.Get<components::Camera>(m_cameraEntity)) {
        cam->isMainCamera = true;
    }
}

// -------------------------------------------------------
// IsInputLocked
// -------------------------------------------------------
/**
 * @brief チュートリアル演出による入力ロック状態を返します。
 * @return イベントカメラ説明または説明間のチェック演出中ならtrueです。*/
bool TutorialOverlayController::IsInputLocked() const {
    if (m_inputLocked) return true;
    if (m_step == TutorialStep::FlagEvent && !m_flagEventCamTargets.empty()) {
        return true;
    }
    return m_stepClearPending &&
           (m_step == TutorialStep::TerrainEvent ||
            m_step == TutorialStep::FlagEvent);
}

// -------------------------------------------------------
// Update（毎フレーム）
// -------------------------------------------------------
void TutorialOverlayController::Update(core::GameContext& ctx,
                                       CameraController* cameraCtrl,
                                       ClubController* clubCtrl,
                                       ShotController* shotCtrl,
                                       MinimapController* minimapCtrl) {
    if (m_step == TutorialStep::Done) return;

    // Enter でスキップ
    if (ctx.input.GetKeyDown(VK_RETURN)) {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);

        // チェックマーク演出中なら即度次ステップへ
        if (m_stepClearPending) {
            if (ctx.world.IsAlive(m_checkMarkEntity)) {
                ctx.world.DestroyEntity(m_checkMarkEntity);
                m_checkMarkEntity = UINT32_MAX;
            }
            m_stepClearPending = false;
            m_checkMarkShown   = false;
            NextStep(ctx);
            m_initialCameraYaw = 0.0f;
            m_initialClubIndex = -1;
            return;
        }

        // イベントカメラ説明スキップ時はカメラロック解除
        if (m_step == TutorialStep::TerrainEvent ||
            m_step == TutorialStep::FlagEvent) {
            m_inputLocked         = false;
            m_terrainEventStarted = false;
        }

        NextStep(ctx);
        m_initialCameraYaw  = 0.0f;
        m_initialClubIndex  = -1;
        return;
    }

    // ステップクリア演出中（NextStep 待機）
    if (m_stepClearPending) {
        m_checkMarkTimer += ctx.dt;
        UpdateStepClearAnim(ctx);

        // CupIn は異なる待機時間（m_cupInWaitTimer で管理）
        if (m_step != TutorialStep::CupIn && m_checkMarkTimer >= 0.9f) {
            if (ctx.world.IsAlive(m_checkMarkEntity)) {
                ctx.world.DestroyEntity(m_checkMarkEntity);
                m_checkMarkEntity = UINT32_MAX;
            }
            m_stepClearPending = false;
            m_checkMarkShown   = false;
            NextStep(ctx);
        }
        // CupIn の終了判定は以下の CheckCompletion 内で行う
        if (m_step != TutorialStep::CupIn) return;
    }

    // イベントカメラ説明中はカメラを更新する
    if ((m_step == TutorialStep::TerrainEvent ||
         m_step == TutorialStep::FlagEvent) &&
        m_terrainEventStarted &&
        !GetActiveEventCameraTargets().empty()) {
        UpdateEventCamera(ctx);
    }

    CheckCompletion(ctx, cameraCtrl, clubCtrl, shotCtrl, minimapCtrl);
}

// -------------------------------------------------------
// CheckCompletion
// -------------------------------------------------------
void TutorialOverlayController::CheckCompletion(core::GameContext& ctx,
                                                CameraController* cameraCtrl,
                                                ClubController* clubCtrl,
                                                ShotController* shotCtrl,
                                                MinimapController* minimapCtrl) {
    auto* shotState = ctx.world.GetGlobal<components::ShotState>();

    switch (m_step) {
        case TutorialStep::Camera:
            if (m_initialCameraYaw == 0.0f && cameraCtrl) {
                m_initialCameraYaw = cameraCtrl->GetYaw();
            }
            if (cameraCtrl && std::abs(cameraCtrl->GetYaw() - m_initialCameraYaw) > 0.5f) {
                TriggerStepClear(ctx);
            }
            break;

        case TutorialStep::Club:
            if (m_initialClubIndex == -1 && clubCtrl) {
                m_initialClubIndex = clubCtrl->GetCurrentClubIndex();
            }
            if (clubCtrl && clubCtrl->GetCurrentClubIndex() != m_initialClubIndex
                && m_initialClubIndex != -1) {
                TriggerStepClear(ctx);
            }
            break;

        case TutorialStep::Power:
            if (shotState && shotState->phase == components::ShotState::Phase::ImpactTiming) {
                TriggerStepClear(ctx);
            }
            break;

        case TutorialStep::Impact:
            if (shotState && (shotState->phase == components::ShotState::Phase::Executing ||
                              shotState->phase == components::ShotState::Phase::ShowResult)) {
                TriggerStepClear(ctx);
            }
            break;

        case TutorialStep::TerrainEvent: {
            if (!m_eventCamTargets.empty()) {
                // === イベントカメラモード（targets が注入済み）===
                if (!m_terrainEventStarted) {
                    // ショット直後のボール飛行待機
                    //       ShotPhase が Idle かつボール速度が十分小さくなるまで待機する
                    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
                    bool shotIdle = !shotState ||
                                   shotState->phase == components::ShotState::Phase::Idle ||
                                   shotState->phase == components::ShotState::Phase::ShowResult;

                    bool ballStopped = true;
                    auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
                    if (golfState && golfState->ballEntity != UINT32_MAX) {
                        auto* rb = ctx.world.Get<components::RigidBody>(golfState->ballEntity);
                        if (rb) {
                            float spd = rb->velocity.x * rb->velocity.x
                                      + rb->velocity.y * rb->velocity.y
                                      + rb->velocity.z * rb->velocity.z;
                            ballStopped = (spd < 0.05f * 0.05f);
                        }
                    }

                    if (!shotIdle || !ballStopped) {
                        // まだ待機中 — UI はそのままで return
                        break;
                    }

                    m_terrainEventStarted  = true;
                    m_inputLocked          = true;
                    m_terrainCardIndex     = 0;
                    m_eventCamDisplayTimer = 4.0f;
                    m_eventCamLerpTimer    = 0.0f;

                    // 現在のカメラ位置からラープ開始
                    if (m_cameraEntity != UINT32_MAX) {
                        auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
                        if (camTr) m_eventCamFromPos = camTr->position;
                    }
                    UpdateUI(ctx);
                }

                m_eventCamDisplayTimer -= ctx.dt;
                if (m_eventCamDisplayTimer <= 0.0f) {
                    m_terrainCardIndex++;
                    if (m_terrainCardIndex >= m_eventCamTargets.size()) {
                        // 全地形を表示した → 次のステップへ（チェックマーク付き）
                        m_inputLocked = false;
                        TriggerStepClear(ctx);
                    } else {
                        // 次の地形へ：現在位置からラープ開始
                        m_eventCamDisplayTimer = 4.0f;
                        if (m_cameraEntity != UINT32_MAX) {
                            auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
                            if (camTr) m_eventCamFromPos = camTr->position;
                        }
                        m_eventCamLerpTimer = 0.0f;
                        UpdateUI(ctx);
                    }
                }
            } else {
                // === 旧動作フォールバック（WikiGolfScene 経由時など）===
                auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
                if (!m_terrainEventStarted && golfState && golfState->isBallGrounded) {
                    if (shotState && shotState->phase == components::ShotState::Phase::Idle) {
                        m_terrainEventStarted = true;
                        m_terrainCardTimer    = 4.0f;
                        m_terrainCardIndex    = 0;
                        UpdateUI(ctx);
                    }
                }
                if (m_terrainEventStarted) {
                    m_terrainCardTimer -= ctx.dt;
                    if (m_terrainCardTimer <= 0.0f) {
                        m_terrainCardTimer = 4.0f;
                        m_terrainCardIndex++;
                        if (m_terrainCardIndex >= m_terrainCards.size()) {
                            TriggerStepClear(ctx);
                        } else {
                            UpdateUI(ctx);
                        }
                    }
                }
            }
            break;
        }

        case TutorialStep::FlagEvent: {
            if (m_flagEventCamTargets.empty()) {
                TriggerStepClear(ctx);
                break;
            }

            if (!m_terrainEventStarted) {
                m_terrainEventStarted  = true;
                m_inputLocked          = true;
                m_terrainCardIndex     = 0;
                m_eventCamDisplayTimer = 4.0f;
                m_eventCamLerpTimer    = 0.0f;

                if (m_cameraEntity != UINT32_MAX) {
                    auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
                    if (camTr) m_eventCamFromPos = camTr->position;
                }
                UpdateUI(ctx);
            }

            m_eventCamDisplayTimer -= ctx.dt;
            if (m_eventCamDisplayTimer <= 0.0f) {
                m_terrainCardIndex++;
                if (m_terrainCardIndex >= m_flagEventCamTargets.size()) {
                    m_inputLocked = false;
                    TriggerStepClear(ctx);
                } else {
                    m_eventCamDisplayTimer = 4.0f;
                    if (m_cameraEntity != UINT32_MAX) {
                        auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
                        if (camTr) m_eventCamFromPos = camTr->position;
                    }
                    m_eventCamLerpTimer = 0.0f;
                    UpdateUI(ctx);
                }
            }
            break;
        }

        case TutorialStep::CupIn: {
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (golfState && golfState->gameCleared) {
                m_cupInWaitTimer += ctx.dt;

                // クリア確定時にチェックマーク演出を開始（未生成のときのみ）
                if (!m_checkMarkShown) {
                    TriggerStepClear(ctx);
                    m_stepClearPending = false; // CupIn は表示時間を別管理するのでフラグを戻す
                    m_checkMarkShown = true;    // 再度生成しないよう
                }

                // チェックマークアニメーション更新は UpdateStepClearAnim に委譲（タイマーは m_checkMarkTimer）
                m_checkMarkTimer += ctx.dt;
                UpdateStepClearAnim(ctx);

                UpdateUI(ctx);
                if (m_cupInWaitTimer >= 4.0f) {
                    // チェックマークエンティティを破棄してから次ステップへ
                    if (ctx.world.IsAlive(m_checkMarkEntity)) {
                        ctx.world.DestroyEntity(m_checkMarkEntity);
                        m_checkMarkEntity = UINT32_MAX;
                    }
                    NextStep(ctx);
                }
            }
            break;
        }

        case TutorialStep::Done:
            break;
    }
}

// -------------------------------------------------------
// NextStep
// -------------------------------------------------------
void TutorialOverlayController::NextStep(core::GameContext& ctx) {
    // TerrainEvent から脱出するときは必ずロックを解除
    if (m_step == TutorialStep::TerrainEvent) {
        m_inputLocked = false;

        // 打ってみようの時にボールが遠くに行き過ぎたり、不意にゴールに入ってしまった場合に備え、
        // ボールの位置・速度、およびクリアフラグをリセットする
        auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
        if (golfState) {
            golfState->gameCleared = false;

            if (golfState->ballEntity != UINT32_MAX) {
                if (auto* tr = ctx.world.Get<components::Transform>(golfState->ballEntity)) {
                    tr->position = {
                        0.0f,
                        game::physics::kTerrainVisualSurfaceOffset +
                            game::physics::kBallRadius,
                        -32.0f};
                }
                if (auto* rb = ctx.world.Get<components::RigidBody>(golfState->ballEntity)) {
                    rb->velocity = {0.0f, 0.0f, 0.0f};
                    rb->angularVelocity = {0.0f, 0.0f, 0.0f};
                }
            }
        }
    }

    if (m_step == TutorialStep::FlagEvent) {
        m_inputLocked = false;
    }

    int next = static_cast<int>(m_step) + 1;
    m_step   = static_cast<TutorialStep>(next);

    if (m_step == TutorialStep::TerrainEvent ||
        m_step == TutorialStep::FlagEvent) {
        m_terrainEventStarted = false;
        m_terrainCardIndex = 0;
        m_eventCamDisplayTimer = 0.0f;
        m_eventCamLerpTimer = 0.0f;
    }

    if (m_step == TutorialStep::Camera) m_initialCameraYaw  = 0.0f;
    if (m_step == TutorialStep::Club)   m_initialClubIndex  = -1;

    UpdateUI(ctx);
}

} // namespace game::controllers

