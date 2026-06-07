#pragma once
/**
 * @file ShotGaugeRules.h
 * @brief ショットゲージの表示と判定で共有するルール
 */

#include "../components/WikiComponents.h"
#include "UIConstants.h"
#include <algorithm>
#include <cmath>

namespace game::utils {

/**
 * @brief ゲージ値を0.0から1.0に収めます。
 * @author 山内陽
 */
inline float ClampGaugeValue(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

/**
 * @brief インパクト中心からの絶対誤差を返します。
 * @author 山内陽
 */
inline float GetImpactDiff(float impactValue) {
  return std::abs(ClampGaugeValue(impactValue) - 0.5f);
}

/**
 * @brief インパクト値からショット判定を決定します。
 * @author 山内陽
 */
inline game::components::ShotJudgement EvaluateImpactJudgement(
    float impactValue) {
  const float diff = GetImpactDiff(impactValue);
  if (diff < game::ui::kThresholdSpecial) {
    return game::components::ShotJudgement::Special;
  }
  if (diff < game::ui::kThresholdGreat) {
    return game::components::ShotJudgement::Great;
  }
  if (diff < game::ui::kThresholdNice) {
    return game::components::ShotJudgement::Nice;
  }
  return game::components::ShotJudgement::Miss;
}

/**
 * @brief 中央から左右に広がる判定しきい値を描画用の全幅へ変換します。
 * @author 山内陽
 */
inline float GetImpactZoneVisualWidth(float threshold) {
  return ClampGaugeValue(threshold * 2.0f);
}

} // namespace game::utils
