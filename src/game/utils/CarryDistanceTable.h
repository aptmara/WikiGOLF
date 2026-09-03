#pragma once
/**
 * @file CarryDistanceTable.h
 * @brief 初速とキャリー飛距離(平坦・無風基準)の対応表
 *
 * 「初速→飛距離」は地形摩擦・空気抵抗を含む非線形な関係のため、毎フレーム
 * 弾道シミュレーションを解いて逆算するのはコストが高い。クラブ初期化時に
 * 一度だけ TrajectorySimulation.h でこの表を構築し、以降はここでの線形補間
 * だけで「目標飛距離→初速」を求める。
 */

#include <vector>

namespace game::utils {

struct CarryDistanceTable {
  std::vector<float> speeds;    ///< 昇順、先頭は0
  std::vector<float> distances; ///< speedsに対応する平坦・無風キャリー飛距離(昇順)
};

/// @brief 目標飛距離に対応する初速を表から線形補間で求めます。
inline float LookupSpeedForDistance(const CarryDistanceTable &table,
                                    float targetDistance) {
  if (table.speeds.empty() || table.distances.empty()) {
    return 0.0f;
  }
  if (targetDistance <= table.distances.front()) {
    return table.speeds.front();
  }
  if (targetDistance >= table.distances.back()) {
    return table.speeds.back();
  }

  for (size_t i = 1; i < table.distances.size(); ++i) {
    if (targetDistance <= table.distances[i]) {
      const float d0 = table.distances[i - 1];
      const float d1 = table.distances[i];
      const float s0 = table.speeds[i - 1];
      const float s1 = table.speeds[i];
      const float t = (d1 > d0) ? (targetDistance - d0) / (d1 - d0) : 0.0f;
      return s0 + (s1 - s0) * t;
    }
  }
  return table.speeds.back();
}

} // namespace game::utils
