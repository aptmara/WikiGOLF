/**
 * @file TutorialOverlayController.cpp
 * @brief WikiGolf チュートリアル進行管理実装
 *
 * 入力: GameContext・各コントローラーへのポインタ
 * 変更: チュートリアルステップ進行・UI 更新・STEP 5 イベントカメラ制御
 * 出力: IsDone()/IsInputLocked() の状態変化・カメラ Transform の強制更新
 */

#include "TutorialOverlayController.h"
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

    // チュートリアル中はゲージ速度を下げて操作しやすくする
    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed = 0.7f;
        shotState->impactGaugeSpeed = 0.9f;
    }

    // オーバーレイ背景
    m_overlayBgEntity = ctx.world.CreateEntity();
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
    m_overlayTextEntity = ctx.world.CreateEntity();
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
    m_skipTextEntity = ctx.world.CreateEntity();
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
        shotState->impactGaugeSpeed = 2.0f;
    }

    if (ctx.world.IsAlive(m_overlayBgEntity))   ctx.world.DestroyEntity(m_overlayBgEntity);
    if (ctx.world.IsAlive(m_overlayTextEntity)) ctx.world.DestroyEntity(m_overlayTextEntity);
    if (ctx.world.IsAlive(m_skipTextEntity))    ctx.world.DestroyEntity(m_skipTextEntity);
}

// -------------------------------------------------------
// SetEventCameraTargets
// -------------------------------------------------------
void TutorialOverlayController::SetEventCameraTargets(
    ecs::Entity cameraEntity,
    std::vector<EventCameraTarget> targets)
{
    m_cameraEntity    = cameraEntity;
    m_eventCamTargets = std::move(targets);
    LOG_INFO("TutorialOverlay", "EventCamera targets set: {}",
             m_eventCamTargets.size());
}

// -------------------------------------------------------
// UpdateEventCamera（STEP 5 中に毎フレーム呼ぶ）
// -------------------------------------------------------
void TutorialOverlayController::UpdateEventCamera(core::GameContext& ctx) {
    if (m_cameraEntity == UINT32_MAX) return;
    if (m_terrainCardIndex >= m_eventCamTargets.size()) return;

    auto* camTr = ctx.world.Get<components::Transform>(m_cameraEntity);
    if (!camTr) return;

    const auto& target = m_eventCamTargets[m_terrainCardIndex];

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

        // TerrainEvent スキップ時はカメラロック解除
        if (m_step == TutorialStep::TerrainEvent) {
            m_inputLocked         = false;
            m_terrainEventStarted = false;
        }

        NextStep(ctx);
        m_initialCameraYaw  = 0.0f;
        m_initialClubIndex  = -1;
        return;
    }

    // STEP 5 中はイベントカメラを更新する
    if (m_step == TutorialStep::TerrainEvent &&
        m_terrainEventStarted &&
        !m_eventCamTargets.empty()) {
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
                NextStep(ctx);
            }
            break;

        case TutorialStep::Club:
            if (m_initialClubIndex == -1 && clubCtrl) {
                m_initialClubIndex = clubCtrl->GetCurrentClubIndex();
            }
            if (clubCtrl && clubCtrl->GetCurrentClubIndex() != m_initialClubIndex
                && m_initialClubIndex != -1) {
                NextStep(ctx);
            }
            break;

        case TutorialStep::Power:
            if (shotState && shotState->phase == components::ShotState::Phase::ImpactTiming) {
                NextStep(ctx);
            }
            break;

        case TutorialStep::Impact:
            if (shotState && (shotState->phase == components::ShotState::Phase::Executing ||
                              shotState->phase == components::ShotState::Phase::ShowResult)) {
                NextStep(ctx);
            }
            break;

        case TutorialStep::TerrainEvent: {
            if (!m_eventCamTargets.empty()) {
                // === イベントカメラモード（targets が注入済み）===
                if (!m_terrainEventStarted) {
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
                        // 全地形を表示した → 次のステップへ
                        NextStep(ctx);
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
                            NextStep(ctx);
                        } else {
                            UpdateUI(ctx);
                        }
                    }
                }
            }
            break;
        }

        case TutorialStep::CupIn: {
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (golfState && golfState->gameCleared) {
                m_cupInWaitTimer += ctx.dt;
                UpdateUI(ctx);
                if (m_cupInWaitTimer >= 4.0f) {
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
                    tr->position = {0.0f, 0.022f, -32.0f}; // ティー位置へリセット
                }
                if (auto* rb = ctx.world.Get<components::RigidBody>(golfState->ballEntity)) {
                    rb->velocity = {0.0f, 0.0f, 0.0f};
                    rb->angularVelocity = {0.0f, 0.0f, 0.0f};
                }
            }
        }
    }

    int next = static_cast<int>(m_step) + 1;
    m_step   = static_cast<TutorialStep>(next);

    if (m_step == TutorialStep::Camera) m_initialCameraYaw  = 0.0f;
    if (m_step == TutorialStep::Club)   m_initialClubIndex  = -1;

    UpdateUI(ctx);
}

// -------------------------------------------------------
// UpdateUI
// -------------------------------------------------------
void TutorialOverlayController::UpdateUI(core::GameContext& ctx) {
    if (m_step == TutorialStep::Done) {
        if (ctx.world.IsAlive(m_overlayBgEntity))
            ctx.world.Get<components::UIText>(m_overlayBgEntity)->visible = false;
        if (ctx.world.IsAlive(m_overlayTextEntity))
            ctx.world.Get<components::UIText>(m_overlayTextEntity)->visible = false;
        if (ctx.world.IsAlive(m_skipTextEntity))
            ctx.world.Get<components::UIText>(m_skipTextEntity)->visible = false;
        return;
    }

    std::wstring text;
    switch (m_step) {
        case TutorialStep::Camera:
            text = L"【STEP 1】マウスの中ボタンをドラッグして、\nカメラを回して周りを見てみましょう。";
            break;
        case TutorialStep::Club:
            text = L"【STEP 2】QキーとEキーを押して、\n使用するクラブを変更してみましょう。";
            break;
        case TutorialStep::Power:
            text = L"【STEP 3】左クリックでパワーゲージのチャージを開始します。\nもう一度左クリックでパワーを決定します。";
            break;
        case TutorialStep::Impact:
            text = L"【STEP 4】ゲージが戻ってきます。中央の白いゾーン（Special）を\n狙って左クリックし、ショットを打ちます！";
            break;
        case TutorialStep::TerrainEvent:
            if (!m_eventCamTargets.empty()) {
                // イベントカメラモード
                if (m_terrainEventStarted && m_terrainCardIndex < m_eventCamTargets.size()) {
                    auto& t = m_eventCamTargets[m_terrainCardIndex];
                    text = L"【STEP 5】" + t.name + L"\n" + t.desc;
                } else {
                    text = L"【STEP 5】地形とOBについて";
                }
            } else {
                // 旧動作
                if (m_terrainEventStarted && m_terrainCardIndex < m_terrainCards.size()) {
                    text = L"【STEP 5】地形について学ぼう\n" +
                           m_terrainCards[m_terrainCardIndex].name + L"\n" +
                           m_terrainCards[m_terrainCardIndex].desc;
                } else {
                    text = L"【STEP 5】地形とOBについて\n(ボールが停止するまでお待ちください)";
                }
            }
            break;
        case TutorialStep::CupIn: {
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (golfState && golfState->gameCleared) {
                text = L"【TUTORIAL CLEAR!!】\nチュートリアル完了です！\n(まもなくタイトルへ戻ります)";
            } else {
                text = L"【STEP 6】旗の位置を確認し、\nカップインを目指しましょう！";
            }
            break;
        }
        default:
            break;
    }

    if (ctx.world.IsAlive(m_overlayTextEntity)) {
        ctx.world.Get<components::UIText>(m_overlayTextEntity)->text = text;
    }
}

} // namespace game::controllers
