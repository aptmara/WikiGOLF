/**
 * @file ShotGaugePanelVisibility.cpp
 * @brief ショットゲージパネルの表示切り替えと後始末
*/

#include "ShotGaugePanel.h"
#include "ShotGaugePanelInternals.h"
#include "../../../core/GameContext.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"


namespace game::controllers::hud {

void ShotGaugePanel::ResetTimers() {
  m_entranceElapsed = 99.0f;
  m_zoneElapsed = 99.0f;
  m_confirmElapsed = 99.0f;
  m_resultActive = false;
  m_wasCommitted = false;
  m_resultElapsed = 0.0f;
}

void ShotGaugePanel::Reset(core::GameContext &ctx) {
  UpdateJudge(ctx, L"", {1.0f, 1.0f, 1.0f, 1.0f});
  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (gauge) {
    gauge->value = 0.0f;
    gauge->markerValue = 0.0f;
    gauge->isVisible = false;
    gauge->mode = game::components::UIBarGaugeMode::Power;
    gauge->showImpactZones = false;
    gauge->showAimPinMarker = false;
    gauge->aimPinValue = 0.0f;
    gauge->resultActive = false;
    gauge->confirmElapsed = 99.0f;
    gauge->opacity = 1.0f;
  }
  ResetTimers();
  m_previousPhase = game::components::ShotState::Phase::Idle;
}

void ShotGaugePanel::SetGaugeVisible(core::GameContext &ctx, bool visible) {
  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (gauge) {
    gauge->isVisible = visible;
  }
}

void ShotGaugePanel::SetImpactZonesVisible(core::GameContext &ctx,
                                           bool visible) {
  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (gauge) {
    gauge->showImpactZones = visible;
  }
}

void ShotGaugePanel::SetVisible(core::GameContext &ctx, bool visible) {
  if (visible) {
    return;
  }
  // 残っている結果表示を止めないと、次の更新でゲージが再表示されてしまう。
  m_resultActive = false;
  SetGaugeVisible(ctx, false);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.judge, false);
}

void ShotGaugePanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_entities = Entities{};
  ResetTimers();
  m_previousPhase = game::components::ShotState::Phase::Idle;
}

} // namespace game::controllers::hud

