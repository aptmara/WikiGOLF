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
 * @brief ゲージ値を0.0から1.0に収めます。*/
inline float ClampGaugeValue(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

/**
 * @brief インパクト中心からの絶対誤差を返します。
 * @param center パーフェクトとみなす中心位置 (0.0〜1.0)。既定は0.5（バー中央）だが、
 *        エイムピン設置時はパワー確定位置(confirmedPower)がここに渡され、
 *        「狙った位置」がそのままパーフェクト位置になる。
*/
inline float GetImpactDiff(float impactValue, float center = 0.5f) {
  return std::abs(ClampGaugeValue(impactValue) - ClampGaugeValue(center));
}

/**
 * @brief インパクト値からショット判定を決定します。
 * @param center パーフェクトとみなす中心位置 (0.0〜1.0)。GetImpactDiff参照。
*/
inline game::components::ShotJudgement EvaluateImpactJudgement(
    float impactValue, float center = 0.5f) {
  const float diff = GetImpactDiff(impactValue, center);
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
 * @brief 中央から左右に広がる判定しきい値を描画用の全幅へ変換します。*/
inline float GetImpactZoneVisualWidth(float threshold) {
  return ClampGaugeValue(threshold * 2.0f);
}

/**
 * @brief インパクトのずれを反映した打球方向を返します。
 * @details Nice/Missは、狙った方向からずれた側へ打ち出します。定規の中心より手前で
 *          止めれば左、奥で止めれば右へ曲がり、Great以上はまっすぐ飛びます。
 *          ずれは狙った方向に対する横方向で与え、水平成分の長さを保つので、
 *          打つ向きによって曲がる方向や飛距離が変わりません。
 * @param shotDir 狙った方向（左手系、水平成分は単位長さを想定）。
 * @param center パーフェクトとみなす中心位置 (0.0〜1.0)。
*/
inline DirectX::XMFLOAT3 ApplyImpactSideError(
    const DirectX::XMFLOAT3 &shotDir,
    game::components::ShotJudgement judgement, float impactValue,
    float center) {
  float sideOffset = 0.0f;
  if (judgement == game::components::ShotJudgement::Nice) {
    sideOffset = 0.1f;
  } else if (judgement == game::components::ShotJudgement::Miss) {
    sideOffset = 0.3f;
  }
  if (sideOffset == 0.0f) {
    return shotDir;
  }
  if (ClampGaugeValue(impactValue) < ClampGaugeValue(center)) {
    sideOffset = -sideOffset;
  }
  const float x = shotDir.x + shotDir.z * sideOffset;
  const float z = shotDir.z - shotDir.x * sideOffset;
  const float aimLength = std::sqrt(shotDir.x * shotDir.x + shotDir.z * shotDir.z);
  const float length = std::sqrt(x * x + z * z);
  if (length <= 0.0f) {
    return shotDir;
  }
  const float scale = aimLength / length;
  return {x * scale, shotDir.y, z * scale};
}

/**
 * @brief 判定を示す色を返します。ゲージと演出で同じ規則を使います。
 * @details Wikipediaのリンク色に対応させています（Niceはリンクの青、Greatは
 *          訪問済みの紫、Missは赤リンク、Specialは秀逸な記事の金）。
*/
inline DirectX::XMFLOAT4 GetJudgementColor(
    game::components::ShotJudgement judgement) {
  switch (judgement) {
  case game::components::ShotJudgement::Special:
    return game::ui::kColorSpecial;
  case game::components::ShotJudgement::Great:
    return game::ui::kColorJudgeGreat;
  case game::components::ShotJudgement::Nice:
    return game::ui::kColorAccent;
  default:
    return game::ui::kColorError;
  }
}

/** @brief 定規の目盛り間隔です。どちらも0なら目盛りを描きません。*/
struct RulerTicks {
  int minorYards = 0; ///< 細かい目盛りの間隔
  int majorYards = 0; ///< 数字を添える目盛りの間隔
};

/**
 * @brief クラブの基準飛距離に合う目盛り間隔を返します。
 * @details 数字付きの目盛りが6本程度に収まる最小の間隔を選ぶので、ドライバーでも
 *          パターでも定規の密度が揃います。
*/
inline RulerTicks GetRulerTicks(float maxYards) {
  if (maxYards <= 0.0f) {
    return {};
  }
  constexpr RulerTicks kCandidates[] = {{1, 2},   {1, 5},   {2, 10},
                                        {5, 20},  {5, 25},  {10, 50},
                                        {20, 100}, {50, 200}, {100, 500}};
  for (const auto &ticks : kCandidates) {
    if (maxYards / static_cast<float>(ticks.majorYards) <= 6.0f) {
      return ticks;
    }
  }
  return {100, 500};
}

/**
 * @brief インパクト判定結果から、基本飛距離に対する倍率を返します。
 *
 * ExecuteShot(実際のショット)とHUD表示のどちらからも参照し、
 * 「基本飛距離からの変位」を判定と一貫させます。
*/
inline float GetJudgementDistanceMultiplier(
    game::components::ShotJudgement judgement) {
  switch (judgement) {
  case game::components::ShotJudgement::Special:
    return 1.1f;
  case game::components::ShotJudgement::Great:
    return 1.0f;
  case game::components::ShotJudgement::Nice:
    return 0.9f;
  case game::components::ShotJudgement::Miss:
    return 0.8f;
  default:
    return 1.0f;
  }
}

} // namespace game::utils
