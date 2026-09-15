/**
 * @file WikiGolfHUD.cpp
 * @brief 通常時画面のHUDを管理するコントローラー
 *
 * 通常時は四隅へ必要情報を寄せ、中央のプレー領域を空ける。
 *
 * 入力: GolfGameState, ShotState, クラブ情報, 風情報, カメラ情報
 * 出力: UIText/UIImage/UIBarGauge エンティティの表示・テキスト更新
 *
 * 呼び出し元: WikiGolfScene::OnUpdate -> m_hud->Update(...)
*/
#include "WikiGolfHUD.h"
#include "hud/HudStyles.h"
#include "../../ecs/World.h"
#include "../components/UIText.h"
#include "../components/UIImage.h"
#include "../components/WikiComponents.h"
#include "../../core/StringUtils.h"
#include "../utils/ShotGaugeRules.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

namespace game {
namespace controllers {

// =====================================================
// Initialize
// =====================================================

void WikiGolfHUD::Initialize(core::GameContext& ctx) {
    m_courseInfoPanel.Initialize(ctx);
    m_windPanel.Initialize(ctx);
    m_clubSelectionPanel.Initialize(ctx);
    m_shotGaugePanel.Initialize(ctx);
    m_minimapDecorationPanel.Initialize(ctx);
    m_normalHudOpacity = 1.0f;
    m_shotSequenceActive = false;
    m_isVisible = true;
    m_tutorialPresentation = false;
    m_tutorialShowCourseInfo = false;
    m_tutorialShowClubSelection = false;
    m_tutorialShowMinimapDecoration = false;
}

void WikiGolfHUD::Shutdown(core::GameContext& ctx) {
    m_courseInfoPanel.Shutdown(ctx);
    m_clubSelectionPanel.Shutdown(ctx);
    m_minimapDecorationPanel.Shutdown(ctx);
    m_shotGaugePanel.Shutdown(ctx);
    m_windPanel.Shutdown(ctx);
    m_elapsedTime = 0.0f;
    m_normalHudOpacity = 1.0f;
    m_shotSequenceActive = false;
    m_isVisible = true;
    m_tutorialPresentation = false;
    m_tutorialShowCourseInfo = false;
    m_tutorialShowClubSelection = false;
    m_tutorialShowMinimapDecoration = false;
}

// =====================================================
// Update
// =====================================================

void WikiGolfHUD::Update(core::GameContext& ctx, float dt,
                         const game::components::GolfGameState& state,
                         game::components::ShotState::Phase shotPhase,
                         float currentImpact,
                         float currentPower, float confirmedPower,
                         float confirmedImpact, float impactCenter,
                         float windSpeed, const DirectX::XMFLOAT2& windDir, float cameraYaw,
                         const std::vector<ClubUIData>& clubs,
                         int currentClubIndex,
                         float distanceToTarget, float heightDiff,
                         const game::components::AimPinState* aimPin)
{
    m_elapsedTime += dt;

    m_courseInfoPanel.Update(ctx, state);
    m_windPanel.Update(ctx, m_elapsedTime, windSpeed, windDir, cameraYaw,
                       state.currentMaterial);
    m_clubSelectionPanel.Update(ctx, m_elapsedTime, clubs, currentClubIndex);

    ClubUIData currentClubData;
    if (currentClubIndex >= 0 && currentClubIndex < clubs.size()) {
        currentClubData = clubs[currentClubIndex];
    }
    m_shotGaugePanel.Update(ctx, dt, shotPhase, currentPower, confirmedPower,
                            currentImpact, confirmedImpact, impactCenter,
                            currentClubData, aimPin);
    UpdateNormalHudTransition(ctx, dt, shotPhase);
}

void WikiGolfHUD::UpdateNormalHudTransition(
    core::GameContext& ctx, float dt,
    game::components::ShotState::Phase shotPhase) {
    using Phase = game::components::ShotState::Phase;
    const bool active = shotPhase == Phase::PowerCharging ||
                        shotPhase == Phase::ImpactTiming ||
                        shotPhase == Phase::Executing;
    if (active) {
        m_shotSequenceActive = true;
        m_normalHudOpacity = 0.0f;
    } else {
        if (m_shotSequenceActive) {
            m_shotSequenceActive = false;
            m_normalHudOpacity = 0.0f;
        }
        const float duration = game::ui::kNormalHudFadeDuration;
        m_normalHudOpacity = std::min(
            1.0f, m_normalHudOpacity + std::max(0.0f, dt) / duration);
    }
    ApplyNormalHudOpacity(ctx);
}

void WikiGolfHUD::ApplyNormalHudOpacity(core::GameContext& ctx) {
    const bool visible = m_isVisible && m_normalHudOpacity > 0.001f;
    const bool showAll = !m_tutorialPresentation;
    m_courseInfoPanel.SetVisible(
        ctx, visible && (showAll || m_tutorialShowCourseInfo));
    m_windPanel.SetVisible(ctx, visible && showAll);
    m_clubSelectionPanel.SetVisible(
        ctx, visible && (showAll || m_tutorialShowClubSelection));
    m_minimapDecorationPanel.SetVisible(
        ctx, visible && (showAll || m_tutorialShowMinimapDecoration));
    m_courseInfoPanel.SetOpacity(ctx, m_normalHudOpacity);
    m_windPanel.SetOpacity(ctx, m_normalHudOpacity);
    m_clubSelectionPanel.SetOpacity(ctx, m_normalHudOpacity);
    m_minimapDecorationPanel.SetOpacity(ctx, m_normalHudOpacity);
}

void WikiGolfHUD::UpdatePowerGauge(core::GameContext& ctx, float fillValue,
                                    float markerValue, float minPower,
                                    float maxPower) {
    m_shotGaugePanel.UpdatePowerGauge(ctx, fillValue, markerValue, minPower,
                                      maxPower);
}

void WikiGolfHUD::UpdateJudge(core::GameContext& ctx,
                              const std::wstring& text,
                              const DirectX::XMFLOAT4& color) {
    m_shotGaugePanel.UpdateJudge(ctx, text, color);
}

void WikiGolfHUD::ResetShotUI(core::GameContext& ctx) {
    m_shotGaugePanel.Reset(ctx);
    SetShotPhaseUIVisible(ctx, false);
}

void WikiGolfHUD::SetGaugeVisible(core::GameContext& ctx, bool visible) {
    m_shotGaugePanel.SetGaugeVisible(ctx, visible);
}

void WikiGolfHUD::SetImpactZonesVisible(core::GameContext& ctx, bool visible) {
    m_shotGaugePanel.SetImpactZonesVisible(ctx, visible);
}

// -------------------------------------------------------
// 通常時 <-> ショット時 UI 切り替え
// 入力: shotPhase = true なら ゲージ表示、クラブリスト薄く
// 変更: ショットボタン/操作ヘルプの visible を制御
// 出力: 各エンティティの visible 変更
// -------------------------------------------------------
void WikiGolfHUD::SetShotPhaseUIVisible(core::GameContext& ctx, bool shotPhase) {
    if (shotPhase) {
        m_shotSequenceActive = true;
        m_normalHudOpacity = 0.0f;
        ApplyNormalHudOpacity(ctx);
    }
    m_shotGaugePanel.SetShotPhaseVisible(ctx, shotPhase);

} // SetShotPhaseUIVisible

// -----------------------------------------------------------------
// HUD全体の表示/非表示切り替え（ロード中は非表示）
// 入力: visible=false → 全UIエンティティを非表示
// 変更: UIText の visible フラグを一括更新
// 出力: なし（副作用: ECS UIText コンポーネントの visible 変更）
// -----------------------------------------------------------------
void WikiGolfHUD::SetVisible(core::GameContext& ctx, bool visible) {
    m_isVisible = visible;
    ApplyNormalHudOpacity(ctx);
    m_shotGaugePanel.SetVisible(ctx, visible);

}

void WikiGolfHUD::SetTutorialPresentation(
    core::GameContext& ctx, bool enabled, bool showCourseInfo,
    bool showClubSelection, bool showMinimapDecoration) {
    m_tutorialPresentation = enabled;
    m_tutorialShowCourseInfo = showCourseInfo;
    m_tutorialShowClubSelection = showClubSelection;
    m_tutorialShowMinimapDecoration = showMinimapDecoration;
    ApplyNormalHudOpacity(ctx);
}

} // namespace controllers
} // namespace game
