#include "TutorialOverlayController.h"
#include "CameraController.h"
#include "ClubController.h"
#include "ShotController.h"
#include "MinimapController.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../audio/AudioSystem.h"
#include "../../ecs/World.h"

namespace game::controllers {

void TutorialOverlayController::Initialize(core::GameContext& ctx) {
    LOG_INFO("TutorialOverlay", "Initialize");

    m_step = TutorialStep::Camera;
    m_terrainEventStarted = false;
    m_terrainCardIndex = 0;
    m_terrainCardTimer = 0.0f;
    m_initialCameraYaw = 0.0f;
    m_initialClubIndex = 0;

    // 速度低下
    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed = 0.7f;
        shotState->impactGaugeSpeed = 0.9f;
    }

    // UI作成
    m_overlayBgEntity = ctx.world.CreateEntity();
    auto& bg = ctx.world.Add<components::UIText>(m_overlayBgEntity);
    bg.text = L"";
    bg.x = 240.0f; bg.y = 80.0f;
    bg.width = 800.0f; bg.height = 140.0f;
    bg.style.bgColor = {0.05f, 0.1f, 0.15f, 0.85f};
    bg.style.cornerRadius = 16.0f;
    bg.style.borderWidth = 2.0f;
    bg.style.borderColor = {0.8f, 0.7f, 0.3f, 1.0f};
    bg.layer = 200;
    bg.visible = true;

    m_overlayTextEntity = ctx.world.CreateEntity();
    auto& txt = ctx.world.Add<components::UIText>(m_overlayTextEntity);
    txt.text = L"チュートリアル開始";
    txt.x = 240.0f; txt.y = 110.0f;
    txt.width = 800.0f;
    txt.style.fontSize = 24.0f;
    txt.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
    txt.style.align = graphics::TextAlign::Center;
    txt.layer = 201;
    txt.visible = true;

    m_skipTextEntity = ctx.world.CreateEntity();
    auto& skip = ctx.world.Add<components::UIText>(m_skipTextEntity);
    skip.text = L"Enterキーでスキップ";
    skip.x = 240.0f; skip.y = 180.0f;
    skip.width = 780.0f;
    skip.style.fontSize = 18.0f;
    skip.style.color = {0.6f, 0.6f, 0.6f, 1.0f};
    skip.style.align = graphics::TextAlign::Right;
    skip.layer = 201;
    skip.visible = true;

    // 地形カード
    m_terrainCards = {
        { L"Fairway (フェアウェイ)", L"ボールが転がりやすい標準的な地形です。" },
        { L"Rough (ラフ)", L"草が深く、ボールの転がりが少し悪くなります。" },
        { L"Bunker (バンカー)", L"砂地です。転がりにくく、パワーも落ちやすくなります。" },
        { L"Green (グリーン)", L"カップ周りの滑らかな地形です。よく転がります。" },
        { L"OB / Water / Lava", L"水や溶岩などの危険エリア。入るとペナルティで1打戻されます。" }
    };

    UpdateUI(ctx);
}

void TutorialOverlayController::Shutdown(core::GameContext& ctx) {
    LOG_INFO("TutorialOverlay", "Shutdown");

    auto* shotState = ctx.world.GetGlobal<components::ShotState>();
    if (shotState) {
        shotState->powerGaugeSpeed = 1.5f;
        shotState->impactGaugeSpeed = 2.0f;
    }

    if (ctx.world.IsAlive(m_overlayBgEntity)) ctx.world.DestroyEntity(m_overlayBgEntity);
    if (ctx.world.IsAlive(m_overlayTextEntity)) ctx.world.DestroyEntity(m_overlayTextEntity);
    if (ctx.world.IsAlive(m_skipTextEntity)) ctx.world.DestroyEntity(m_skipTextEntity);
}

void TutorialOverlayController::Update(core::GameContext& ctx, 
                                       CameraController* cameraCtrl,
                                       ClubController* clubCtrl,
                                       ShotController* shotCtrl,
                                       MinimapController* minimapCtrl) {
    if (m_step == TutorialStep::Done) return;

    if (ctx.input.GetKeyDown(VK_RETURN)) {
        if (ctx.audio) ctx.audio->PlaySE(ctx, "se_shot_soft.mp3", 0.5f);
        NextStep(ctx);
        // Reset initialization variables so they re-trigger on the new step
        m_initialCameraYaw = 0.0f;
        m_initialClubIndex = -1;
        return;
    }

    CheckCompletion(ctx, cameraCtrl, clubCtrl, shotCtrl, minimapCtrl);
}

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
            if (cameraCtrl && std::abs(cameraCtrl->GetYaw() - m_initialCameraYaw) > 0.5f) { // roughly 30 degrees
                NextStep(ctx);
            }
            break;

        case TutorialStep::Club:
            if (m_initialClubIndex == -1 && clubCtrl) {
                m_initialClubIndex = clubCtrl->GetCurrentClubIndex();
            }
            if (clubCtrl && clubCtrl->GetCurrentClubIndex() != m_initialClubIndex && m_initialClubIndex != -1) {
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
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (!m_terrainEventStarted && golfState && golfState->isBallGrounded) {
                // ボールが停止したら地形イベント開始（今回はバンカーに入った前提）
                if (shotState && shotState->phase == components::ShotState::Phase::Idle) {
                    m_terrainEventStarted = true;
                    m_terrainCardTimer = 4.0f;
                    m_terrainCardIndex = 0;
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
            break;
        }

        case TutorialStep::CupIn: {
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (golfState && golfState->gameCleared) {
                NextStep(ctx);
            }
            break;
        }

        case TutorialStep::Done:
            break;
    }
}

void TutorialOverlayController::NextStep(core::GameContext& ctx) {
    int next = static_cast<int>(m_step) + 1;
    m_step = static_cast<TutorialStep>(next);

    // reset logic
    if (m_step == TutorialStep::Camera) m_initialCameraYaw = 0.0f;
    if (m_step == TutorialStep::Club) m_initialClubIndex = -1;

    UpdateUI(ctx);
}

void TutorialOverlayController::UpdateUI(core::GameContext& ctx) {
    if (m_step == TutorialStep::Done) {
        if (ctx.world.IsAlive(m_overlayBgEntity)) ctx.world.Get<components::UIText>(m_overlayBgEntity)->visible = false;
        if (ctx.world.IsAlive(m_overlayTextEntity)) ctx.world.Get<components::UIText>(m_overlayTextEntity)->visible = false;
        if (ctx.world.IsAlive(m_skipTextEntity)) ctx.world.Get<components::UIText>(m_skipTextEntity)->visible = false;
        return;
    }

    std::wstring text = L"";
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
            text = L"【STEP 5】地形とOBについて\n(ボールが停止するまでお待ちください)";
            if (m_terrainEventStarted && m_terrainCardIndex < m_terrainCards.size()) {
                text = L"【STEP 5】地形について学ぼう\n" + m_terrainCards[m_terrainCardIndex].name + L"\n" + m_terrainCards[m_terrainCardIndex].desc;
            }
            break;
        case TutorialStep::CupIn:
            text = L"【STEP 6】ミニマップ(Mキー)で旗の位置を確認し、\nカップインを目指しましょう！";
            break;
        default:
            break;
    }

    if (ctx.world.IsAlive(m_overlayTextEntity)) {
        ctx.world.Get<components::UIText>(m_overlayTextEntity)->text = text;
    }
}

} // namespace game::controllers
