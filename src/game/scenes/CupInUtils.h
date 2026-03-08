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

  float captureRadius = holeRadius * 0.9f;
  if (distSq > captureRadius * captureRadius)
    return false;

  // 高さチェック：穴の底にいる場合のみ判定（上を通過中は無視）
  float dy = ballPos.y - holePos.y;
  // dyが負（ホールより下）かつ穴の深さ内にいる
  bool inHoleBottom = (dy < -0.1f && dy > -1.0f);

  // 速度条件なし：穴の底にいれば判定
  return inHoleBottom;
}

} // namespace game::scenes::cupin
