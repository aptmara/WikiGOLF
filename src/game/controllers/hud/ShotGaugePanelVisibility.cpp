/**
 * @file ShotGaugePanel.cpp
 * @brief ショットゲージパネルの実装
 */

#include "ShotGaugePanel.h"
#include "ShotGaugePanelInternals.h"
#include "HudStyles.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/ShotGaugeRules.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>


namespace game::controllers::hud {

void ShotGaugePanel::Reset(core::GameContext &ctx) {
  UpdateJudge(ctx, L"", {1.0f, 1.0f, 1.0f, 1.0f});
  shot_gauge_detail::SetText(ctx.world, m_entities.accuracyValue, L"---");
  shot_gauge_detail::SetColor(ctx.world, m_entities.accuracyValue,
           game::ui::kColorTextPrimary);
  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (gauge) {
    gauge->value = 0.0f;
    gauge->markerValue = 0.0f;
    gauge->isVisible = false;
    gauge->mode = game::components::UIBarGaugeMode::Power;
    gauge->showImpactZones = false;
    gauge->showConfirmedMarker = false;
    gauge->confirmPulse = 0.0f;
    gauge->opacity = 1.0f;
  }
  m_dismissRemaining = 0.0f;
  SetShotPhaseVisible(ctx, false);
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

void ShotGaugePanel::SetShotPhaseVisible(core::GameContext &ctx,
                                         bool shotPhase) {
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.background, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.step, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.title, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.hint, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.powerLabel, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.powerValue, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.accuracyLabel, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.accuracyValue, shotPhase);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.club, shotPhase);
}

void ShotGaugePanel::SetVisible(core::GameContext &ctx, bool visible) {
  if (visible) {
    return;
  }
  SetGaugeVisible(ctx, false);
  shot_gauge_detail::SetTextVisible(ctx.world, m_entities.judge, false);
  SetShotPhaseVisible(ctx, false);
}

void ShotGaugePanel::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_entities = Entities{};
  m_phaseTransition = 1.0f;
  m_previousPhase = game::components::ShotState::Phase::Idle;
  m_dismissRemaining = 0.0f;
}

} // namespace game::controllers::hud

