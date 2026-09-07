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

void ShotGaugePanel::Update(core::GameContext &ctx, float deltaTime,
                            game::components::ShotState::Phase phase,
                            float currentPower, float confirmedPower,
                            float currentImpact, float confirmedImpact,
                            const ClubUIData &currentClub) {
  m_dismissRemaining =
      std::max(0.0f, m_dismissRemaining - deltaTime);
  const auto previousPhase = m_previousPhase;
  if (phase != previousPhase) {
    m_previousPhase = phase;
    m_phaseTransition = 0.0f;
    if (previousPhase == game::components::ShotState::Phase::ImpactTiming &&
        phase == game::components::ShotState::Phase::Executing) {
      m_dismissRemaining =
          game::ui::kGaugeHoldDuration + game::ui::kGaugeFadeDuration;
    }
  }
  m_phaseTransition =
      std::min(1.0f, m_phaseTransition + deltaTime * 7.5f);
  UpdatePanelContent(ctx, phase, currentPower, confirmedPower, currentImpact,
                     confirmedImpact, currentClub);
}

void ShotGaugePanel::UpdatePanelContent(
    core::GameContext &ctx, game::components::ShotState::Phase phase,
    float currentPower, float confirmedPower, float currentImpact,
    float confirmedImpact, const ClubUIData &currentClub) {
  const bool powerPhase =
      phase == game::components::ShotState::Phase::PowerCharging;
  const bool impactPhase =
      phase == game::components::ShotState::Phase::ImpactTiming;
  const bool activePhase = powerPhase || impactPhase;
  const bool dismissing = !activePhase && m_dismissRemaining > 0.0f;
  float fadeLinear = 1.0f;
  if (dismissing) {
    fadeLinear = std::clamp(
        m_dismissRemaining / game::ui::kGaugeFadeDuration, 0.0f, 1.0f);
  }
  const float fadeAlpha =
      fadeLinear * fadeLinear * (3.0f - 2.0f * fadeLinear);

  float shownPower = currentPower;
  if (confirmedPower > 0.0f) {
    shownPower = confirmedPower;
  }
  const int distanceYards = std::clamp(
      static_cast<int>(std::round(currentClub.baseCarryDistance * shownPower)),
      0, 9999);
  const int powerPercent = std::clamp(
      static_cast<int>(std::round(shownPower * 100.0f)), 0, 100);
  const float pulse = std::max(0.0f, 1.0f - m_phaseTransition);

  if (powerPhase) {
    shot_gauge_detail::SetText(ctx.world, m_entities.step, L"01 / 02");
    shot_gauge_detail::SetText(ctx.world, m_entities.title, L"パワー調整");
    shot_gauge_detail::SetText(ctx.world, m_entities.hint,
            L"左クリックで確定　　右クリックでキャンセル");
    shot_gauge_detail::SetText(ctx.world, m_entities.powerLabel, L"距離");
    shot_gauge_detail::SetText(ctx.world, m_entities.powerValue,
            std::to_wstring(distanceYards) + L"y");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyLabel, L"つぎ");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyValue, L"インパクト");
    shot_gauge_detail::SetColor(ctx.world, m_entities.powerValue, game::ui::kColorWarning);
    shot_gauge_detail::SetColor(ctx.world, m_entities.accuracyValue, game::ui::kColorTextSub);
  } else if (impactPhase || dismissing) {
    float shownImpact = confirmedImpact;
    if (impactPhase) {
      shownImpact = currentImpact;
    }
    const float difference = shownImpact - 0.5f;
    const float absoluteDifference = std::abs(difference);
    std::wstring impactText = L"芯";
    DirectX::XMFLOAT4 impactColor = game::ui::kColorSpecial;
    if (absoluteDifference >= game::ui::kThresholdSpecial) {
      const int missPercent = std::clamp(
          static_cast<int>(std::round(absoluteDifference * 200.0f)), 0, 100);
      std::wstring direction = L"右 ";
      if (difference < 0.0f) {
        direction = L"左 ";
      }
      impactText = direction + std::to_wstring(missPercent) + L"%";
      if (absoluteDifference < game::ui::kThresholdGreat) {
        impactColor = game::ui::kColorSuccess;
      } else {
        impactColor = game::ui::kColorWarning;
      }
    }

    shot_gauge_detail::SetText(ctx.world, m_entities.step, L"02 / 02");
    shot_gauge_detail::SetText(ctx.world, m_entities.title, L"インパクトタイミング");
    if (dismissing) {
      shot_gauge_detail::SetText(ctx.world, m_entities.hint, L"");
    } else {
      shot_gauge_detail::SetText(ctx.world, m_entities.hint,
              L"左クリックでインパクト　　右クリックでキャンセル");
    }
    shot_gauge_detail::SetText(ctx.world, m_entities.powerLabel, L"距離");
    shot_gauge_detail::SetText(ctx.world, m_entities.powerValue,
            std::to_wstring(distanceYards) + L"y 確定");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyLabel, L"正確性");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyValue, impactText);
    shot_gauge_detail::SetColor(ctx.world, m_entities.powerValue, game::ui::kColorTextSub);
    shot_gauge_detail::SetColor(ctx.world, m_entities.accuracyValue, impactColor);
  } else if (phase == game::components::ShotState::Phase::Idle) {
    shot_gauge_detail::SetText(ctx.world, m_entities.step, L"");
    shot_gauge_detail::SetText(ctx.world, m_entities.title, L"ショット準備");
    shot_gauge_detail::SetText(ctx.world, m_entities.hint, L"");
    shot_gauge_detail::SetText(ctx.world, m_entities.powerLabel, L"距離");
    shot_gauge_detail::SetText(ctx.world, m_entities.powerValue, L"0y");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyLabel, L"正確性");
    shot_gauge_detail::SetText(ctx.world, m_entities.accuracyValue, L"待機");
    shot_gauge_detail::SetColor(ctx.world, m_entities.powerValue, game::ui::kColorTextPrimary);
    shot_gauge_detail::SetColor(ctx.world, m_entities.accuracyValue, game::ui::kColorTextSub);
  }

  const auto applyPunch = [&](ecs::Entity entity) {
    auto *text = ctx.world.Get<game::components::UIText>(entity);
    if (!text) {
      return;
    }
    text->style.fontSize = game::ui::kShotValueFontSize +
                           game::ui::kShotValuePunchFontDelta * pulse;
  };
  applyPunch(m_entities.powerValue);
  applyPunch(m_entities.accuracyValue);

  std::wstring clubText = core::ToWString(currentClub.name);
  if (currentClub.baseCarryDistance > 0.0f) {
    clubText += L" (基準 " +
                std::to_wstring(static_cast<int>(
                    std::round(currentClub.baseCarryDistance))) +
                L"y)";
  }
  shot_gauge_detail::SetText(ctx.world, m_entities.club, clubText);

  auto *gauge =
      ctx.world.Get<game::components::UIBarGauge>(m_entities.gauge);
  if (gauge) {
    gauge->isVisible = activePhase || dismissing;
    gauge->mode = game::components::UIBarGaugeMode::Power;
    if (impactPhase || dismissing) {
      gauge->mode = game::components::UIBarGaugeMode::Impact;
    }
    gauge->showImpactZones = impactPhase || dismissing;
    gauge->showMarker = powerPhase || impactPhase;
    gauge->showConfirmedMarker = dismissing;
    gauge->confirmPulse = pulse;
    gauge->opacity = 1.0f;
    if (dismissing) {
      gauge->opacity = fadeAlpha;
      const auto judgement =
          game::utils::EvaluateImpactJudgement(confirmedImpact);
      const auto color = shot_gauge_detail::GetJudgementColor(judgement);
      gauge->confirmedValue = confirmedImpact;
      gauge->confirmedMarkerColor = color;
      gauge->markerColor = color;
      gauge->borderColor = color;
    } else {
      gauge->confirmedValue = confirmedPower;
      gauge->confirmedMarkerColor = game::ui::kColorWarning;
      gauge->markerColor = game::ui::kColorWarning;
      gauge->borderColor = game::ui::kColorBorder;
      if (impactPhase) {
        gauge->markerColor = game::ui::kColorWhite;
        gauge->borderColor = game::ui::kColorSuccess;
      }
    }
    gauge->color = game::ui::kColorAccent;
    if (powerPercent >= 75) {
      gauge->color = game::ui::kColorWarning;
    } else if (powerPercent >= 40) {
      gauge->color = game::ui::kColorSuccess;
    }

    const float eased =
        1.0f - std::pow(1.0f - m_phaseTransition, 3.0f);
    float gaugeOffsetY = 0.0f;
    if (activePhase) {
      gaugeOffsetY = (1.0f - eased) * 22.0f;
    } else if (dismissing) {
      gaugeOffsetY =
          -(1.0f - fadeAlpha) * game::ui::kGaugeFadeDriftY;
    }
    gauge->y = game::ui::kShotPanelY + 12.0f + gaugeOffsetY;
  }

  float offsetY = -game::ui::kGaugeFadeDriftY;
  float alpha = 0.0f;
  if (activePhase) {
    const float eased =
        1.0f - std::pow(1.0f - m_phaseTransition, 3.0f);
    offsetY = (1.0f - eased) * 22.0f;
    alpha = eased;
  } else if (dismissing) {
    offsetY = -(1.0f - fadeAlpha) * game::ui::kGaugeFadeDriftY;
    alpha = fadeAlpha;
  }

  auto *background =
      ctx.world.Get<game::components::UIText>(m_entities.background);
  if (background) {
    background->y = game::ui::kShotPanelBgY + offsetY;
    background->style.bgColor.w = game::ui::kColorBgDark.w * alpha;
    background->style.borderColor.w = game::ui::kColorBorder.w * alpha;
  }

  constexpr float kShadowBaseAlpha = 0.35f;
  const auto moveText = [&](ecs::Entity entity, float baseY) {
    auto *text = ctx.world.Get<game::components::UIText>(entity);
    if (!text) {
      return;
    }
    text->y = baseY + offsetY;
    text->style.color.w = alpha;
    if (text->style.hasShadow) {
      text->style.shadowColor.w = kShadowBaseAlpha * alpha;
    }
  };
  moveText(m_entities.step, game::ui::kShotPanelY - 22.0f);
  auto *step = ctx.world.Get<game::components::UIText>(m_entities.step);
  if (step) {
    step->style.bgColor.w = game::ui::kColorSurfaceRaised.w * alpha;
    step->style.borderColor.w = 0.75f * alpha;
  }
  moveText(m_entities.title, game::ui::kShotPanelY - 25.0f);
  moveText(m_entities.hint, game::ui::kShotPanelY - 19.0f);
  moveText(m_entities.powerLabel, game::ui::kShotPanelY + 48.0f);
  moveText(m_entities.powerValue, game::ui::kShotPanelY + 48.0f);
  moveText(m_entities.accuracyLabel, game::ui::kShotPanelY + 76.0f);
  moveText(m_entities.accuracyValue, game::ui::kShotPanelY + 76.0f);
  moveText(m_entities.club, game::ui::kShotPanelY + 104.0f);
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
  } else {
    gauge->markerValue = std::clamp(normalizedMarker, 0.0f, 1.0f);
  }
  gauge->showMarker = true;
  gauge->isVisible = true;
}

void ShotGaugePanel::UpdateJudge(core::GameContext &ctx,
                                 const std::wstring &text,
                                 const DirectX::XMFLOAT4 &color) {
  shot_gauge_detail::SetText(ctx.world, m_entities.judge, text);
  shot_gauge_detail::SetColor(ctx.world, m_entities.judge, color);
  shot_gauge_detail::SetText(ctx.world, m_entities.accuracyValue, text);
  shot_gauge_detail::SetColor(ctx.world, m_entities.accuracyValue, color);
}

} // namespace game::controllers::hud

