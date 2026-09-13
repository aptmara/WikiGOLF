#pragma once
/**
 * @file CupInUtils.h
 * @brief CupInUtils クラスおよび関連インターフェース
*/

#include <DirectXMath.h>
#include <cmath>

namespace game::scenes::cupin {

inline constexpr float kCupApproachEffectRadius = 1.25f;

inline bool IsBallWithinCupApproachRange(
    const DirectX::XMFLOAT3 &ballPos, const DirectX::XMFLOAT3 &holePos) {
  const float dx = ballPos.x - holePos.x;
  const float dz = ballPos.z - holePos.z;
  const float dy = ballPos.y - holePos.y;
  return dx * dx + dz * dz <=
             kCupApproachEffectRadius * kCupApproachEffectRadius &&
         std::abs(dy) <= 2.0f;
}

// ホール判定をまとめたヘルパー（テスト可能な純粋関数）
inline bool IsBallReadyForCupIn(const DirectX::XMFLOAT3 &ballPos,
                                const DirectX::XMFLOAT3 &holePos,
                                float holeRadius, float speedSq) {
  float dx = ballPos.x - holePos.x;
  float dz = ballPos.z - holePos.z;
  float distSq = dx * dx + dz * dz;

  if (distSq > holeRadius * holeRadius)
    return false;

  // 高さチェック：ホールより下かつ一定の深さ内にいる
  float dy = ballPos.y - holePos.y;
  bool inHoleRange = (dy < 0.0f && dy > -1.0f);

  // 速度チェック：十分に遅い場合のみカップインと判定
  bool isSlow = speedSq < 0.01f;

  return inHoleRange && isSlow;
}

} // namespace game::scenes::cupin
