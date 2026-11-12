#pragma once
/**
 * @file ShotGuideZone.h
 * @brief 「だいたいこのあたりに止まる」範囲
 *
 * 実際のショットはインパクトの出来で距離と左右がぶれるので、予測を1点で
 * 示しても当たらない。ぶれの大きさ（ゲームの判定の規則そのもの）から、
 * 止まりそうな範囲をだ円で求める。ふちの点は地面の高さに合わせてあり、
 * 起伏のある場所でも地面に沿って描ける。
*/

#include "ShotGuidePath.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace game::guide {

/** @brief ぶれの大きさ*/
struct ShotGuideSpread {
  float side = 0.05f;  ///< 左右のぶれ（飛距離に対する割合）
  float shortBy = 0.05f; ///< 短くなるぶれ（飛距離に対する割合）。長くはならない
  float minHalfSize = 0.28f; ///< 範囲の半径の下限[m]（パターでも見える大きさ）
  float rollSlack = 0.02f;   ///< 転がりの読みづらさぶんの前後のゆとり（割合）
};

/** @brief 止まりそうな範囲（地面に沿っただ円）*/
struct ShotGuideZone {
  bool valid = false;
  DirectX::XMFLOAT3 center{0.0f, 0.0f, 0.0f};  ///< 中心（地表）
  DirectX::XMFLOAT3 forward{0.0f, 0.0f, 1.0f}; ///< 打つ向き（水平・正規化済み）
  DirectX::XMFLOAT3 normal{0.0f, 1.0f, 0.0f};  ///< 中心の地面の向き
  float halfLength = 0.0f; ///< 打つ向きの半径[m]
  float halfWidth = 0.0f;  ///< 左右の半径[m]
  std::vector<DirectX::XMFLOAT3> rim; ///< ふちの点（地表・一周ぶん。最後は最初へつなぐ）
};

/**
 * @brief 道すじとぶれの大きさから、止まりそうな範囲を求める。
 * @param rimCount ふちの点の数
*/
inline ShotGuideZone BuildShotGuideZone(
    const ShotGuidePath &path, const ShotGuideSpread &spread,
    game::systems::WikiTerrainSystem *terrain,
    const game::physics::FlatGroundParams &flatGround, int rimCount = 32) {
  ShotGuideZone zone;
  if (!path.valid || rimCount < 3) {
    return zone;
  }
  const DirectX::XMFLOAT3 &start = path.Start().position;
  const DirectX::XMFLOAT3 &rest = path.Rest().position;
  float fx = rest.x - start.x;
  float fz = rest.z - start.z;
  const float distance = std::sqrt(fx * fx + fz * fz);
  if (distance > 1.0e-4f) {
    fx /= distance;
    fz /= distance;
  } else {
    fx = 0.0f;
    fz = 1.0f;
  }

  // 短くなる側へだけぶれるので、中心は予測の少し手前に置く
  const float shortReach = distance * std::max(0.0f, spread.shortBy);
  const float slack = distance * std::max(0.0f, spread.rollSlack);
  zone.halfLength = std::max(spread.minHalfSize, shortReach * 0.5f + slack);
  zone.halfWidth =
      std::max(spread.minHalfSize, distance * std::max(0.0f, spread.side));
  const float back = std::min(shortReach * 0.5f, distance * 0.5f);
  const float cx = rest.x - fx * back;
  const float cz = rest.z - fz * back;

  auto groundAt = [&](float x, float z) {
    return path_detail::GroundHeight(terrain, flatGround, x, z);
  };
  zone.center = {cx, groundAt(cx, cz), cz};
  zone.forward = {fx, 0.0f, fz};
  zone.normal = path_detail::GroundNormal(terrain, cx, cz);

  zone.rim.reserve(static_cast<size_t>(rimCount));
  for (int i = 0; i < rimCount; ++i) {
    const float angle =
        6.2831853f * static_cast<float>(i) / static_cast<float>(rimCount);
    const float along = std::cos(angle) * zone.halfLength;
    const float across = std::sin(angle) * zone.halfWidth;
    // 右向きは (fz, -fx)
    const float x = cx + fx * along + fz * across;
    const float z = cz + fz * along - fx * across;
    zone.rim.push_back({x, groundAt(x, z), z});
  }
  zone.valid = true;
  return zone;
}

} // namespace game::guide
