#pragma once

#include <DirectXMath.h>

namespace game::scenes::cupin {

// ホール判定をまとめたヘルパー（テスト可能な純粋関数）
inline bool IsBallReadyForCupIn(const DirectX::XMFLOAT3 &ballPos,
                                const DirectX::XMFLOAT3 &holePos,
                                float holeRadius, float speedSq) {
  float dx = ballPos.x - holePos.x;
  float dz = ballPos.z - holePos.z;
  float distSq = dx * dx + dz * dz;

  float captureRadius = holeRadius * 0.9f; // さらに広めに捕捉
  if (distSq > captureRadius * captureRadius)
    return false;

  float dy = ballPos.y - holePos.y;
  bool inCupDepth = (dy < 0.2f && dy > -0.6f); // 底に向かう広いレンジを許容
  bool slowEnough = speedSq < 0.05f;

  return inCupDepth && slowEnough;
}

} // namespace game::scenes::cupin
