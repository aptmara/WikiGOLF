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
#include "../utils/CameraProjection.h"
#include "../utils/UIConstants.h"
#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

namespace game::controllers::minimap_detail {

inline constexpr float kMinMapViewSpan = 5.0f;
inline constexpr float kHudMinimapPadding = 1.10f;
inline constexpr float kMapRenderScreenScale = 1.2f;
inline constexpr float kScreenWidth = 1280.0f;
inline constexpr float kScreenHeight = 720.0f;

enum class FlagKind : std::size_t {
  Target,
  OneHop,
  TwoHops,
  ThreeToFiveHops,
  SixOrMoreHops,
  Unknown,
  Count,
};

inline constexpr std::size_t kFlagKindCount =
    static_cast<std::size_t>(FlagKind::Count);

/** @brief 既存の旗色規則と同じ条件で、表示フィルターの種類を返します。*/
inline FlagKind ClassifyFlag(bool isTargetHole, int hopsToTarget) {
  if (isTargetHole) {
    return FlagKind::Target;
  }
  if (hopsToTarget == 1) {
    return FlagKind::OneHop;
  }
  if (hopsToTarget == 2) {
    return FlagKind::TwoHops;
  }
  if (hopsToTarget >= 3 && hopsToTarget <= 5) {
    return FlagKind::ThreeToFiveHops;
  }
  if (hopsToTarget > 5) {
    return FlagKind::SixOrMoreHops;
  }
  return FlagKind::Unknown;
}

/**
 * @brief 選択中の旗が存在しない場合だけ、存在する最良の1種類を一時表示します。
 * @details 戻り値だけを補完し、ユーザーの選択配列は変更しません。
 */
inline std::array<bool, kFlagKindCount> ResolveFlagVisibility(
    const std::array<bool, kFlagKindCount> &selected,
    const std::array<bool, kFlagKindCount> &available) {
  std::array<bool, kFlagKindCount> visible{};
  bool hasSelectedFlag = false;
  for (std::size_t i = 0; i < kFlagKindCount; ++i) {
    visible[i] = selected[i] && available[i];
    hasSelectedFlag = hasSelectedFlag || visible[i];
  }
  if (hasSelectedFlag) {
    return visible;
  }
  for (std::size_t i = 0; i < kFlagKindCount; ++i) {
    if (available[i]) {
      visible[i] = true;
      break;
    }
  }
  return visible;
}

/**
 * @brief HUDミニマップに必要なワールド表示範囲を計算します。
*/
inline float ComputeMinimapWorldSpan(
    const game::systems::MapRenderParams &params) {
  if (params.visibleWidth > 0.0f || params.visibleDepth > 0.0f) {
    return std::max(params.visibleWidth, params.visibleDepth);
  }
  float viewSpan = params.extent / std::max(0.01f, params.zoom);
  viewSpan = std::clamp(viewSpan, kMinMapViewSpan, params.extent * 6.0f);
  return std::max(viewSpan * params.orthoPadding, viewSpan * 0.5f) *
         kMapRenderScreenScale;
}

inline DirectX::XMFLOAT2 ComputeMinimapWorldSize(
    const game::systems::MapRenderParams &params) {
  if (params.visibleWidth > 0.0f && params.visibleDepth > 0.0f) {
    return {params.visibleWidth, params.visibleDepth};
  }
  const float span = ComputeMinimapWorldSpan(params);
  return {span, span};
}

/**
 * @brief ワールド座標をミニマップの正規化座標へ投影します。
*/
inline bool ProjectToMinimap(float worldX, float worldZ,
                             const game::systems::MapRenderParams &params,
                             float &outU, float &outV) {
  const auto size = ComputeMinimapWorldSize(params);
  if (size.x <= 0.0f || size.y <= 0.0f) {
    return false;
  }

  outU = 0.5f + (worldX - params.center.x) / size.x;
  outV = 0.5f - (worldZ - params.center.z) / size.y;
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
 * @note 投影はエイムピンのレイキャスト(ScreenRaycast)と同じ変換規則
 *       （実クライアント解像度のビューポート＋実描画と同じアスペクト比）で
 *       行う。どちらかが仮想解像度のまま投影していると、16:9以外のウィンドウ
 *       では中クリックした位置とマップ上のマーカーが横方向にずれる。
 * @return カメラ後方や投影範囲外の場合はfalse
*/
inline bool ProjectWorldToMapViewScreen(core::GameContext &ctx,
                                        ecs::Entity cameraEntity,
                                        const DirectX::XMFLOAT3 &worldPos,
                                        float &outScreenX, float &outScreenY) {
  return game::utils::ProjectWorldToVirtualScreen(ctx, cameraEntity, worldPos,
                                                  outScreenX, outScreenY);
}

/**
 * @brief HUDミニマップの描画パラメータを作成します。
*/
inline game::systems::MapRenderParams BuildHudMinimapParams(
    float fieldWidth, float fieldDepth) {
  game::systems::MapRenderParams params;
  params.center = {0.0f, 0.0f, 0.0f};
  params.extent = std::max(fieldWidth, fieldDepth);
  params.zoom = 1.0f;
  params.heightScale = 2.2f;
  params.orthoPadding = 1.3f;
  params.highlightBall = true;

  constexpr float panelAspect =
      game::ui::kMinimapWidth / game::ui::kMinimapHeight;
  const float paddedDepth =
      std::max(fieldDepth, fieldWidth / panelAspect) * kHudMinimapPadding;
  params.visibleDepth = std::max(1.0f, paddedDepth);
  params.visibleWidth = params.visibleDepth * panelAspect;
  return params;
}

struct MarkerBounds {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
};

inline bool ContainsScreenPoint(const MarkerBounds &bounds, float screenX,
                                float screenY, float padding = 0.0f) {
  return screenX >= bounds.x - padding &&
         screenX <= bounds.x + bounds.width + padding &&
         screenY >= bounds.y - padding &&
         screenY <= bounds.y + bounds.height + padding;
}

inline bool UnprojectHudMinimap(float screenX, float screenY,
                                const MarkerBounds &bounds,
                                const game::systems::MapRenderParams &params,
                                float &outWorldX, float &outWorldZ) {
  if (bounds.width <= 0.0f || bounds.height <= 0.0f ||
      !ContainsScreenPoint(bounds, screenX, screenY)) {
    return false;
  }
  const auto size = ComputeMinimapWorldSize(params);
  const float u = (screenX - bounds.x) / bounds.width;
  const float v = (screenY - bounds.y) / bounds.height;
  outWorldX = params.center.x + (u - 0.5f) * size.x;
  outWorldZ = params.center.z - (v - 0.5f) * size.y;
  return true;
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

/**
 * @brief 全体マップ表示時のマーカー描画領域を取得します。
*/
inline MarkerBounds GetMapViewMarkerBounds() {
  constexpr float margin = 24.0f;
  return {margin, margin, kScreenWidth - margin * 2.0f,
          kScreenHeight - margin * 2.0f};
}

} // namespace game::controllers::minimap_detail
