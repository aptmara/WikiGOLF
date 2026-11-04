/**
 * @file ShotGaugePanelUpdate.cpp
 * @brief ショットゲージパネルの毎フレーム更新
*/

#include "ShotGaugePanel.h"
#include "ShotGaugePanelInternals.h"
#include "../../../core/GameContext.h"
#include "../../../core/StringUtils.h"
#include "../../../ecs/World.h"
#include "../../components/UIText.h"
#include "../../utils/ShotGaugeRules.h"
#include "../../utils/UIConstants.h"
#include <algorithm>
#include <cmath>


namespace game::controllers::hud {

namespace {

int ToYards(float carryDistance, float ratio) {
  return std::clamp(static_cast<int>(std::round(carryDistance * ratio)), 0,
                    9999);
}

} // namespace

void ShotGaugePanel::Update(core::GameContext &ctx, float deltaTime,
                            game::components::ShotState::Phase phase,
                            float currentPower, float confirmedPower,
                            float confirmedImpact, float impactCenter,
                            const ClubUIData &currentClub,
                            const game::components::AimPinState *aimPin,
                            bool swingCommitted) {
  using Phase = game::components::ShotState::Phase;
  const bool powerPhase = phase == Phase::PowerCharging;
  const bool impactPhase = phase == Phase::ImpactTiming;
  const bool committed = impactPhase && swingCommitted;

  if (phase != m_previousPhase) {
    if (powerPhase) {
      ResetTimers();
      m_entranceElapsed = 0.0f;
    } else if (impactPhase) {
      m_zoneElapsed = 0.0f;
      m_confirmElapsed = 0.0f;
    }
    m_previousPhase = phase;
  }

  if (committed && !m_wasCommitted) {
    m_resultActive = true;
    m_resultElapsed = 0.0f;
    m_resultPower = confirmedPower;
    m_resultImpact = confirmedImpact;
    m_resultCenter = impactCenter;
    m_resultJudgement =
        game::utils::EvaluateImpactJudgement(confirmedImpact, impactCenter);
    m_resultYards = ToYards(
        currentClub.baseCarryDistance * confirmedPower,
        game::utils::GetJudgementDistanceMultiplier(m_resultJudgement));
  }
  m_wasCommitted = committed;

  m_entranceElapsed += deltaTime;
  m_zoneElapsed += deltaTime;
  m_confirmElapsed += deltaTime;
  const float resultDuration =
      game::ui::kGaugeHoldDuration + game::ui::kGaugeFadeDuration;
  if (m_resultActive) {
    m_resultElapsed += deltaTime;
    if (m_resultElapsed >= resultDuration) {
      m_resultActive = false;
    }
  }

  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (!gauge) {
    return;
  }

  const bool activeInput = powerPhase || impactPhase;
  gauge->isVisible = activeInput || m_resultActive;
  if (!gauge->isVisible) {
    return;
  }

  const bool showImpact = impactPhase || m_resultActive;
  gauge->mode = showImpact ? game::components::UIBarGaugeMode::Impact
                           : game::components::UIBarGaugeMode::Power;
  gauge->showImpactZones = showImpact;
  gauge->entrance = std::clamp(
      m_entranceElapsed / game::ui::kGaugeEntranceDuration, 0.0f, 1.0f);
  gauge->zoneGrow = std::clamp(
      m_zoneElapsed / game::ui::kGaugeZoneGrowDuration, 0.0f, 1.0f);
  gauge->confirmElapsed = m_confirmElapsed;
  gauge->rulerMaxYards = currentClub.baseCarryDistance;
  gauge->tabLabel = core::ToWString(currentClub.name);

  gauge->showAimPinMarker =
      aimPin && aimPin->active && activeInput && !m_resultActive;
  if (gauge->showAimPinMarker) {
    gauge->aimPinValue = std::clamp(aimPin->requiredPowerRatio, 0.0f, 1.0f);
    gauge->aimPinMarkerColor = aimPin->reachable ? game::ui::kColorSpecial
                                                 : game::ui::kColorError;
  }

  gauge->resultActive = m_resultActive;
  gauge->opacity = 1.0f;
  if (m_resultActive) {
    const float t = std::clamp((resultDuration - m_resultElapsed) /
                                   game::ui::kGaugeFadeDuration,
                               0.0f, 1.0f);
    gauge->opacity = t * t * (3.0f - 2.0f * t);
    gauge->judgement = m_resultJudgement;
    gauge->resultElapsed = m_resultElapsed;
    gauge->value = m_resultPower;
    gauge->markerValue = m_resultImpact;
    gauge->impactCenter = m_resultCenter;
    gauge->chipYards = m_resultYards;
    gauge->chipAnchor = m_resultCenter;
  } else if (impactPhase) {
    gauge->impactCenter = impactCenter;
    gauge->chipYards = ToYards(currentClub.baseCarryDistance, confirmedPower);
    gauge->chipAnchor = impactCenter;
  } else {
    gauge->chipYards = ToYards(currentClub.baseCarryDistance, currentPower);
    gauge->chipAnchor = std::clamp(currentPower, 0.0f, 1.0f);
  }
}

void ShotGaugePanel::UpdatePowerGauge(core::GameContext &ctx, float fillValue,
                                      float markerValue, float minPower,
                                      float maxPower) {
  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (!gauge) {
    return;
  }
  float normalizedFill = 0.0f;
  float normalizedMarker = 0.0f;
  if (maxPower > minPower && maxPower > 0.0f) {
    normalizedFill = (fillValue - minPower) / (maxPower - minPower);
    normalizedMarker = (markerValue - minPower) / (maxPower - minPower);
  }
  gauge->value = std::clamp(normalizedFill, 0.0f, 1.0f);
  if (gauge->mode == game::components::UIBarGaugeMode::Power) {
    gauge->markerValue = gauge->value;
    gauge->chipAnchor = gauge->value;
  } else {
    gauge->markerValue = std::clamp(normalizedMarker, 0.0f, 1.0f);
  }
  gauge->isVisible = true;
}

void ShotGaugePanel::UpdateJudge(core::GameContext &ctx,
                                 const std::wstring &text,
                                 const DirectX::XMFLOAT4 &color) {
  shot_gauge_detail::SetText(ctx.world, m_entities.judge, text);
  shot_gauge_detail::SetColor(ctx.world, m_entities.judge, color);
}

} // namespace game::controllers::hud

