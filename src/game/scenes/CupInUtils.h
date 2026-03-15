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

  // 高さチェック：ホールより下かつ一定の深さ内にいる
  float dy = ballPos.y - holePos.y;
  bool inHoleRange = (dy < 0.0f && dy > -1.0f);

  // 速度チェック：十分に遅い場合のみカップインと判定
  bool isSlow = speedSq < 0.01f;

  return inHoleRange && isSlow;
}

} // namespace game::scenes::cupin
