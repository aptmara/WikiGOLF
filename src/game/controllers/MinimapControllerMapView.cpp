/**
 * @file MinimapControllerMapView.cpp
 * @brief MinimapControllerMapView の実装
*/

#include "MinimapController.h"
#include "MinimapControllerInternals.h"
#include "../components/Transform.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/Camera.h"
#include "../components/Skybox.h"
#include "../components/WikiComponents.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../ecs/World.h"
#include "../utils/UIConstants.h"
#include <algorithm>
#include <cmath>
#include <format>

using namespace DirectX;
using namespace game::components;


namespace game::controllers {

using namespace DirectX;
using namespace game::components;

/**
 * @brief マップビュー（全体俯瞰表示）のトグルを切り替えます。
*/
void MinimapController::ToggleMapView(core::GameContext &ctx, ecs::Entity skyboxEntity) {
  m_isMapView = !m_isMapView;

  if (auto *golfState = ctx.world.GetGlobal<GolfGameState>()) {
    golfState->isMapView = m_isMapView;
  }

  if (ctx.world.IsAlive(skyboxEntity)) {
    auto* skybox = ctx.world.Get<components::Skybox>(skyboxEntity);
    if (skybox) {
      m_mapViewSkyboxState.Sync(m_isMapView, *skybox);
    }
  }

  if (m_isMapView) {
    m_mapOpenHintTimer = game::ui::kMapOpenHintDuration;
    if (auto* openBg = ctx.world.Get<UIText>(m_mapOpenHintBg)) {
      openBg->visible = true;
      openBg->style.bgColor.w = 0.85f;
      openBg->style.borderColor.w = 0.45f;
    }
    if (auto* openTxt = ctx.world.Get<UIText>(m_mapOpenHintText)) {
      openTxt->visible = true;
      openTxt->style.color.w = 1.0f;
    }

    ctx.input.SetMouseCursorVisible(true);
    ctx.input.SetMouseCursorLocked(false);

    m_mapCenter.x = 0.0f;
    m_mapCenter.y = 0.0f;
    if (auto* ballT = ctx.world.Get<Transform>(m_cfg.ballEntity)) {
      m_mapCenter.x = ballT->position.x;
      m_mapCenter.y = ballT->position.z;
    }
    m_targetMapZoom = 1.0f;
    m_mapPanVelocity = {0.0f, 0.0f};
    m_mapHelpVisible = false;
  } else {
    m_mapOpenHintTimer = 0.0f;
    if (auto* openBg = ctx.world.Get<UIText>(m_mapOpenHintBg)) openBg->visible = false;
    if (auto* openTxt = ctx.world.Get<UIText>(m_mapOpenHintText)) openTxt->visible = false;

    // マップビュー中はHUDミニマップのオフスクリーン描画を止めていたため、
    // 復帰直後は変化駆動判定に関わらず必ず1回再描画する。
    m_minimapHasRenderedOnce = false;
    m_lastRenderedMoveCount = -1;
  }
}

/**
 * @brief トップビュー上の着弾点プレビュー(中心マーカー+ばらつき範囲円)の
 * 表示状態を設定します。実際の画面座標への配置はUpdateMinimap内で行う。
*/
void MinimapController::SetLandingPreview(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &landingCenter,
                                          float dispersionRadius, bool visible) {
  m_landingPreviewVisible = visible;
  m_landingPreviewCenter = landingCenter;
  m_landingPreviewRadius = std::max(dispersionRadius, 0.0f);

  if (!visible) {
    if (auto *range = ctx.world.Get<UIText>(m_landingPreviewRangeEntity)) {
      range->visible = false;
    }
    if (auto *center = ctx.world.Get<UIText>(m_landingPreviewCenterEntity)) {
      center->visible = false;
    }
  }
}

/**
 * @brief マップビュー有効時のカメラ位置を更新します。
*/
void MinimapController::UpdateMapCamera(core::GameContext &ctx, float fieldWidth, float fieldDepth) {
  if (!ctx.world.IsAlive(m_cfg.cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!camT)
    return;

  // フィールド中央の真上から見下ろす
  float extent = std::max(fieldWidth, fieldDepth);
  float viewSpan = extent / std::max(0.01f, m_mapZoom);
  viewSpan = std::clamp(viewSpan, minimap_detail::kMinMapViewSpan, extent * 6.0f);
  float height = std::max(viewSpan * 1.6f, 5.0f);

  // 目標位置: オフセット適用
  XMVECTOR targetPos = XMVectorSet(m_mapCenter.x, height, m_mapCenter.y, 0.0f);

  // 少し手前に引く (Zマイナス方向)
  targetPos = XMVectorAdd(targetPos, XMVectorSet(0, 0, -height * 0.3f, 0));

  // 現在位置から滑らかに補間
  XMVECTOR currentPos = XMLoadFloat3(&camT->position);
  XMVECTOR newPos =
      XMVectorLerp(currentPos, targetPos, game::ui::kLerpSpeedCamera * ctx.dt);
  XMStoreFloat3(&camT->position, newPos);

  // 斜め下を向く（ピッチ70度）
  XMVECTOR q =
      XMQuaternionRotationRollPitchYaw(XMConvertToRadians(70.0f), 0.0f, 0.0f);
  XMStoreFloat4(&camT->rotation, q);
}

} // namespace game::controllers

