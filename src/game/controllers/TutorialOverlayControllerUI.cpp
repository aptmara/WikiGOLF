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
            text = L"【STEP 1】マウスの左ボタンか右ボタンをドラッグして、\nカメラを回して周りを見てみましょう。";
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
        case TutorialStep::FlagEvent:
            if (!m_flagEventCamTargets.empty()) {
                if (m_terrainEventStarted &&
                    m_terrainCardIndex < m_flagEventCamTargets.size()) {
                    auto& t = m_flagEventCamTargets[m_terrainCardIndex];
                    text = L"【STEP 6】" + t.name + L"\n" + t.desc;
                } else {
                    text = L"【STEP 6】旗の色と意味について";
                }
            } else {
                text = L"【STEP 6】旗の色と意味について";
            }
            break;
        case TutorialStep::CupIn: {
            auto* golfState = ctx.world.GetGlobal<components::GolfGameState>();
            if (golfState && golfState->gameCleared) {
                text = L"【TUTORIAL CLEAR!!】\nチュートリアル完了です！\n(まもなくタイトルへ戻ります)";
            } else {
                text = L"【STEP 7】旗の位置を確認し、\nカップインを目指しましょう！";
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

// -------------------------------------------------------
// GetActiveEventCameraTargets
// -------------------------------------------------------
/**
 * @brief 現在ステップのイベントカメラターゲット一覧を返します。
 * @return 地形説明中は地形ターゲット、旗説明中は旗ターゲット、それ以外は空配列です。*/
const std::vector<TutorialOverlayController::EventCameraTarget>&
TutorialOverlayController::GetActiveEventCameraTargets() const {
    static const std::vector<EventCameraTarget> kEmptyTargets;
    if (m_step == TutorialStep::TerrainEvent) {
        return m_eventCamTargets;
    }
    if (m_step == TutorialStep::FlagEvent) {
        return m_flagEventCamTargets;
    }
    return kEmptyTargets;
}

// -------------------------------------------------------
// TriggerStepClear（ステップ完了チェックマーク開始）
// -------------------------------------------------------
/**
 * @brief ステップ完了時にチェックマーク演出を開始する。
 * @details チェックマーク UIImage を生成し m_stepClearPending = true にする。
 *          呼び出し元は NextStep を直接呼ばずこの関数を使う。
*/
void TutorialOverlayController::TriggerStepClear(core::GameContext& ctx) {
    // 既存のチェックマークがあれば破棄
    if (ctx.world.IsAlive(m_checkMarkEntity)) {
        ctx.world.DestroyEntity(m_checkMarkEntity);
    }

    m_checkMarkEntity  = m_entityOwner.Create(ctx.world);
    m_checkMarkTimer   = 0.0f;
    m_stepClearPending = true;

    auto& img       = ctx.world.Add<components::UIImage>(m_checkMarkEntity);
    img.texturePath = "mark_check.png";
    // チュートリアルオーバーレイBG 中央（x=640, y=150）に配置
    // BG は x=240, y=80, w=800, h=140 → 中心 (640, 150)
    img.x       = 640.0f;
    img.y       = 80.0f;  // sz=0 の初期値; UpdateStepClearAnim で sz を足して補正
    img.width   = 0.0f;
    img.height  = 0.0f;
    img.alpha   = 1.0f;
    img.visible = true;
    img.layer   = 210;
}

// -------------------------------------------------------
// UpdateStepClearAnim（チェックマークアニメーション更新）
// -------------------------------------------------------
/**
 * @brief チェックマーク UIImage のサイズ・位置・透明度を毎フレーム更新する。
 * @details タイマー m_checkMarkTimer を参照する（更新は呼び出し元が行う）。
 *          CupIn 用に 4 秒表示・フェードアウトにも対応。
 *          通常ステップ用（0.9 秒）はフェードなし（破棄で消す）。
*/
void TutorialOverlayController::UpdateStepClearAnim(core::GameContext& ctx) {
    if (!ctx.world.IsAlive(m_checkMarkEntity)) return;
    auto* img = ctx.world.Get<components::UIImage>(m_checkMarkEntity);
    if (!img) return;

    // アニメーションパラメータ
    constexpr float kExpandEnd = 0.30f;  // 0 → 0.30s: ease-out 拡大
    constexpr float kShrinkEnd = 0.50f;  // 0.30 → 0.50s: ease-in 縮小
    constexpr float kMaxSize   = 200.0f;
    constexpr float kFinalSize = 150.0f;
    // CupIn 専用フェードアウト開始タイミング（4s 待機の最後 0.5s）
    constexpr float kFadeStart = 3.5f;
    constexpr float kFadeTotal = 4.0f;

    float sz    = kFinalSize;
    float alpha = 1.0f;

    if (m_checkMarkTimer < kExpandEnd) {
        // ease-out 拡大（0 → kMaxSize）
        float t = m_checkMarkTimer / kExpandEnd;
        t  = 1.0f - (1.0f - t) * (1.0f - t);
        sz = kMaxSize * t;
    } else if (m_checkMarkTimer < kShrinkEnd) {
        // ease-in 縮小（kMaxSize → kFinalSize）
        float t = (m_checkMarkTimer - kExpandEnd) / (kShrinkEnd - kExpandEnd);
        sz = kMaxSize - (kMaxSize - kFinalSize) * t;
    } else if (m_step == TutorialStep::CupIn && m_checkMarkTimer >= kFadeStart) {
        // CupIn のみ: フェードアウト
        float remain = kFadeTotal - m_checkMarkTimer;
        alpha = std::max(0.0f, remain / 0.5f);
    }

    // オーバーレイBG 中央（640, 150）を基準にセンタリング
    img->width  = sz;
    img->height = sz;
    img->alpha  = alpha;
    img->x      = 640.0f - sz * 0.5f;
    img->y      = 150.0f - sz * 0.5f;  // 150 = BG(y=80) + BG_h(140)/2

    if (alpha <= 0.0f) img->visible = false;
}

} // namespace game::controllers

