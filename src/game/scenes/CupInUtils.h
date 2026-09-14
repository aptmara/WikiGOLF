#pragma once
/**
 * @file CupInUtils.h
 * @brief CupInUtils クラスおよび関連インターフェース
*/

#include "../utils/GolfCupPhysics.h"
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

/**
 * @brief ホール位置（縁の中心）と開口半径からカップ形状を作ります。
*/
inline game::physics::GolfCupShape MakeCupShape(
    const DirectX::XMFLOAT3 &holePos, float holeRadius) {
  game::physics::GolfCupShape cup;
  cup.centerX = holePos.x;
  cup.centerZ = holePos.z;
  cup.rimY = holePos.y;
  cup.radius = holeRadius;
  return cup;
}

// ホール判定をまとめたヘルパー（テスト可能な純粋関数）
// カップは壁と底を持つ実際の穴なので、ボールが底まで落ち切った時点で成立する。
inline bool IsBallReadyForCupIn(const DirectX::XMFLOAT3 &ballPos,
                                const DirectX::XMFLOAT3 &holePos,
                                float holeRadius, float ballRadius) {
  return game::physics::IsBallSettledInCup(MakeCupShape(holePos, holeRadius),
                                           ballPos, ballRadius);
}

} // namespace game::scenes::cupin
