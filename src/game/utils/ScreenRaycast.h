#pragma once
/**
 * @file ScreenRaycast.h
 * @brief スクリーン座標から地形へのレイキャストを行うユーティリティ
 * @details 三人称視点でのエイムピン設置（中クリック）用に、マウス位置から
 *          カメラのビュー/プロジェクション行列を使ってワールド空間のレイを
 *          組み立て、地形ハイトフィールドとの交点を線形探索+二分探索で求める。
 * @note マウス座標はUI用の仮想解像度(1280x720, 16:9)基準で渡される想定。
 *       実ウィンドウが16:9以外の場合、3D描画側の実アスペクト比とズレるため
 *       水平方向にわずかな誤差が生じ得る（着地点は常に地形上に補正されるため
 *       破綻はしない）。ウィンドウアスペクト非依存の厳密な解を求めるには、
 *       実クライアント座標への逆変換を別途行う必要がある。
*/

#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/Transform.h"
#include "../systems/WikiTerrainSystem.h"
#include <DirectXMath.h>
#include <algorithm>

namespace game::utils {

/**
 * @brief 仮想解像度(1280x720)上のスクリーン座標から、地形との交点を求めます。
 * @param ctx ゲームコンテキスト
 * @param cameraEntity Camera/Transformを持つカメラEntity
 * @param screenX 仮想解像度でのマウスX座標
 * @param screenY 仮想解像度でのマウスY座標
 * @param terrain 高さ参照用の地形システム（nullptrなら失敗）
 * @param maxDistance レイの最大探索距離（ワールド単位）
 * @param outWorldPos 交点のワールド座標（成功時のみ書き込む）
 * @return 交点が見つかった場合はtrue
*/
inline bool RaycastScreenToTerrain(core::GameContext &ctx,
                                   ecs::Entity cameraEntity, float screenX,
                                   float screenY,
                                   game::systems::WikiTerrainSystem *terrain,
                                   float maxDistance,
                                   DirectX::XMFLOAT3 &outWorldPos) {
  using namespace DirectX;

  if (!terrain) {
    return false;
  }

  auto *camTransform = ctx.world.Get<game::components::Transform>(cameraEntity);
  auto *camComp = ctx.world.Get<game::components::Camera>(cameraEntity);
  if (!camTransform || !camComp) {
    return false;
  }

  constexpr float kVirtualWidth = 1280.0f;
  constexpr float kVirtualHeight = 720.0f;

  const XMMATRIX view = camComp->GetViewMatrix(*camTransform);
  const XMMATRIX proj = camComp->GetProjectionMatrix();
  const XMMATRIX world = XMMatrixIdentity();

  const XMVECTOR nearPoint = XMVector3Unproject(
      XMVectorSet(screenX, screenY, 0.0f, 0.0f), 0.0f, 0.0f, kVirtualWidth,
      kVirtualHeight, 0.0f, 1.0f, proj, view, world);
  const XMVECTOR farPoint = XMVector3Unproject(
      XMVectorSet(screenX, screenY, 1.0f, 0.0f), 0.0f, 0.0f, kVirtualWidth,
      kVirtualHeight, 0.0f, 1.0f, proj, view, world);

  XMFLOAT3 origin, direction;
  XMStoreFloat3(&origin, nearPoint);
  XMVECTOR dirVec = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));
  XMStoreFloat3(&direction, dirVec);

  // 真上/水平方向を狙っている場合、地形と交わらないため早期に諦める。
  if (direction.y > -0.01f) {
    return false;
  }

  const float kStep = 1.5f;
  float prevT = 0.0f;
  float prevDiff = origin.y - terrain->GetHeight(origin.x, origin.z);
  if (prevDiff < 0.0f) {
    // カメラ自体が地形の下にある異常ケース。交点なしとして扱う。
    return false;
  }

  for (float t = kStep; t <= maxDistance; t += kStep) {
    const float x = origin.x + direction.x * t;
    const float z = origin.z + direction.z * t;
    const float y = origin.y + direction.y * t;
    const float diff = y - terrain->GetHeight(x, z);

    if (diff <= 0.0f) {
      // [prevT, t] の間で地表を跨いだので二分探索で詰める。
      float lo = prevT;
      float hi = t;
      for (int i = 0; i < 12; ++i) {
        const float mid = (lo + hi) * 0.5f;
        const float mx = origin.x + direction.x * mid;
        const float mz = origin.z + direction.z * mid;
        const float my = origin.y + direction.y * mid;
        const float midDiff = my - terrain->GetHeight(mx, mz);
        if (midDiff <= 0.0f) {
          hi = mid;
        } else {
          lo = mid;
        }
      }
      const float hitT = (lo + hi) * 0.5f;
      outWorldPos.x = origin.x + direction.x * hitT;
      outWorldPos.z = origin.z + direction.z * hitT;
      outWorldPos.y = terrain->GetHeight(outWorldPos.x, outWorldPos.z);
      return true;
    }

    prevT = t;
    prevDiff = diff;
  }

  return false;
}

} // namespace game::utils
