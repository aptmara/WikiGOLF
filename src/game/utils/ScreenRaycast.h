#pragma once
/**
 * @file ScreenRaycast.h
 * @brief スクリーン座標から地形へのレイキャストを行うユーティリティ
 * @details 三人称視点・全体マップビューでのエイムピン設置（中クリック）や、
 *          マップ上のカーソル位置表示に使う。マウス位置からワールド空間の
 *          レイを組み立て（CameraProjection.h）、地形ハイトフィールドとの
 *          交点をレイマーチング+二分探索で求める。
 * @note 画面→ワールドの変換は実クライアント解像度のビューポートと、実描画と
 *       同じアスペクト比で行う。仮想解像度(1280x720)のまま逆投影すると、
 *       16:9以外のウィンドウやリサイズ後に横方向がずれる。
*/

#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../systems/WikiTerrainSystem.h"
#include "CameraProjection.h"
#include "GameplayPhysicsConstants.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace game::utils {

/** @brief レイマーチングを行う高さ場のXZ範囲*/
struct HeightFieldBounds {
  float minX = 0.0f;
  float maxX = 0.0f;
  float minZ = 0.0f;
  float maxZ = 0.0f;
};

/**
 * @brief レイをXZ範囲で切り取り、範囲内を通過する区間[tEnter, tExit]を求めます。
 * @details 範囲外では高さが定義されない（0扱いになる）ため、フィールドの外に
 *          誤った交点を作らないよう探索区間を先に絞る。
 * @return 範囲内を通る区間があればtrue
*/
inline bool ClipRayToBoundsXZ(const DirectX::XMFLOAT3 &origin,
                              const DirectX::XMFLOAT3 &direction,
                              const HeightFieldBounds &bounds, float maxDistance,
                              float &outEnter, float &outExit) {
  float tEnter = 0.0f;
  float tExit = maxDistance;

  const float originAxis[2] = {origin.x, origin.z};
  const float directionAxis[2] = {direction.x, direction.z};
  const float minAxis[2] = {bounds.minX, bounds.minZ};
  const float maxAxis[2] = {bounds.maxX, bounds.maxZ};

  for (int axis = 0; axis < 2; ++axis) {
    const float d = directionAxis[axis];
    const float o = originAxis[axis];
    if (std::abs(d) < 1e-6f) {
      if (o < minAxis[axis] || o > maxAxis[axis]) {
        return false; // その軸方向に進まないまま範囲外
      }
      continue;
    }
    float t0 = (minAxis[axis] - o) / d;
    float t1 = (maxAxis[axis] - o) / d;
    if (t0 > t1) {
      std::swap(t0, t1);
    }
    tEnter = (std::max)(tEnter, t0);
    tExit = (std::min)(tExit, t1);
    if (tExit <= tEnter) {
      return false;
    }
  }

  outEnter = tEnter;
  outExit = tExit;
  return true;
}

/**
 * @brief 高さ場との交点をレイマーチングで求めます。
 * @param sampleHeight (x, z) -> 地表の高さ を返す呼び出し可能オブジェクト
 * @param step マーチングの刻み幅（ハイトマップのセル間隔以下にすること）
 * @details レイの向きは問わない。上り坂を見上げている（方向が水平〜やや上向き）
 *          場合でも地形が持ち上がってくれば交差するため、向きによる足切りは
 *          行わない。始点が地表より下（カメラが斜面へめり込んでいる等）の
 *          ときは、いったん地表より上へ出てから交差判定を始める。
*/
template <typename HeightSampler>
inline bool MarchRayToHeightField(const DirectX::XMFLOAT3 &origin,
                                  const DirectX::XMFLOAT3 &direction,
                                  HeightSampler &&sampleHeight,
                                  const HeightFieldBounds &bounds,
                                  float maxDistance, float step,
                                  DirectX::XMFLOAT3 &outWorldPos) {
  float tEnter = 0.0f;
  float tExit = 0.0f;
  if (!ClipRayToBoundsXZ(origin, direction, bounds, maxDistance, tEnter,
                         tExit)) {
    return false;
  }

  step = std::clamp(step, 0.05f, 8.0f);

  const auto heightDiffAt = [&](float t) {
    const float x = origin.x + direction.x * t;
    const float z = origin.z + direction.z * t;
    const float y = origin.y + direction.y * t;
    return y - sampleHeight(x, z);
  };

  const int stepCount =
      static_cast<int>(std::ceil((tExit - tEnter) / step)) + 1;

  float prevT = tEnter;
  bool aboveSurface = heightDiffAt(tEnter) > 0.0f;

  for (int i = 1; i <= stepCount; ++i) {
    const float t = (std::min)(tEnter + step * static_cast<float>(i), tExit);
    const float diff = heightDiffAt(t);

    if (!aboveSurface) {
      // 地中から始まった区間。地表より上に出るまでは交差とみなさない。
      aboveSurface = diff > 0.0f;
      prevT = t;
      continue;
    }

    if (diff <= 0.0f) {
      // [prevT, t] の間で地表を跨いだので二分探索で詰める。
      float lo = prevT;
      float hi = t;
      for (int j = 0; j < 16; ++j) {
        const float mid = (lo + hi) * 0.5f;
        if (heightDiffAt(mid) <= 0.0f) {
          hi = mid;
        } else {
          lo = mid;
        }
      }
      const float hitT = (lo + hi) * 0.5f;
      outWorldPos.x = origin.x + direction.x * hitT;
      outWorldPos.z = origin.z + direction.z * hitT;
      outWorldPos.y = sampleHeight(outWorldPos.x, outWorldPos.z);
      return true;
    }

    prevT = t;
  }

  return false;
}

/**
 * @brief 仮想解像度(1280x720)上のスクリーン座標から、地形との交点を求めます。
 * @param ctx ゲームコンテキスト
 * @param cameraEntity Camera/Transformを持つカメラEntity
 * @param screenX 仮想解像度でのマウスX座標
 * @param screenY 仮想解像度でのマウスY座標
 * @param terrain 高さ参照用の地形システム（未生成なら失敗）
 * @param maxDistance レイの最大探索距離（ワールド単位）
 * @param outWorldPos 交点のワールド座標（成功時のみ書き込む）
 * @param outRayOrigin レイの始点（デバッグ可視化用、nullptrなら書き込まない。
 *                      レイが計算できた場合のみ書き込まれる）
 * @param outRayDirection レイの正規化方向（デバッグ可視化用、nullptrなら書き込まない。
 *                         レイが計算できた場合のみ書き込まれる）
 * @return 交点が見つかった場合はtrue
*/
inline bool RaycastScreenToTerrain(core::GameContext &ctx,
                                   ecs::Entity cameraEntity, float screenX,
                                   float screenY,
                                   game::systems::WikiTerrainSystem *terrain,
                                   float maxDistance,
                                   DirectX::XMFLOAT3 &outWorldPos,
                                   DirectX::XMFLOAT3 *outRayOrigin = nullptr,
                                   DirectX::XMFLOAT3 *outRayDirection = nullptr) {
  using namespace DirectX;

  if (!terrain) {
    return false;
  }

  XMMATRIX view;
  XMMATRIX proj;
  if (!GetCameraMatrices(ctx, cameraEntity, view, proj)) {
    return false;
  }

  const ScreenRay ray = BuildRayFromVirtualScreen(GetViewportMapping(ctx), view,
                                                  proj, screenX, screenY);

  // 以降の失敗パス（地形と交わらない等）でもデバッグ可視化ができるよう、
  // レイ自体が計算できた時点で出力しておく。
  if (outRayOrigin) {
    *outRayOrigin = ray.origin;
  }
  if (outRayDirection) {
    *outRayDirection = ray.direction;
  }

  const auto terrainData = terrain->GetTerrainData();
  if (!terrainData || terrainData->heightMap.empty()) {
    return false; // コース生成前はどこにも刺せない
  }

  // フィールド端ちょうどは高さが未定義（0が返る）のため、わずかに内側へ詰める。
  constexpr float kBoundsEpsilon = 0.01f;
  HeightFieldBounds bounds;
  bounds.minX = -terrainData->config.worldWidth * 0.5f + kBoundsEpsilon;
  bounds.maxX = terrainData->config.worldWidth * 0.5f - kBoundsEpsilon;
  bounds.minZ = -terrainData->config.worldDepth * 0.5f + kBoundsEpsilon;
  bounds.maxZ = terrainData->config.worldDepth * 0.5f - kBoundsEpsilon;

  // ハイトマップのセルを飛び越すと尾根を素通りしてしまうため、刻み幅は
  // セル間隔より細かくする。
  const float cellX =
      terrainData->config.worldWidth /
      static_cast<float>((std::max)(terrainData->config.resolutionX - 1, 1));
  const float cellZ =
      terrainData->config.worldDepth /
      static_cast<float>((std::max)(terrainData->config.resolutionZ - 1, 1));
  const float step = (std::min)(cellX, cellZ) * 0.75f;

  return MarchRayToHeightField(
      ray.origin, ray.direction,
      [terrain](float x, float z) {
        // プレイヤーが見ている面（記事テクスチャのオーバーレイ面）に合わせる。
        return game::physics::ToVisualSurfaceHeight(terrain->GetHeight(x, z));
      },
      bounds, maxDistance, step, outWorldPos);
}

} // namespace game::utils
