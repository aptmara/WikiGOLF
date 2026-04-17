#include "MinimapController.h"
#include "../components/Transform.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/Camera.h"
#include "../components/Skybox.h"
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

namespace {
constexpr float kMinMapViewSpan = 5.0f;
} // namespace


void MinimapController::Initialize(Config cfg, core::GameContext &ctx) {
  m_cfg = cfg;
  m_minimapRenderer = std::make_unique<game::systems::MapSys>();
  m_minimapRenderer->Initialize(ctx.graphics.GetDevice(), 720, 720);
}

void MinimapController::InitializeUI(core::GameContext &ctx) {
  if (!m_minimapRenderer) return;

  m_minimapEntity = ctx.world.CreateEntity();
  auto &ui = ctx.world.Add<UIImage>(m_minimapEntity);
  ui.textureSRV = m_minimapRenderer->GetSRV();
  ui.width = game::ui::kMinimapWidth;
  ui.height = game::ui::kMinimapHeight;
  ui.x = game::ui::kMinimapX;
  ui.y = game::ui::kMinimapY;
  ui.visible = true;
  ui.layer = game::ui::kLayerMinimap;

  m_minimapMarkerEntity = ctx.world.CreateEntity();
  auto &marker = ctx.world.Add<UIText>(m_minimapMarkerEntity);
  marker.text = L"◎";
  marker.x = ui.x + ui.width * 0.5f - 10.0f;
  marker.y = ui.y + ui.height * 0.5f - 10.0f;
  marker.style = graphics::TextStyle::Guide();
  marker.style.fontSize = game::ui::kMinimapMarkerSize;
  marker.style.color = game::ui::kColorWarning;
  marker.layer = game::ui::kLayerMarker;
}

void MinimapController::ClearHoleIcons(core::GameContext &ctx) {
  for (auto &icon : m_mapHoleIcons) {
    ctx.world.DestroyEntity(icon.iconEntity);
  }
  m_mapHoleIcons.clear();
}

void MinimapController::AddHoleIcon(core::GameContext &ctx, float x, float z, const std::string& linkTarget, bool isTargetHole) {
  auto iconEntity = ctx.world.CreateEntity();
  auto &ui = ctx.world.Add<UIImage>(iconEntity);
  
  if (isTargetHole) {
    ui = UIImage::Create("ui_map_hole_target.png", 0.0f, 0.0f);
    ui.width = 48.0f;
    ui.height = 48.0f;
    ui.layer = game::ui::kLayerMarker + 1; 
  } else {
    ui = UIImage::Create("ui_map_hole_normal.png", 0.0f, 0.0f);
    ui.width = 24.0f;
    ui.height = 24.0f;
    ui.alpha = 0.8f;
    ui.layer = game::ui::kLayerMarker;
  }
  ui.visible = false;
  
  m_mapHoleIcons.push_back({iconEntity, {x, z}, isTargetHole});
}

void MinimapController::ToggleMapView(core::GameContext &ctx, ecs::Entity skyboxEntity) {
  m_isMapView = !m_isMapView;
  
  if (ctx.world.IsAlive(skyboxEntity)) {
    auto* skybox = ctx.world.Get<components::Skybox>(skyboxEntity);
    if (skybox) {
      m_mapViewSkyboxState.Sync(m_isMapView, *skybox);
    }
  }

  if (m_isMapView) {
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
  }
}

void MinimapController::UpdateMapCamera(core::GameContext &ctx, float fieldWidth, float fieldDepth) {
  if (!ctx.world.IsAlive(m_cfg.cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(m_cfg.cameraEntity);
  if (!camT)
    return;

  // フィールド中央の真上から見下ろす
  float extent = std::max(fieldWidth, fieldDepth);
  float viewSpan = extent / std::max(0.01f, m_mapZoom);
  viewSpan = std::clamp(viewSpan, kMinMapViewSpan, extent * 6.0f);
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

void MinimapController::UpdateMinimap(core::GameContext &ctx, float fieldWidth, float fieldDepth, const DirectX::XMFLOAT3& shotDirection) {
  if (!m_minimapRenderer)
    return;

  // カメラ外でも現在地がわかるよう、マーカーをUI上にプロット
  UIImage *ui = ctx.world.Get<UIImage>(m_minimapEntity);
  UIText *marker = ctx.world.Get<UIText>(m_minimapMarkerEntity);
  Transform *ballT = ctx.world.Get<Transform>(m_cfg.ballEntity);

  game::systems::MapRenderParams params;
  params.center = {m_mapCenter.x, 0.0f, m_mapCenter.y};
  params.extent = std::max(fieldWidth, fieldDepth);
  params.zoom = m_mapZoom;
  params.heightScale = m_isMapView ? 1.8f : 2.2f;
  params.orthoPadding = 1.3f;
  params.highlightBall = true;
  m_minimapRenderer->Render(ctx, params);

  if (ui && marker && ballT) {
    // MapSysの射影計算と同一の値を使ってUI座標に変換する
    float viewSpan = std::clamp(params.extent / std::max(0.01f, params.zoom),
                                params.extent * 0.01f, params.extent * 6.0f);
    float orthoWidth =
        std::max(viewSpan * params.orthoPadding, viewSpan * 0.5f);
    // GetProjMatrixが幅・高さに1.2倍を掛ける点を反映
    float clipWidth = orthoWidth * 1.2f;

    float dx = ballT->position.x - params.center.x;
    float dz = ballT->position.z - params.center.z;
    
    // UV座標の計算 (0.0 - 1.0)
    // 射影行列の clipWidth は中心から端までの距離に対応するため、0.5fを足して正規化
    float u = 0.5f + (dx / clipWidth); 
    float v = 0.5f - (dz / clipWidth);

    u = std::clamp(u, 0.02f, 0.98f); 
    v = std::clamp(v, 0.02f, 0.98f);
    marker->x = ui->x + u * ui->width - 10.0f;
    marker->y = ui->y + v * ui->height - 10.0f;
    marker->visible = ui->visible;
  }

  // === Phase 2: マーカーパルスアニメーション ===
  if (marker && ui && ui->visible) {
    m_markerPulseTimer += ctx.dt;
    float pulse = 1.0f + game::ui::kMarkerPulseScale * std::sin(m_markerPulseTimer * game::ui::kMarkerPulseSpeed);
    marker->style.fontSize = game::ui::kMinimapMarkerSize * pulse;

    // 色もパルス
    float alphaPulse = 0.8f + 0.2f * std::sin(m_markerPulseTimer * game::ui::kMarkerPulseSpeed);
    marker->style.color.w = alphaPulse;
  }

  // === Phase 3: 座標・距離表示（マップビュー時のみ） ===
  if (m_isMapView && ui && ballT) {
    int mouseX = ctx.input.GetMousePosition().x;
    int mouseY = ctx.input.GetMousePosition().y;

    // マウスがマップ内かチェック
    bool inMap = (mouseX >= ui->x && mouseX <= ui->x + ui->width &&
                  mouseY >= ui->y && mouseY <= ui->y + ui->height);

    auto *coordTxt = ctx.world.Get<UIText>(m_mapCoordText);
    auto *distTxt = ctx.world.Get<UIText>(m_mapDistanceText);

    if (inMap && coordTxt && distTxt) {
      float u = (mouseX - ui->x) / ui->width;
      float v = (mouseY - ui->y) / ui->height;

      // UV→ワールド座標
      float clipWidth = params.extent / std::max(0.01f, params.zoom);
      clipWidth = std::clamp(clipWidth, 5.0f, params.extent * 6.0f);

      float worldX = params.center.x + (u - 0.5f) * clipWidth;
      float worldZ = params.center.z - (v - 0.5f) * clipWidth;

      // 座標表示（ミニマップ内固定位置）
      coordTxt->x = ui->x + 10.0f;
      coordTxt->y = ui->y + ui->height - 35.0f;
      coordTxt->text = std::format(L"座標: ({:.1f}, {:.1f})", worldX, worldZ);
      coordTxt->visible = true;

      // ボールからの距離
      float dx = worldX - ballT->position.x;
      float dz = worldZ - ballT->position.z;
      float distance = std::sqrt(dx * dx + dz * dz);

      distTxt->x = ui->x + 10.0f;
      distTxt->y = ui->y + ui->height - 20.0f;
      distTxt->text = std::format(L"距離: {:.1f}m", distance);
      distTxt->visible = true;
    } else {
      if (coordTxt)
        coordTxt->visible = false;
      if (distTxt)
        distTxt->visible = false;
    }
  } else {
    // 非マップビューでは非表示
    if (auto *coordTxt = ctx.world.Get<UIText>(m_mapCoordText))
      coordTxt->visible = false;
    if (auto *distTxt = ctx.world.Get<UIText>(m_mapDistanceText))
      distTxt->visible = false;
  }

  // === Phase 2: ズームインジケーター（マップビュー時のみ） ===
  auto *zoomBg = ctx.world.Get<UIImage>(m_mapZoomIndicatorBg);
  auto *zoomTxt = ctx.world.Get<UIText>(m_mapZoomIndicatorText);

  if (m_isMapView && zoomBg && zoomTxt) {
    // ズーム率を計算（基準1.0 = 100%）
    int zoomPercent = (int)(m_mapZoom * 100.0f);
    zoomTxt->text = std::format(L"{}%", zoomPercent);

    zoomBg->visible = true;
    zoomTxt->visible = true;
  } else {
    if (zoomBg)
      zoomBg->visible = false;
    if (zoomTxt)
      zoomTxt->visible = false;
  }

  // === Phase 3: ヘルプパネルフェードイン/アウト ===
  static float helpFadeAlpha = 0.0f;
  float targetHelpAlpha = m_mapHelpVisible ? 1.0f : 0.0f;
  float fadeSpeed = game::ui::kFadeSpeed; 
  helpFadeAlpha += (targetHelpAlpha - helpFadeAlpha) * fadeSpeed * ctx.dt;

  bool shouldShowHelp = helpFadeAlpha > 0.01f;

  auto *helpBg = ctx.world.Get<UIImage>(m_mapHelpPanelBg);
  auto *helpTitle = ctx.world.Get<UIText>(m_mapHelpTitle);

  if (helpBg) {
    helpBg->visible = shouldShowHelp;
    helpBg->alpha = helpFadeAlpha * game::ui::kMapHelpPanelAlpha;
  }

  if (helpTitle) {
    helpTitle->visible = shouldShowHelp;
    helpTitle->style.color.w = helpFadeAlpha;
  }

  for (auto lineE : m_mapHelpLines) {
    if (auto *line = ctx.world.Get<UIText>(lineE)) {
      line->visible = shouldShowHelp;
      line->style.color.w = helpFadeAlpha;
    }
  }
}

void MinimapController::SyncMapCenterToBall(core::GameContext &ctx, float dt, float fieldWidth, float fieldDepth, bool forceSnap) {
  DirectX::XMFLOAT2 targetCenter{0.0f, 0.0f};
  if (auto *ballT = ctx.world.Get<Transform>(m_cfg.ballEntity)) {
    targetCenter = {ballT->position.x, ballT->position.z};
  }

  targetCenter = game::utils::ClampMapCenter(targetCenter, fieldWidth,
                                             fieldDepth, 2.0f);

  if (forceSnap || dt <= 0.0f) {
    m_mapCenter = targetCenter;
    return;
  }

  float lerp = 1.0f - std::exp(-game::ui::kLerpSpeedMinimap * dt);
  m_mapCenter.x += (targetCenter.x - m_mapCenter.x) * lerp;
  m_mapCenter.y += (targetCenter.y - m_mapCenter.y) * lerp;
}

void MinimapController::ProcessInput(core::GameContext &ctx, int mouseX, int mouseY, float fieldWidth, float fieldDepth, ecs::Entity skyboxEntity) {
  if (ctx.input.GetKeyDown('M')) {
    ToggleMapView(ctx, skyboxEntity);
    LOG_INFO("MinimapController", "Map view: {}", m_isMapView ? "ON" : "OFF");
  }

  if (!m_isMapView) return;

  // --- マップビュー操作処理 ---
  if (ctx.input.GetKeyDown(VK_ESCAPE)) {
    m_isMapView = false;
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
    m_targetMapZoom = std::clamp(fieldWidth / std::max(10.0f, fieldWidth * 0.25f), game::ui::kMapMinZoom, game::ui::kMapMaxZoom);
  }

  if (ctx.input.GetKeyDown('F')) {
    SyncMapCenterToBall(ctx, 0.0f, fieldWidth, fieldDepth, true);
    float extent = std::max(fieldWidth, fieldDepth);
    m_targetMapZoom = extent / 220.0f * 0.9f;
    m_targetMapZoom = std::clamp(m_targetMapZoom, game::ui::kMapMinZoom, game::ui::kMapMaxZoom);
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
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, game::ui::kMapMaxZoom);
  }
  if (ctx.input.GetKeyDown(VK_OEM_PLUS) || ctx.input.GetKeyDown(VK_ADD)) {
    m_targetMapZoom *= 1.12f;
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, game::ui::kMapMaxZoom);
  }
  if (ctx.input.GetKeyDown(VK_OEM_MINUS) || ctx.input.GetKeyDown(VK_SUBTRACT)) {
    m_targetMapZoom /= 1.12f;
    m_targetMapZoom = game::utils::ClampMapZoom(m_targetMapZoom, game::ui::kMapMinZoom, game::ui::kMapMaxZoom);
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

// -----------------------------------------------------------------
// MinimapController::SetVisible
// 入力: visible=false → ロード中にミニマップ一切を非表示にする
// 変更: UIImage/UIText の visible フラグを一括更新
// 出力: なし（副作用: ECS コンポーネントの visible 変更）
// -----------------------------------------------------------------
namespace game::controllers {

void MinimapController::SetVisible(core::GameContext& ctx, bool visible) {
    auto setUIImg = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* img = ctx.world.Get<components::UIImage>(e)) img->visible = v;
    };
    auto setUITxt = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* t = ctx.world.Get<components::UIText>(e)) t->visible = v;
    };

    // ミニマップ本体とマーカー
    setUIImg(m_minimapEntity,       visible);
    setUITxt(m_minimapMarkerEntity, visible);
    setUITxt(m_minimapHelpEntity,   visible);

    // ズームインジケーター
    setUIImg(m_mapZoomIndicatorBg,   false); // マップビュー時のみ表示のため常にfalse
    setUITxt(m_mapZoomIndicatorText, false);

    // 座標・距離テキスト（マップビュー時のみ）
    setUITxt(m_mapCoordText,    false);
    setUITxt(m_mapDistanceText, false);

    // ヘルプパネル
    setUIImg(m_mapHelpPanelBg, false);
    setUITxt(m_mapHelpTitle,   false);
    for (auto e : m_mapHelpLines) setUITxt(e, false);

    // ホールアイコン
    for (auto& icon : m_mapHoleIcons) {
        setUIImg(icon.iconEntity, false);
    }
}

} // namespace game::controllers
