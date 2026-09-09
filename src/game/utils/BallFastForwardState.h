#pragma once
/**
 * @file BallFastForwardState.h
 * @brief 着地後の待機時間短縮（倍速演出）の段階を決めるルール
*/

namespace game::utils {

/** @brief 着地後の倍速演出段階*/
enum class FastForwardTier {
  Normal,    ///< 通常速度（着地前、または着地直後で規定秒数未満）
  Speed1_5x, ///< 着地後、下記kFastForwardStage1Seconds秒〜Stage2未満: 1.5倍速
  Speed2_0x, ///< 着地後、下記kFastForwardStage2Seconds秒以降: 2.0倍速
};

/** @brief 1.5倍速へ切り替わるまでの着地後経過秒数*/
constexpr float kFastForwardStage1Seconds = 10.0f;

/** @brief 2.0倍速へ切り替わるまでの着地後経過秒数*/
constexpr float kFastForwardStage2Seconds = 15.0f;

/**
 * @brief 着地後の経過秒数から現在の倍速段階を求めます。
 * @param secondsSinceLanding 着地してからの経過秒数（未着地の場合は0を渡す）
*/
inline FastForwardTier ResolveFastForwardTier(float secondsSinceLanding) {
  if (secondsSinceLanding >= kFastForwardStage2Seconds) {
    return FastForwardTier::Speed2_0x;
  }
  if (secondsSinceLanding >= kFastForwardStage1Seconds) {
    return FastForwardTier::Speed1_5x;
  }
  return FastForwardTier::Normal;
}

/** @brief 倍速段階に対応する物理シミュレーション速度倍率を返します。*/
inline float GetFastForwardSpeedMultiplier(FastForwardTier tier) {
  switch (tier) {
  case FastForwardTier::Speed1_5x:
    return 1.5f;
  case FastForwardTier::Speed2_0x:
    return 2.0f;
  case FastForwardTier::Normal:
  default:
    return 1.0f;
  }
}

} // namespace game::utils
