#pragma once
/**
 * @file AimPinSolver.h
 * @brief エイムピン（狙い所）へ届かせるのに必要なショットの強さを求める
 * @details クラブの基準飛距離(baseCarryDistance)は平坦・無風での値のため、
 *          打ち上げ（ピンがボールより高い）ではその距離だけ打っても手前に、
 *          打ち下ろしでは奥に着弾する。基準飛距離だけでクラブを選ぶと
 *          高低差のあるコースでは常に過不足が出るので、実際の地形上で
 *          弾道シミュレーションを行い、「ピンへ届くゲージ比率」を逆算する。
 *
 *          サンプリング関数を差し替えられるようテンプレートにしてあり、
 *          物理シミュレーションに依存せず単体テストできる。
*/

#include <algorithm>
#include <cmath>

namespace game::utils {

/** @brief 解の上限比率。フルスイングでも届かない場合の外挿をここで打ち切る。*/
inline constexpr float kMaxSolvedPowerRatio = 2.0f;

/** @brief エイムピンへ打つために必要な強さの解*/
struct AimPinSolution {
  /** @brief ピンへ届かせるのに必要なパワーゲージ比率 (0.0〜kMaxSolvedPowerRatio)*/
  float requiredPowerRatio = 0.0f;
  /**
   * @brief 高低差・風を織り込んだ実効飛距離（平坦換算）。
   * @details 「平坦ならこの距離を打つのと同じ」という値。クラブ選択は
   *          実距離ではなくこの値を基準に行う。
  */
  float playsLikeDistance = 0.0f;
  /** @brief フルスイング(比率1.0)以内で届くか*/
  bool reachable = false;
};

namespace aim_pin_detail {

/** @brief 2点間の線形補間で、目標距離に対応する比率を求めます。*/
inline float InterpolateRatio(float ratio0, float distance0, float ratio1,
                              float distance1, float targetDistance) {
  const float span = distance1 - distance0;
  if (span <= 1e-4f) {
    return ratio1;
  }
  return ratio0 + (ratio1 - ratio0) * (targetDistance - distance0) / span;
}

} // namespace aim_pin_detail

/**
 * @brief 目標地点へ届かせるのに必要なパワーゲージ比率を求めます。
 * @param targetDistance ボールから目標地点までの水平距離
 * @param baseCarryDistance クラブの平坦基準飛距離
 * @param sampleReach 比率(0.0〜1.0)を渡すと、その強さで実際に打ったときの
 *        到達水平距離を返す呼び出し可能オブジェクト
 * @param sampleCount 事前サンプリング数（2〜12）。多いほど正確だが重い。
 * @details サンプリング結果から線形補間で比率を逆引きし、さらに求めた比率で
 *          1回だけ実測して補間し直すことで、少ない試行回数で精度を確保する。
 *          斜面での跳ね方によって到達距離がわずかに前後しても順序が壊れない
 *          よう、サンプル列は単調増加になるよう均してから扱う。
*/
template <typename SampleReach>
inline AimPinSolution SolveAimPinPower(float targetDistance,
                                       float baseCarryDistance,
                                       SampleReach &&sampleReach,
                                       int sampleCount = 5) {
  AimPinSolution solution;
  if (!(baseCarryDistance > 0.0f) || !std::isfinite(targetDistance)) {
    return solution;
  }
  if (targetDistance <= 0.0f) {
    solution.reachable = true;
    return solution;
  }

  constexpr int kMaxSamples = 14;
  sampleCount = std::clamp(sampleCount, 2, kMaxSamples - 2);

  float ratios[kMaxSamples] = {0.0f};
  float reaches[kMaxSamples] = {0.0f};
  int count = 1; // [0] は 比率0 -> 到達0

  float monotonicReach = 0.0f;
  for (int i = 1; i <= sampleCount; ++i) {
    const float ratio =
        static_cast<float>(i) / static_cast<float>(sampleCount);
    float reach = sampleReach(ratio);
    if (!std::isfinite(reach)) {
      reach = monotonicReach;
    }
    monotonicReach = (std::max)(monotonicReach, reach);
    ratios[count] = ratio;
    reaches[count] = monotonicReach;
    ++count;
  }

  int upper = -1;
  for (int i = 1; i < count; ++i) {
    if (reaches[i] >= targetDistance) {
      upper = i;
      break;
    }
  }

  float ratio = 0.0f;
  if (upper < 0) {
    // フルスイングでも届かない。最後の区間の傾きで外挿し、どれだけ足りない
    // かを実効飛距離として残す（より飛ぶクラブを選ぶ判断材料になる）。
    const float ratio0 = ratios[count - 2];
    const float reach0 = reaches[count - 2];
    const float ratio1 = ratios[count - 1];
    const float reach1 = reaches[count - 1];
    const float span = reach1 - reach0;
    ratio = (span > 1e-4f)
                ? ratio1 + (targetDistance - reach1) * (ratio1 - ratio0) / span
                : kMaxSolvedPowerRatio;
  } else {
    float ratio0 = ratios[upper - 1];
    float reach0 = reaches[upper - 1];
    float ratio1 = ratios[upper];
    float reach1 = reaches[upper];

    const float estimate = aim_pin_detail::InterpolateRatio(
        ratio0, reach0, ratio1, reach1, targetDistance);
    if (estimate > ratio0 && estimate < ratio1) {
      float measured = sampleReach(estimate);
      if (!std::isfinite(measured)) {
        measured = reach0;
      }
      measured = std::clamp(measured, reach0, reach1);
      if (measured >= targetDistance) {
        ratio1 = estimate;
        reach1 = measured;
      } else {
        ratio0 = estimate;
        reach0 = measured;
      }
    }
    ratio = aim_pin_detail::InterpolateRatio(ratio0, reach0, ratio1, reach1,
                                             targetDistance);
  }

  solution.requiredPowerRatio = std::clamp(ratio, 0.0f, kMaxSolvedPowerRatio);
  solution.playsLikeDistance = solution.requiredPowerRatio * baseCarryDistance;
  solution.reachable = solution.requiredPowerRatio <= 1.0f;
  return solution;
}

} // namespace game::utils
