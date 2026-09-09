#pragma once
/**
 * @file MinimapControllerInternals.h
 * @brief ミニマップ計算処理の内部共有定義
*/

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/Transform.h"
#include "../systems/MapSys.h"
#include "../utils/UIConstants.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::controllers::minimap_detail {

inline constexpr float kMinMapViewSpan = 5.0f;
inline constexpr float kLongArticleFieldWidth = 190.0f;
inline constexpr float kHudMinimapWideArticleSpan = 120.0f;
inline constexpr float kHudMinimapPadding = 1.10f;
inline constexpr float kMapRenderScreenScale = 1.2f;
inline constexpr float kScreenWidth = 1280.0f;
inline constexpr float kScreenHeight = 720.0f;

/**
 * @brief HUDミニマップに必要なワールド表示範囲を計算します。
*/
inline float ComputeMinimapWorldSpan(
    const game::systems::MapRenderParams &params) {
  float viewSpan = params.extent / std::max(0.01f, params.zoom);
  viewSpan = std::clamp(viewSpan, kMinMapViewSpan, params.extent * 6.0f);
  return std::max(viewSpan * params.orthoPadding, viewSpan * 0.5f) *
         kMapRenderScreenScale;
}

/**
 * @brief 指定した表示範囲に合わせたズーム率を計算します。
*/
inline float ComputeZoomForVisibleSpan(float extent, float desiredVisibleSpan,
                                       float orthoPadding) {
  const float safeExtent = std::max(1.0f, extent);
  const float padding =
      std::max(1.0f, orthoPadding) * kMapRenderScreenScale;
  const float rawViewSpan = std::max(5.0f, desiredVisibleSpan / padding);
  return safeExtent / rawViewSpan;
}

/**
 * @brief ボール位置をマップ座標の中心として取得します。
*/
inline DirectX::XMFLOAT2 GetBallMapCenter(core::GameContext &ctx,
                                          ecs::Entity ballEntity) {
  if (auto *ballTransform =
          ctx.world.Get<game::components::Transform>(ballEntity)) {
    return {ballTransform->position.x, ballTransform->position.z};
  }
  return {0.0f, 0.0f};
}

/**
 * @brief ボール中心表示に必要な全体範囲を計算します。
*/
inline float ComputeBallCenteredFullSpan(const DirectX::XMFLOAT2 &center,
                                         float fieldWidth, float fieldDepth) {
  const float halfWidth = std::max(1.0f, fieldWidth * 0.5f);
  const float halfDepth = std::max(1.0f, fieldDepth * 0.5f);
  const float spanX = (halfWidth + std::abs(center.x)) * 2.0f;
  const float spanZ = (halfDepth + std::abs(center.y)) * 2.0f;
  return std::max(spanX, spanZ) * kHudMinimapPadding;
}

/**
 * @brief ワールド座標をミニマップの正規化座標へ投影します。
*/
inline bool ProjectToMinimap(float worldX, float worldZ,
                             const game::systems::MapRenderParams &params,
                             float &outU, float &outV) {
  const float span = ComputeMinimapWorldSpan(params);
  if (span <= 0.0f) {
    return false;
  }

  outU = 0.5f + (worldX - params.center.x) / span;
  outV = 0.5f - (worldZ - params.center.z) / span;
  return outU >= 0.0f && outU <= 1.0f && outV >= 0.0f && outV <= 1.0f;
}

/**
 * @brief 全体マップビュー中、実カメラの透視投影でワールド座標を画面座標
 * (仮想解像度1280x720)へ投影します。
 * @details 全体マップビューはメインカメラ(m_cameraEntity)を高所へ移動して
 *          ピッチを傾けた「実カメラによる俯瞰」であり、正射影ではない。
 *          そのためHUD常時ミニマップ(MapSys/正射影オフスクリーン描画)用の
 *          ProjectToMinimapとは別に、実際のView/Projection行列で投影する
 *          必要がある。これを使わずにProjectToMinimapを流用すると、傾いた
 *          カメラの遠近感が無視され、ボールやアイコンの表示位置がクリック
 *          位置や実際の見た目とズレる。
 * @return カメラ後方や投影範囲外の場合はfalse
*/
inline bool ProjectWorldToMapViewScreen(core::GameContext &ctx,
                                        ecs::Entity cameraEntity,
                                        const DirectX::XMFLOAT3 &worldPos,
                                        float &outScreenX, float &outScreenY) {
  using namespace DirectX;

  auto *camTransform =
      ctx.world.Get<game::components::Transform>(cameraEntity);
  auto *camComp = ctx.world.Get<game::components::Camera>(cameraEntity);
  if (!camTransform || !camComp) {
    return false;
  }

  constexpr float kVirtualWidth = 1280.0f;
  constexpr float kVirtualHeight = 720.0f;

  const XMMATRIX view = camComp->GetViewMatrix(*camTransform);
  const XMMATRIX proj = camComp->GetProjectionMatrix();
  const XMMATRIX world = XMMatrixIdentity();

  const XMVECTOR worldV = XMLoadFloat3(&worldPos);
  const XMVECTOR screenV =
      XMVector3Project(worldV, 0.0f, 0.0f, kVirtualWidth, kVirtualHeight,
                       0.0f, 1.0f, proj, view, world);

  XMFLOAT3 screen;
  XMStoreFloat3(&screen, screenV);
  if (screen.z < 0.0f || screen.z > 1.0f) {
    return false; // カメラの後方、またはFar超え
  }
  outScreenX = screen.x;
  outScreenY = screen.y;
  return true;
}

/**
 * @brief HUDミニマップの描画パラメータを作成します。
*/
inline game::systems::MapRenderParams BuildHudMinimapParams(
    core::GameContext &ctx, ecs::Entity ballEntity, float fieldWidth,
    float fieldDepth) {
  game::systems::MapRenderParams params;
  params.center = {0.0f, 0.0f, 0.0f};
  params.extent = std::max(fieldWidth, fieldDepth);
  params.zoom = 1.0f;
  params.heightScale = 2.2f;
  params.orthoPadding = 1.3f;
  params.highlightBall = true;

  const DirectX::XMFLOAT2 ballCenter = GetBallMapCenter(ctx, ballEntity);
  params.center = {ballCenter.x, 0.0f, ballCenter.y};

  const bool canFitWholeMap = fieldWidth <= kLongArticleFieldWidth;
  float desiredSpan = kHudMinimapWideArticleSpan;
  if (canFitWholeMap) {
    desiredSpan = ComputeBallCenteredFullSpan(ballCenter, fieldWidth, fieldDepth);
  }
  params.zoom =
      ComputeZoomForVisibleSpan(params.extent, desiredSpan, params.orthoPadding);
  return params;
}

/**
 * @brief 全体マップビューの描画パラメータを作成します。
*/
inline game::systems::MapRenderParams BuildMapViewParams(
    const DirectX::XMFLOAT2 &center, float zoom, float fieldWidth,
    float fieldDepth) {
  game::systems::MapRenderParams params;
  params.center = {center.x, 0.0f, center.y};
  params.extent = std::max(fieldWidth, fieldDepth);
  params.zoom = zoom;
  params.heightScale = 1.8f;
  params.orthoPadding = 1.3f;
  params.highlightBall = true;
  return params;
}

struct MarkerBounds {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

/**
 * @brief 全体マップ表示時のマーカー描画領域を取得します。
*/
inline MarkerBounds GetMapViewMarkerBounds() {
  constexpr float margin = 24.0f;
  return {margin, margin, kScreenWidth - margin * 2.0f,
          kScreenHeight - margin * 2.0f};
}

} // namespace game::controllers::minimap_detail
