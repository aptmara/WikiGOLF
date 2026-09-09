/**
 * @file WikiGolfHUD.cpp
 * @brief 通常時画面のHUDを管理するコントローラー
 *
 * デザインの方針:
 *   - すべてのパネルは ApplySurfaceStyle() が作る単一の見た目（単色・
 *     同一角丸・同一枠線・同一影）だけを使う。パネルごとに固有の
 *     グラデーションや影を作らない。
 *   - 色は用途で固定する: kColorAccent=操作可能な要素、kColorSpecial=
 *     目的地、kColorSuccess/kColorWarning/kColorError=プレー結果の
 *     フィードバック、kColorTextSub=すべてのラベル。装飾のための
 *     色分けはしない。
 *   - 常時明滅するアイドルアニメーションは持たない。動きはフェーズ
 *     遷移（ショット開始/終了）など、状態が変わった瞬間にのみ使う。
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
    m_gameplayControlsPanel.Initialize(ctx);
    m_shotGaugePanel.Initialize(ctx);
    m_minimapDecorationPanel.Initialize(ctx);
    m_aimDistancePanel.Initialize(ctx);
}

void WikiGolfHUD::Shutdown(core::GameContext& ctx) {
    m_courseInfoPanel.Shutdown(ctx);
    m_clubSelectionPanel.Shutdown(ctx);
    m_gameplayControlsPanel.Shutdown(ctx);
    m_liePanel.Shutdown(ctx);
    m_minimapDecorationPanel.Shutdown(ctx);
    m_shotGaugePanel.Shutdown(ctx);
    m_windPanel.Shutdown(ctx);
    m_aimDistancePanel.Shutdown(ctx);
    m_elapsedTime = 0.0f;
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
    m_windPanel.Update(ctx, m_elapsedTime, windSpeed, windDir, cameraYaw);
    m_clubSelectionPanel.Update(ctx, m_elapsedTime, clubs, currentClubIndex);

    ClubUIData currentClubData;
    if (currentClubIndex >= 0 && currentClubIndex < clubs.size()) {
        currentClubData = clubs[currentClubIndex];
    }
    m_liePanel.Update(ctx, state.currentMaterial);
    m_shotGaugePanel.Update(ctx, dt, shotPhase, currentPower, confirmedPower,
                            currentImpact, confirmedImpact, impactCenter,
                            currentClubData);
    m_aimDistancePanel.Update(ctx, shotPhase, currentPower, confirmedPower,
                              aimPin, currentClubData);
}

void WikiGolfHUD::UpdateLandingPreviewButton(core::GameContext& ctx,
                                              bool hovered, bool active,
                                              bool enabled) {
    m_clubSelectionPanel.UpdateLandingPreviewButton(ctx, hovered, active,
                                                     enabled);
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
    m_clubSelectionPanel.SetShotPhaseVisible(ctx, shotPhase);
    m_gameplayControlsPanel.SetShotPhaseVisible(ctx, shotPhase);
    m_liePanel.SetShotPhaseVisible(ctx, shotPhase);
    m_shotGaugePanel.SetShotPhaseVisible(ctx, shotPhase);

} // SetShotPhaseUIVisible

// -----------------------------------------------------------------
// HUD全体の表示/非表示切り替え（ロード中は非表示）
// 入力: visible=false → 全UIエンティティを非表示
// 変更: UIText の visible フラグを一括更新
// 出力: なし（副作用: ECS UIText コンポーネントの visible 変更）
// -----------------------------------------------------------------
void WikiGolfHUD::SetVisible(core::GameContext& ctx, bool visible) {
    m_courseInfoPanel.SetVisible(ctx, visible);
    m_clubSelectionPanel.SetVisible(ctx, visible);
    m_gameplayControlsPanel.SetVisible(ctx, visible);
    m_liePanel.SetVisible(ctx, visible);
    m_minimapDecorationPanel.SetVisible(ctx, visible);
    m_shotGaugePanel.SetVisible(ctx, visible);
    m_windPanel.SetVisible(ctx, visible);
    m_aimDistancePanel.SetVisible(ctx, visible);

}

} // namespace controllers
} // namespace game
