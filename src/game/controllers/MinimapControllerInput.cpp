/**
 * @file MinimapControllerInput.cpp
 * @brief MinimapControllerInput の実装
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
 * @brief ユーザー入力を処理しマップビュー操作に反映します。
*/
void MinimapController::ProcessInput(core::GameContext &ctx, int mouseX, int mouseY, float fieldWidth, float fieldDepth, ecs::Entity skyboxEntity) {
  if (ctx.input.GetKeyDown('M')) {
    ToggleMapView(ctx, skyboxEntity);
    const char *mapViewState = "OFF";
    if (m_isMapView) {
      mapViewState = "ON";
    }
    LOG_INFO("MinimapController", "Map view: {}", mapViewState);
  }

  if (!m_isMapView) return;

  // 動的な最大ズーム倍率を算出（フィールドサイズを考慮）
  float extent = (std::max)(fieldWidth, fieldDepth);
  m_maxMapZoom = game::utils::CalculateMaxMapZoom(extent, minimap_detail::kMinMapViewSpan, m_baseMaxMapZoom);

  // マップビューの操作処理
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    m_isMapView = false;
    if (auto *golfState = ctx.world.GetGlobal<GolfGameState>()) {
      golfState->isMapView = false;
    }
    m_mapOpenHintTimer = 0.0f;
    if (auto* openBg = ctx.world.Get<UIText>(m_mapOpenHintBg)) openBg->visible = false;
    if (auto* openTxt = ctx.world.Get<UIText>(m_mapOpenHintText)) openTxt->visible = false;

    if (ctx.world.IsAlive(skyboxEntity)) {
      auto* skybox = ctx.world.Get<components::Skybox>(skyboxEntity);
      if (skybox) {
        m_mapViewSkyboxState.Sync(m_isMapView, *skybox);
      }
    }
    LOG_INFO("MinimapController", "Map view closed (ESC)");
  }

  if (ctx.input.GetKeyDown(VK_SPACE) || ctx.input.GetKeyDown('C')) {
    SyncMapCenterToBall(ctx, 0.0f, fieldWidth, fieldDepth, true);
    m_targetMapZoom = std::clamp(fieldWidth / std::max(10.0f, fieldWidth * 0.25f), game::ui::kMapMinZoom, m_maxMapZoom);
  }

  if (ctx.input.GetKeyDown('F')) {
    SyncMapCenterToBall(ctx, 0.0f, fieldWidth, fieldDepth, true);
    float extentVal = (std::max)(fieldWidth, fieldDepth);
    m_targetMapZoom = extentVal / 220.0f * 0.9f;
    m_targetMapZoom = std::clamp(m_targetMapZoom, game::ui::kMapMinZoom, m_maxMapZoom);
  }

  if (ctx.input.GetKeyDown('0')) {
    m_targetMapZoom = 1.0f;
  }

  if (ctx.input.GetKeyDown(VK_OEM_2)) { // '/' or '?'
    m_mapHelpVisible = !m_mapHelpVisible;
  }

  float wheel = ctx.input.GetMouseScrollDelta();
  if (wheel != 0.0f) {
    m_targetMapZoom *= std::pow(1.12f, wheel);
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, m_maxMapZoom);
  }
  if (ctx.input.GetKeyDown(VK_OEM_PLUS) || ctx.input.GetKeyDown(VK_ADD)) {
    m_targetMapZoom *= 1.12f;
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, m_maxMapZoom);
  }
  if (ctx.input.GetKeyDown(VK_OEM_MINUS) || ctx.input.GetKeyDown(VK_SUBTRACT)) {
    m_targetMapZoom /= 1.12f;
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, m_maxMapZoom);
  }

  float zoomLerp = 1.0f - std::exp(-10.0f * ctx.dt);
  m_mapZoom += (m_targetMapZoom - m_mapZoom) * zoomLerp;

  // パン操作 (ドラッグ)
  if (ctx.input.GetMouseButton(0) || ctx.input.GetMouseButton(2)) {
    int deltaX = mouseX - m_prevMouseX;
    int deltaY = mouseY - m_prevMouseY;
    if (deltaX != 0 || deltaY != 0) {
      float extent = std::max(fieldWidth, fieldDepth);
      float viewSpan = extent / std::max(0.01f, m_mapZoom);
      float panSpeed = viewSpan * game::ui::kMapPanSpeedFactor;

      m_mapCenter.x -= deltaX * panSpeed;
      m_mapCenter.y += deltaY * panSpeed;
      m_mapPanVelocity = {0.0f, 0.0f};
    }
  }

  DirectX::XMFLOAT2 beforeClamp = m_mapCenter;
  m_mapCenter = game::utils::ClampMapCenter(m_mapCenter, fieldWidth, fieldDepth, 2.0f);

  if (beforeClamp.x != m_mapCenter.x || beforeClamp.y != m_mapCenter.y) {
    m_mapBoundaryHitTime = ctx.time;
    if (beforeClamp.x != m_mapCenter.x) m_mapPanVelocity.x *= -0.3f;
    if (beforeClamp.y != m_mapCenter.y) m_mapPanVelocity.y *= -0.3f;
  }

  m_prevMouseX = mouseX;
  m_prevMouseY = mouseY;
}

} // namespace game::controllers


