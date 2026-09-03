#include "MinimapController.h"
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

namespace {
constexpr float kMinMapViewSpan = 5.0f;
constexpr float kLongArticleFieldWidth = 190.0f;
constexpr float kHudMinimapWideArticleSpan = 120.0f;
constexpr float kHudMinimapPadding = 1.10f;
constexpr float kMapRenderScreenScale = 1.2f;
constexpr float kScreenWidth = 1280.0f;
constexpr float kScreenHeight = 720.0f;

float ComputeMinimapWorldSpan(const game::systems::MapRenderParams& params) {
  float viewSpan = params.extent / std::max(0.01f, params.zoom);
  viewSpan = std::clamp(viewSpan, 5.0f, params.extent * 6.0f);
  return std::max(viewSpan * params.orthoPadding, viewSpan * 0.5f) *
         kMapRenderScreenScale;
}

float ComputeZoomForVisibleSpan(float extent, float desiredVisibleSpan,
                                float orthoPadding) {
  const float safeExtent = std::max(1.0f, extent);
  const float padding = std::max(1.0f, orthoPadding) * kMapRenderScreenScale;
  const float rawViewSpan = std::max(5.0f, desiredVisibleSpan / padding);
  return safeExtent / rawViewSpan;
}

DirectX::XMFLOAT2 GetBallMapCenter(core::GameContext& ctx,
                                   ecs::Entity ballEntity) {
  if (auto* ballT = ctx.world.Get<Transform>(ballEntity)) {
    return {ballT->position.x, ballT->position.z};
  }
  return {0.0f, 0.0f};
}

float ComputeBallCenteredFullSpan(const DirectX::XMFLOAT2& center,
                                  float fieldWidth, float fieldDepth) {
  const float halfW = std::max(1.0f, fieldWidth * 0.5f);
  const float halfD = std::max(1.0f, fieldDepth * 0.5f);
  const float spanX = (halfW + std::abs(center.x)) * 2.0f;
  const float spanZ = (halfD + std::abs(center.y)) * 2.0f;
  return std::max(spanX, spanZ) * kHudMinimapPadding;
}

bool ProjectToMinimap(float worldX, float worldZ,
                      const game::systems::MapRenderParams& params,
                      float& outU, float& outV) {
  const float span = ComputeMinimapWorldSpan(params);
  if (span <= 0.0f) return false;

  outU = 0.5f + (worldX - params.center.x) / span;
  outV = 0.5f - (worldZ - params.center.z) / span;
  return outU >= 0.0f && outU <= 1.0f && outV >= 0.0f && outV <= 1.0f;
}

game::systems::MapRenderParams BuildHudMinimapParams(core::GameContext& ctx,
                                                     ecs::Entity ballEntity,
                                                     float fieldWidth,
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
  const float desiredSpan = canFitWholeMap
      ? ComputeBallCenteredFullSpan(ballCenter, fieldWidth, fieldDepth)
      : kHudMinimapWideArticleSpan;
  params.zoom = ComputeZoomForVisibleSpan(params.extent, desiredSpan,
                                          params.orthoPadding);
  return params;
}

game::systems::MapRenderParams BuildMapViewParams(const DirectX::XMFLOAT2& center,
                                                  float zoom,
                                                  float fieldWidth,
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

MarkerBounds GetMapViewMarkerBounds() {
  constexpr float margin = 24.0f;
  return {margin, margin, kScreenWidth - margin * 2.0f,
          kScreenHeight - margin * 2.0f};
}
} // namespace


/**
 * @brief コントローラーを初期化します。
 */
void MinimapController::Initialize(Config cfg, core::GameContext &ctx) {
  m_cfg = cfg;
  m_minimapRenderer = std::make_unique<game::systems::MapSys>();
  m_minimapRenderer->Initialize(ctx.graphics.GetDevice(), 256, 256);

  // 変化駆動レンダリングの状態をリセット（初回は必ず1回描画させる）
  m_minimapHasRenderedOnce = false;
  m_lastRenderedCenter = {0.0f, 0.0f, 0.0f};
  m_lastRenderedZoom = -1.0f;
  m_lastRenderedFieldWidth = -1.0f;
  m_lastRenderedFieldDepth = -1.0f;
  m_lastRenderedSpan = -1.0f;
  m_lastRenderedMoveCount = -1;
  m_pendingMoveCount = -1;
}

/**
 * @brief ミニマップのUI表示用エンティティを生成します。
 */
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

  // 自ボール内側ドットマーカー (●)
  m_minimapMarkerEntity = ctx.world.CreateEntity();
  auto &marker = ctx.world.Add<UIText>(m_minimapMarkerEntity);
  marker.text = L"●";
  marker.x = ui.x + ui.width * 0.5f - 10.0f;
  marker.y = ui.y + ui.height * 0.5f - 10.0f;
  marker.style = graphics::TextStyle::Guide();
  marker.style.fontSize = game::ui::kMinimapMarkerSize;
  marker.style.color = {0.18f, 0.85f, 1.0f, 1.0f}; // 蛍光シアン
  marker.layer = game::ui::kLayerMarker + 1;
  marker.visible = false;

  m_minimapBallIconEntity = ctx.world.CreateEntity();
  auto& ballIcon = ctx.world.Add<UIImage>(m_minimapBallIconEntity);
  ballIcon = UIImage::Create("golf_ball_icon_transparent.png", marker.x, marker.y);
  ballIcon.width = 24.0f;
  ballIcon.height = 24.0f;
  ballIcon.layer = game::ui::kLayerMarker + 2;
  ballIcon.visible = true;

  // 自ボール外側パルスサークル (○)
  m_minimapPulseMarkerEntity = ctx.world.CreateEntity();
  auto &pulseMarker = ctx.world.Add<UIText>(m_minimapPulseMarkerEntity);
  pulseMarker.text = L"○";
  pulseMarker.x = marker.x;
  pulseMarker.y = marker.y;
  pulseMarker.style = graphics::TextStyle::Guide();
  pulseMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  pulseMarker.style.color = {0.18f, 0.85f, 1.0f, 0.8f}; // 半透明シアン
  pulseMarker.layer = game::ui::kLayerMarker;
  pulseMarker.visible = false;

  // ターゲットピン用フォールバックマーカー
  m_minimapFlagMarkerEntity = ctx.world.CreateEntity();
  auto &flagMarker = ctx.world.Add<UIText>(m_minimapFlagMarkerEntity);
  flagMarker.text = L"P";
  flagMarker.x = 0.0f;
  flagMarker.y = 0.0f;
  flagMarker.style = graphics::TextStyle::Guide();
  flagMarker.style.fontSize = game::ui::kMinimapMarkerSize;
  flagMarker.style.color = {1.0f, 0.2f, 0.2f, 1.0f}; // 鮮烈なレッド
  flagMarker.layer = game::ui::kLayerMarker + 2; // ボールより前面に描画
  flagMarker.visible = false;

  // ショット方向案内用のガイドドット配列 (·)
  m_minimapGuideDotEntities.clear();
  for (int i = 0; i < 3; ++i) {
    auto dotEntity = ctx.world.CreateEntity();
    auto &dot = ctx.world.Add<UIText>(dotEntity);
    dot.text = L"·";
    dot.x = 0.0f;
    dot.y = 0.0f;
    dot.style = graphics::TextStyle::Guide();
    dot.style.fontSize = 12.0f;
    dot.style.color = {0.18f, 0.85f, 1.0f, 0.65f}; // 薄めのシアン
    dot.layer = game::ui::kLayerMarker;
    dot.visible = false;
    m_minimapGuideDotEntities.push_back(dotEntity);
  }

  // 着弾点プレビュー: ばらつき範囲円(大きな薄い○) + 中心マーカー
  m_landingPreviewRangeEntity = ctx.world.CreateEntity();
  auto &landingRange = ctx.world.Add<UIText>(m_landingPreviewRangeEntity);
  landingRange.text = L"○";
  landingRange.style = graphics::TextStyle::Guide();
  landingRange.style.color = {1.0f, 0.75f, 0.15f, 0.55f}; // 半透明アンバー
  landingRange.layer = game::ui::kLayerMarker;
  landingRange.visible = false;

  m_landingPreviewCenterEntity = ctx.world.CreateEntity();
  auto &landingCenter = ctx.world.Add<UIText>(m_landingPreviewCenterEntity);
  landingCenter.text = L"⛳";
  landingCenter.style = graphics::TextStyle::Guide();
  landingCenter.style.fontSize = 22.0f;
  landingCenter.style.color = {1.0f, 1.0f, 1.0f, 1.0f};
  landingCenter.layer = game::ui::kLayerMarker + 1;
  landingCenter.visible = false;

  // ズームインジケーター背景の生成（マップビューでのズーム率表示用）
  m_mapZoomIndicatorBg = ctx.world.CreateEntity();
  auto &zoomBg = ctx.world.Add<UIText>(m_mapZoomIndicatorBg);
  zoomBg.x = game::ui::kMinimapX + 10.0f;
  zoomBg.y = game::ui::kMinimapY + 10.0f;
  zoomBg.width = 65.0f;
  zoomBg.height = 24.0f;
  zoomBg.style.bgColor = {0.05f, 0.1f, 0.15f, 0.8f};
  zoomBg.style.borderColor = game::ui::kColorBorder;
  zoomBg.style.borderWidth = 1.0f;
  zoomBg.style.cornerRadius = 4.0f;
  zoomBg.visible = false;
  zoomBg.layer = game::ui::kLayerMinimap + 2;

  // ズームインジケーターテキストの生成（現在のズーム倍率をテキスト表示）
  m_mapZoomIndicatorText = ctx.world.CreateEntity();
  auto &zoomTxt = ctx.world.Add<UIText>(m_mapZoomIndicatorText);
  zoomTxt.text = L"x1.00";
  zoomTxt.x = zoomBg.x + 5.0f;
  zoomTxt.y = zoomBg.y + 2.0f;
  zoomTxt.width = zoomBg.width - 10.0f;
  zoomTxt.height = zoomBg.height - 4.0f;
  zoomTxt.style = graphics::TextStyle::Guide();
  zoomTxt.style.fontSize = 14.0f;
  zoomTxt.style.align = graphics::TextAlign::Center;
  zoomTxt.visible = false;
  zoomTxt.layer = game::ui::kLayerMinimap + 3;

  // マウス位置のワールド座標表示テキストの生成
  m_mapCoordText = ctx.world.CreateEntity();
  auto &coordTxt = ctx.world.Add<UIText>(m_mapCoordText);
  coordTxt.text = L"";
  coordTxt.style = graphics::TextStyle::Guide();
  coordTxt.style.fontSize = 12.0f;
  coordTxt.style.align = graphics::TextAlign::Left;
  coordTxt.visible = false;
  coordTxt.layer = game::ui::kLayerMinimap + 2;

  // マウス位置とボールの距離表示テキストの生成
  m_mapDistanceText = ctx.world.CreateEntity();
  auto &distTxt = ctx.world.Add<UIText>(m_mapDistanceText);
  distTxt.text = L"";
  distTxt.style = graphics::TextStyle::Guide();
  distTxt.style.fontSize = 12.0f;
  distTxt.style.align = graphics::TextAlign::Left;
  distTxt.visible = false;
  distTxt.layer = game::ui::kLayerMinimap + 2;

  // 操作ヘルプパネル背景の生成
  m_mapHelpPanelBg = ctx.world.CreateEntity();
  auto &helpBg = ctx.world.Add<UIText>(m_mapHelpPanelBg);
  helpBg.x = (1280.0f - game::ui::kMapHelpPanelW) * 0.5f;
  helpBg.y = (720.0f - game::ui::kMapHelpPanelH) * 0.5f;
  helpBg.width = game::ui::kMapHelpPanelW;
  helpBg.height = game::ui::kMapHelpPanelH;
  helpBg.style.bgColor = game::ui::kColorBgDark;
  helpBg.style.borderColor = game::ui::kColorBorder;
  helpBg.style.borderWidth = 2.0f;
  helpBg.style.cornerRadius = 12.0f;
  helpBg.visible = false;
  helpBg.layer = game::ui::kLayerOverlay;

  // 操作ヘルプタイトルテキストの生成
  m_mapHelpTitle = ctx.world.CreateEntity();
  auto &helpTitle = ctx.world.Add<UIText>(m_mapHelpTitle);
  helpTitle.text = L"操作ガイド - マップビュー";
  helpTitle.x = helpBg.x;
  helpTitle.y = helpBg.y + 15.0f;
  helpTitle.width = helpBg.width;
  helpTitle.height = 30.0f;
  helpTitle.style = graphics::TextStyle::Guide();
  helpTitle.style.fontSize = 20.0f;
  helpTitle.style.color = game::ui::kColorAccent;
  helpTitle.style.align = graphics::TextAlign::Center;
  helpTitle.visible = false;
  helpTitle.layer = game::ui::kLayerOverlay + 1;

  // 操作ヘルプの各操作説明テキスト行を生成
  std::vector<std::wstring> helpTexts = {
    L"[ドラッグ / 左クリック] マップをパン",
    L"[スクロール / + / -] ズームイン / ズームアウト",
    L"[C / Space] ボール位置にフォーカス",
    L"[F] フィールド全体を表示",
    L"[0] ズーム率を等倍(100%)にリセット",
    L"[?] 操作ガイドの表示切り替え",
    L"[M / Esc] マップビューを閉じる"
  };

  float startY = helpBg.y + 55.0f;
  float lineSpacing = 22.0f;
  for (size_t i = 0; i < helpTexts.size(); ++i) {
    auto e = ctx.world.CreateEntity();
    auto &line = ctx.world.Add<UIText>(e);
    line.text = helpTexts[i];
    line.x = helpBg.x + 25.0f;
    line.y = startY + i * lineSpacing;
    line.width = helpBg.width - 50.0f;
    line.height = 20.0f;
    line.style = graphics::TextStyle::Guide();
    line.style.fontSize = 14.0f;
    line.style.align = graphics::TextAlign::Left;
    line.visible = false;
    line.layer = game::ui::kLayerOverlay + 1;
    m_mapHelpLines.push_back(e);
  }

  // マップビュー突入時の簡易操作ヒント下部バー背景の生成
  m_mapOpenHintBg = ctx.world.CreateEntity();
  auto &openBg = ctx.world.Add<UIText>(m_mapOpenHintBg);
  openBg.x = 240.0f;
  openBg.y = 665.0f;
  openBg.width = 800.0f;
  openBg.height = 36.0f;
  openBg.style.bgColor = {0.035f, 0.055f, 0.090f, 0.85f};
  openBg.style.borderColor = game::ui::kColorBorder;
  openBg.style.borderWidth = 1.0f;
  openBg.style.cornerRadius = 6.0f;
  openBg.visible = false;
  openBg.layer = game::ui::kLayerOverlay;

  // マップビュー突入時の簡易操作ヒントテキストの生成
  m_mapOpenHintText = ctx.world.CreateEntity();
  auto &openTxt = ctx.world.Add<UIText>(m_mapOpenHintText);
  openTxt.text = L"[ドラッグ] パン  [スクロール/+/-] ズーム  [C/Space] ボール中央  [F] 全体表示  [?] ヘルプ  [Esc/M] 閉じる";
  openTxt.x = openBg.x + 10.0f;
  openTxt.y = openBg.y + 6.0f;
  openTxt.width = openBg.width - 20.0f;
  openTxt.height = openBg.height - 12.0f;
  openTxt.style = graphics::TextStyle::Guide();
  openTxt.style.fontSize = 13.0f;
  openTxt.style.align = graphics::TextAlign::Center;
  openTxt.visible = false;
  openTxt.layer = game::ui::kLayerOverlay + 1;
}

/**
 * @brief ミニマップ上のすべてのホールアイコンを削除します。
 */
void MinimapController::ClearHoleIcons(core::GameContext &ctx) {
  for (auto &icon : m_mapHoleIcons) {
    ctx.world.DestroyEntity(icon.iconEntity);
  }
  m_mapHoleIcons.clear();
}

/**
 * @brief ミニマップ上にホールアイコンを追加します。
 */
void MinimapController::AddHoleIcon(core::GameContext &ctx, float x, float z,
                                    const std::string& linkTarget,
                                    bool isTargetHole, bool isPlayableHole,
                                    int hopsToTarget) {
  auto iconEntity = ctx.world.CreateEntity();
  auto &ui = ctx.world.Add<UIImage>(iconEntity);
  
  ui = UIImage::Create("golf_hole_icon_transparent.png", 0.0f, 0.0f);
  ui.width = isTargetHole ? 34.0f : (isPlayableHole ? 20.0f : 12.0f);
  ui.height = ui.width;
  ui.alpha = isTargetHole ? 1.0f : (isPlayableHole ? 0.72f : 0.34f);
  ui.layer = isTargetHole ? game::ui::kLayerMarker + 3
                          : (isPlayableHole ? game::ui::kLayerMarker + 1
                                            : game::ui::kLayerMarker);
  ui.visible = false;
  
  MapHoleIcon mapIcon{};
  mapIcon.iconEntity = iconEntity;
  mapIcon.worldPos = {x, z};
  mapIcon.linkTarget = linkTarget;
  mapIcon.isTarget = isTargetHole;
  mapIcon.isPlayable = isPlayableHole;
  mapIcon.hopsToTarget = hopsToTarget;
  m_mapHoleIcons.push_back(mapIcon);
}

/**
 * @brief 経路評価後のホールアイコン情報を更新します。山内陽
 */
void MinimapController::UpdateHoleIconEvaluation(
    const std::string& linkTarget, bool isPlayableHole, int hopsToTarget) {
  for (auto& icon : m_mapHoleIcons) {
    if (icon.linkTarget != linkTarget) {
      continue;
    }
    icon.isPlayable = isPlayableHole;
    icon.hopsToTarget = hopsToTarget;
  }
}

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

/**
 * @brief ミニマップおよびインジケーターの表示を更新します。
 */
void MinimapController::UpdateMinimap(core::GameContext &ctx, float fieldWidth, float fieldDepth, const DirectX::XMFLOAT3& shotDirection) {
  if (!m_minimapRenderer)
    return;

  // カメラ外でも現在地がわかるよう、マーカーをUI上にプロット
  UIImage *ui = ctx.world.Get<UIImage>(m_minimapEntity);
  UIText *marker = ctx.world.Get<UIText>(m_minimapMarkerEntity);
  UIImage *ballIcon = ctx.world.Get<UIImage>(m_minimapBallIconEntity);
  Transform *ballT = ctx.world.Get<Transform>(m_cfg.ballEntity);

  if (!m_isVisible) {
    if (ui) ui->visible = false;
    m_minimapRenderPending = false;
    m_minimapHasRenderedOnce = false; // 再表示時に必ず1回再描画させる
    m_lastRenderedMoveCount = -1;
    SetVisible(ctx, false);
    return;
  }
  if (ui) ui->visible = !m_isMapView;

  game::systems::MapRenderParams params = m_isMapView
      ? BuildMapViewParams(m_mapCenter, m_mapZoom, fieldWidth, fieldDepth)
      : BuildHudMinimapParams(ctx, m_cfg.ballEntity, fieldWidth, fieldDepth);

  // マップビュー中はメインカメラが俯瞰映像を描画するため、オフスクリーン描画は不要
  m_pendingMinimapParams = params;
  if (!m_isMapView) {
    // 変化駆動レンダリング：初回表示・ページ/フィールド変更・中心移動・
    // ズームや表示範囲の変化があった場合のみ再描画を要求する。
    // 静止中は毎フレームのGPU再描画を行わない。
    const float span = ComputeMinimapWorldSpan(params);
    const float moveThresholdPixels = 1.0f; // 256x256マップ上で意味のある移動量
    const float moveThreshold =
        std::max(0.05f, (span / 256.0f) * moveThresholdPixels);

    // ページ/地形の内容識別子。GolfGameState::moveCountはページ遷移のたびに
    // 単調増加するため、寸法が同一の新しいページへ遷移した場合でも変化を検出できる。
    int currentMoveCount = m_lastRenderedMoveCount;
    if (auto *state = ctx.world.GetGlobal<components::GolfGameState>()) {
      currentMoveCount = state->moveCount;
    }

    const bool paramsChanged =
        !m_minimapHasRenderedOnce ||
        fieldWidth != m_lastRenderedFieldWidth ||
        fieldDepth != m_lastRenderedFieldDepth ||
        currentMoveCount != m_lastRenderedMoveCount ||
        std::abs(params.zoom - m_lastRenderedZoom) > 0.0001f ||
        std::abs(span - m_lastRenderedSpan) > 0.01f ||
        std::abs(params.center.x - m_lastRenderedCenter.x) > moveThreshold ||
        std::abs(params.center.z - m_lastRenderedCenter.z) > moveThreshold;

    if (paramsChanged) {
      m_minimapRenderPending = true;
    }
    m_pendingFieldWidth = fieldWidth;
    m_pendingFieldDepth = fieldDepth;
    m_pendingMoveCount = currentMoveCount;
  } else {
    m_minimapRenderPending = false;
  }

  const float clipWidth = ComputeMinimapWorldSpan(params);
  const MarkerBounds mapBounds = m_isMapView
      ? GetMapViewMarkerBounds()
      : MarkerBounds{ui ? ui->x : game::ui::kMinimapX,
                     ui ? ui->y : game::ui::kMinimapY,
                     ui ? ui->width : game::ui::kMinimapWidth,
                     ui ? ui->height : game::ui::kMinimapHeight};
  const bool markerSurfaceVisible = m_isMapView || (ui && ui->visible);

  if (ui && marker && ballT) {
    float u = 0.5f;
    float v = 0.5f;
    const bool ballInView =
        ProjectToMinimap(ballT->position.x, ballT->position.z, params, u, v);
    if (!m_isMapView) {
      u = std::clamp(u, 0.02f, 0.98f);
      v = std::clamp(v, 0.02f, 0.98f);
    }

    // 内側実心●の位置設定
    marker->x = mapBounds.x + u * mapBounds.width - 10.0f;
    marker->y = mapBounds.y + v * mapBounds.height - 10.0f;
    marker->visible = false;

    if (ballIcon) {
      ballIcon->width = m_isMapView ? 34.0f : 24.0f;
      ballIcon->height = m_isMapView ? 34.0f : 24.0f;
      ballIcon->x = mapBounds.x + u * mapBounds.width - ballIcon->width * 0.5f;
      ballIcon->y = mapBounds.y + v * mapBounds.height - ballIcon->height * 0.5f;
      ballIcon->visible = markerSurfaceVisible && (!m_isMapView || ballInView);
    }

    // 自機の位置に「ここにいるよ」を伝える波紋(レーダーピング)。輪が広がり
    // ながら薄れて消える1サイクルを繰り返す、常時アイドルアニメーション。
    if (auto *pulseMarker = ctx.world.Get<UIText>(m_minimapPulseMarkerEntity)) {
      const bool pulseVisible = markerSurfaceVisible && ballIcon && ballIcon->visible;
      if (pulseVisible) {
        constexpr float kPulseCycle = 1.6f;
        const float phase = std::fmod(m_markerPulseTimer, kPulseCycle) / kPulseCycle; // 0..1
        const float ringSize = game::ui::kMinimapMarkerSize * (0.9f + phase * 1.4f);
        pulseMarker->x = mapBounds.x + u * mapBounds.width - ringSize * 0.5f;
        pulseMarker->y = mapBounds.y + v * mapBounds.height - ringSize * 0.5f;
        pulseMarker->style.fontSize = ringSize;
        pulseMarker->style.color = {0.18f, 0.85f, 1.0f, (1.0f - phase) * 0.5f};
      }
      pulseMarker->visible = pulseVisible;
    }
  }

  m_markerPulseTimer += ctx.dt;

  // 自ボールマーカーは画像アイコンで表示するため、旧テキストマーカーは非表示に固定する。
  if (markerSurfaceVisible) {
    if (marker) {
      marker->style.fontSize = game::ui::kMinimapMarkerSize * 0.85f; // シャープに小さく表示
      marker->style.color = {0.18f, 0.85f, 1.0f, 1.0f}; // ソリッドシアン
    }
  }

  // ホール（カップ）アイコンのミニマップ投影座標更新および🚩フラッグアニメーション
  for (auto &icon : m_mapHoleIcons) {
    if (auto *iconUI = ctx.world.Get<UIImage>(icon.iconEntity)) {
      if (markerSurfaceVisible) {
        float u = 0.0f;
        float v = 0.0f;
        if (ProjectToMinimap(icon.worldPos.x, icon.worldPos.y, params, u, v)) {
          const float normalSize = icon.isPlayable ? 20.0f : 12.0f;
          const float mapSize = icon.isPlayable ? 28.0f : 6.0f;
          iconUI->width = icon.isTarget ? (m_isMapView ? 44.0f : 34.0f)
                                        : (m_isMapView ? mapSize : normalSize);
          iconUI->height = iconUI->width;
          iconUI->x = mapBounds.x + u * mapBounds.width - iconUI->width * 0.5f;
          iconUI->y = mapBounds.y + v * mapBounds.height - iconUI->height * 0.5f;
          iconUI->alpha = icon.isTarget ? 1.0f
                                        : (icon.isPlayable ? 0.72f : 0.18f);
          iconUI->visible = markerSurfaceVisible && !m_isMapView && icon.isPlayable;

          if (icon.isTarget && m_minimapFlagMarkerEntity != UINT32_MAX) {
            if (auto *flagTxt = ctx.world.Get<UIText>(m_minimapFlagMarkerEntity)) {
              flagTxt->x = mapBounds.x + u * mapBounds.width - 8.0f;
              flagTxt->y = mapBounds.y + v * mapBounds.height - 12.0f;
              flagTxt->visible = !m_isMapView && markerSurfaceVisible;

              float flagPulse = 1.0f + 0.16f * std::sin(m_markerPulseTimer * 2.8f);
              flagTxt->style.fontSize = game::ui::kMinimapMarkerSize * flagPulse;
              flagTxt->style.color = {1.0f, 0.2f, 0.2f, 1.0f};
            }
          }
        } else {
          iconUI->visible = false;
          if (icon.isTarget && m_minimapFlagMarkerEntity != UINT32_MAX) {
            if (auto *flagTxt = ctx.world.Get<UIText>(m_minimapFlagMarkerEntity)) {
              flagTxt->visible = false;
            }
          }
        }
      } else {
        iconUI->visible = false;
      }
    }
  }

  // ショット方向案内用のガイドドットの投影座標更新
  if (!m_isMapView && ui && ballT) { // 通常のHUDミニマップ時のみガイドを描画
    XMVECTOR dirVec = XMVector3Normalize(XMLoadFloat3(&shotDirection));
    float step = clipWidth * 0.075f; // ミニマップのズームスケールに応じたドット間隔

    for (size_t i = 0; i < m_minimapGuideDotEntities.size(); ++i) {
      if (auto *dot = ctx.world.Get<UIText>(m_minimapGuideDotEntities[i])) {
        float offsetDist = step * (i + 1);
        XMVECTOR dotPosVec = XMVectorAdd(XMLoadFloat3(&ballT->position), XMVectorScale(dirVec, offsetDist));
        XMFLOAT3 dotPos;
        XMStoreFloat3(&dotPos, dotPosVec);

        float u = 0.0f;
        float v = 0.0f;
        if (ProjectToMinimap(dotPos.x, dotPos.z, params, u, v) &&
            u >= 0.02f && u <= 0.98f && v >= 0.02f && v <= 0.98f) {
          dot->x = mapBounds.x + u * mapBounds.width - 5.0f;
          dot->y = mapBounds.y + v * mapBounds.height - 5.0f;
          dot->visible = ui->visible;
        } else {
          dot->visible = false;
        }
      }
    }
  } else {
    for (auto dotEntity : m_minimapGuideDotEntities) {
      if (auto *dot = ctx.world.Get<UIText>(dotEntity)) dot->visible = false;
    }
  }

  // 着弾点プレビュー(トップビュー専用): 着弾中心マーカー + ばらつき範囲円
  {
    auto *rangeTxt  = ctx.world.Get<UIText>(m_landingPreviewRangeEntity);
    auto *centerTxt = ctx.world.Get<UIText>(m_landingPreviewCenterEntity);
    float u = 0.5f, v = 0.5f;
    const bool inView = m_isMapView && m_landingPreviewVisible &&
        ProjectToMinimap(m_landingPreviewCenter.x, m_landingPreviewCenter.z, params, u, v);

    if (inView) {
      if (centerTxt) {
        const float cs = centerTxt->style.fontSize;
        centerTxt->x = mapBounds.x + u * mapBounds.width - cs * 0.5f;
        centerTxt->y = mapBounds.y + v * mapBounds.height - cs * 0.5f;
        centerTxt->visible = true;
      }
      if (rangeTxt && clipWidth > 0.0f) {
        // ○グリフの見た目上の直径にほぼ相当するフォントサイズを、
        // ワールド半径をマップ画面スケールへ換算して求める。
        const float pixelRadius = (m_landingPreviewRadius / clipWidth) * mapBounds.width;
        const float ringSize = std::clamp(pixelRadius * 2.0f, 16.0f, 480.0f);
        rangeTxt->x = mapBounds.x + u * mapBounds.width - ringSize * 0.5f;
        rangeTxt->y = mapBounds.y + v * mapBounds.height - ringSize * 0.5f;
        rangeTxt->style.fontSize = ringSize;
        rangeTxt->visible = true;
      }
    } else {
      if (centerTxt) centerTxt->visible = false;
      if (rangeTxt) rangeTxt->visible = false;
    }
  }

  // 座標および距離表示
  if (m_isMapView && ballT) {
    int mouseX = ctx.input.GetMousePosition().x;
    int mouseY = ctx.input.GetMousePosition().y;

    // マウスがマップ内かチェック
    bool inMap = (mouseX >= mapBounds.x &&
                  mouseX <= mapBounds.x + mapBounds.width &&
                  mouseY >= mapBounds.y &&
                  mouseY <= mapBounds.y + mapBounds.height);

    auto *coordTxt = ctx.world.Get<UIText>(m_mapCoordText);
    auto *distTxt = ctx.world.Get<UIText>(m_mapDistanceText);

    if (inMap && coordTxt && distTxt) {
      float u = (mouseX - mapBounds.x) / mapBounds.width;
      float v = (mouseY - mapBounds.y) / mapBounds.height;

      // UV→ワールド座標
      float clipWidth = ComputeMinimapWorldSpan(params);

      float worldX = params.center.x + (u - 0.5f) * clipWidth;
      float worldZ = params.center.z - (v - 0.5f) * clipWidth;

      // 座標表示（ミニマップ内固定位置）
      coordTxt->x = mapBounds.x + 10.0f;
      coordTxt->y = mapBounds.y + mapBounds.height - 35.0f;
      coordTxt->text = std::format(L"座標: ({:.1f}, {:.1f})", worldX, worldZ);
      coordTxt->visible = true;

      // ボールからの距離
      float dx = worldX - ballT->position.x;
      float dz = worldZ - ballT->position.z;
      float distance = std::sqrt(dx * dx + dz * dz);

      distTxt->x = mapBounds.x + 10.0f;
      distTxt->y = mapBounds.y + mapBounds.height - 20.0f;
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

  // ズームインジケーターの更新
  auto *zoomBg = ctx.world.Get<UIText>(m_mapZoomIndicatorBg);
  auto *zoomTxt = ctx.world.Get<UIText>(m_mapZoomIndicatorText);

  if (m_isMapView && zoomBg && zoomTxt) {
    zoomTxt->text = std::format(L"x{:.2f}", m_mapZoom);

    zoomBg->visible = true;
    zoomTxt->visible = true;
  } else {
    if (zoomBg)
      zoomBg->visible = false;
    if (zoomTxt)
      zoomTxt->visible = false;
  }

  // 操作ヘルプパネルのフェード処理
  static float helpFadeAlpha = 0.0f;
  float targetHelpAlpha = m_mapHelpVisible ? 1.0f : 0.0f;
  float fadeSpeed = game::ui::kFadeSpeed; 
  helpFadeAlpha += (targetHelpAlpha - helpFadeAlpha) * fadeSpeed * ctx.dt;

  bool shouldShowHelp = helpFadeAlpha > 0.01f;

  auto *helpBg = ctx.world.Get<UIText>(m_mapHelpPanelBg);
  auto *helpTitle = ctx.world.Get<UIText>(m_mapHelpTitle);

  if (helpBg) {
    helpBg->visible = shouldShowHelp;
    helpBg->style.bgColor.w = helpFadeAlpha * game::ui::kMapHelpPanelAlpha;
    helpBg->style.borderColor.w = helpFadeAlpha * 0.8f;
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

  // 簡易操作ヒントの常時表示処理（マップ表示中は下部に固定表示する）
  auto *openBg = ctx.world.Get<UIText>(m_mapOpenHintBg);
  auto *openTxt = ctx.world.Get<UIText>(m_mapOpenHintText);
  if (m_isMapView) {
    if (openBg) {
      openBg->visible = true;
      openBg->style.bgColor.w = 0.85f;
      openBg->style.borderColor.w = 0.45f;
    }
    if (openTxt) {
      openTxt->visible = true;
      openTxt->style.color.w = 1.0f;
    }
  } else {
    if (openBg) openBg->visible = false;
    if (openTxt) openTxt->visible = false;
  }
}

/**
 * @brief 保留中のミニマップ描画要求があれば、オフスクリーンレンダーターゲットへ実際に描画します。
 */
void MinimapController::RenderPendingMinimap(core::GameContext &ctx) {
  if (m_isMapView || !m_isVisible) {
    m_minimapRenderPending = false;
    return;
  }
  if (!m_minimapRenderPending || !m_minimapRenderer) {
    return;
  }
  m_minimapRenderer->Render(ctx, m_pendingMinimapParams);
  m_minimapRenderPending = false;

  // 変化駆動レンダリング用に、実際に描画したときのパラメータを記録する
  m_minimapHasRenderedOnce = true;
  m_lastRenderedCenter = m_pendingMinimapParams.center;
  m_lastRenderedZoom = m_pendingMinimapParams.zoom;
  m_lastRenderedFieldWidth = m_pendingFieldWidth;
  m_lastRenderedFieldDepth = m_pendingFieldDepth;
  m_lastRenderedSpan = ComputeMinimapWorldSpan(m_pendingMinimapParams);
  m_lastRenderedMoveCount = m_pendingMoveCount;
}

/**
 * @brief マップの中心座標をボール位置に同期させます。
 */
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

/**
 * @brief ユーザー入力を処理しマップビュー操作に反映します。
 */
void MinimapController::ProcessInput(core::GameContext &ctx, int mouseX, int mouseY, float fieldWidth, float fieldDepth, ecs::Entity skyboxEntity) {
  if (ctx.input.GetKeyDown('M')) {
    ToggleMapView(ctx, skyboxEntity);
    LOG_INFO("MinimapController", "Map view: {}", m_isMapView ? "ON" : "OFF");
  }

  if (!m_isMapView) return;

  // 動的な最大ズーム倍率を算出（フィールドサイズを考慮）
  float extent = (std::max)(fieldWidth, fieldDepth);
  m_maxMapZoom = game::utils::CalculateMaxMapZoom(extent, kMinMapViewSpan, m_baseMaxMapZoom);

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

namespace game::controllers {

/**
 * @brief ミニマップUI全体の表示状態を切り替えます。
 * 
 * 入力: 表示フラグ（visible）
 * 変更: UIImageやUITextの表示フラグ
 * 出力: なし（副作用としてコンポーネントの表示状態が変化）
 */
void MinimapController::SetVisible(core::GameContext& ctx, bool visible) {
    if (!visible && m_isVisible) {
      // 非表示化：再表示時に必ず1回再描画させるため状態を無効化する
      m_minimapHasRenderedOnce = false;
      m_minimapRenderPending = false;
      m_lastRenderedMoveCount = -1;
    }
    m_isVisible = visible;
    auto setUIImg = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* img = ctx.world.Get<components::UIImage>(e)) img->visible = v;
    };
    auto setUITxt = [&](ecs::Entity e, bool v) {
        if (e == UINT32_MAX) return;
        if (auto* t = ctx.world.Get<components::UIText>(e)) t->visible = v;
    };

    // ミニマップ本体とマーカー類
    setUIImg(m_minimapEntity,            visible);
    setUITxt(m_minimapMarkerEntity,      false);
    setUIImg(m_minimapBallIconEntity,    visible);
    setUITxt(m_minimapPulseMarkerEntity, visible);
    setUITxt(m_minimapFlagMarkerEntity,  false);
    for (auto dotEntity : m_minimapGuideDotEntities) {
      setUITxt(dotEntity, false);
    }
    setUITxt(m_minimapHelpEntity,        visible);

    // ズームインジケーター
    setUITxt(m_mapZoomIndicatorBg,   false); // マップビュー時のみ表示のため常にfalse
    setUITxt(m_mapZoomIndicatorText, false);

    // 座標・距離テキスト（マップビュー時のみ）
    setUITxt(m_mapCoordText,    false);
    setUITxt(m_mapDistanceText, false);

    // ヘルプパネル
    setUITxt(m_mapHelpPanelBg, false);
    setUITxt(m_mapHelpTitle,   false);
    for (auto e : m_mapHelpLines) setUITxt(e, false);

    // 簡易操作ヒント
    setUITxt(m_mapOpenHintBg,   false);
    setUITxt(m_mapOpenHintText, false);

    // ホールアイコン
    for (auto& icon : m_mapHoleIcons) {
        setUIImg(icon.iconEntity, false);
    }
}

} // namespace game::controllers
