/**
 * @file MinimapControllerMap.cpp
 * @brief MinimapControllerMap の実装
*/

#include "MinimapController.h"
#include "MinimapControllerInternals.h"
#include "../components/Transform.h"
#include "../components/UIImage.h"
#include "../components/UIText.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Skybox.h"
#include "../components/WikiComponents.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/StringUtils.h"
#include "../../graphics/GraphicsDevice.h"
#include "../../ecs/World.h"
#include "../utils/ScreenRaycast.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/UIConstants.h"
#include "../scenes/HoleVisualRules.h"
#include <algorithm>
#include <cmath>
#include <format>

using namespace DirectX;
using namespace game::components;


namespace game::controllers {

using namespace DirectX;
using namespace game::components;

namespace {
// ○/📍/⛳等の単一グリフをGuide()スタイル(TextAlign::Center)で描画すると、
// TextVAlignが未実装で常に上詰めのため、行送り(ディセンダー余白)の分だけ
// 光学的な中心が矩形の幾何中心よりわずかに下にずれて見える。x,yを中心に
// 見せたいマーカー類はここで経験的に少し上へ補正する。
constexpr float kGlyphOpticalCenterCorrection = 0.12f; // グリフサイズに対する比率
} // namespace

bool MinimapController::TryGetHudMapWorldPosition(
    core::GameContext &ctx, int mouseX, int mouseY, float fieldWidth,
    float fieldDepth, DirectX::XMFLOAT3 &outWorldPosition) const {
  if (m_isMapView || !m_isVisible || !m_cfg.terrain) {
    return false;
  }
  const auto *mapImage = ctx.world.Get<UIImage>(m_minimapEntity);
  if (!mapImage || !mapImage->visible) {
    return false;
  }

  const MapHoleIcon *hoveredHole = nullptr;
  for (const auto &icon : m_mapHoleIcons) {
    const auto *iconImage = ctx.world.Get<UIImage>(icon.iconEntity);
    if (!iconImage || !iconImage->visible) {
      continue;
    }
    const minimap_detail::MarkerBounds iconBounds{
        iconImage->x, iconImage->y, iconImage->width, iconImage->height};
    if (minimap_detail::ContainsScreenPoint(
            iconBounds, static_cast<float>(mouseX),
            static_cast<float>(mouseY), 3.0f)) {
      hoveredHole = &icon;
    }
  }
  if (hoveredHole) {
    const float worldX = hoveredHole->worldPos.x;
    const float worldZ = hoveredHole->worldPos.y;
    outWorldPosition = {
        worldX,
        game::physics::ToVisualSurfaceHeight(
            m_cfg.terrain->GetHeight(worldX, worldZ)),
        worldZ};
    return true;
  }

  minimap_detail::MarkerBounds bounds{mapImage->x, mapImage->y,
                                       mapImage->width, mapImage->height};
  const auto params =
      minimap_detail::BuildHudMinimapParams(fieldWidth, fieldDepth);
  float worldX = 0.0f;
  float worldZ = 0.0f;
  if (!minimap_detail::UnprojectHudMinimap(
          static_cast<float>(mouseX), static_cast<float>(mouseY), bounds,
          params, worldX, worldZ)) {
    return false;
  }
  constexpr float edgeInset = 0.01f;
  if (std::abs(worldX) > fieldWidth * 0.5f - edgeInset ||
      std::abs(worldZ) > fieldDepth * 0.5f - edgeInset) {
    return false;
  }
  outWorldPosition = {
      worldX,
      game::physics::ToVisualSurfaceHeight(m_cfg.terrain->GetHeight(worldX,
                                                                    worldZ)),
      worldZ};
  return true;
}

/**
 * @brief ミニマップおよびインジケーターの表示を更新します。
*/
void MinimapController::UpdateMinimap(
    core::GameContext &ctx, float fieldWidth, float fieldDepth,
    const std::vector<ecs::Entity> &trajectoryEntities) {
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

  game::systems::MapRenderParams params;
  if (m_isMapView) {
    params = minimap_detail::BuildMapViewParams(
        m_mapCenter, m_mapZoom, fieldWidth, fieldDepth);
  } else {
    params = minimap_detail::BuildHudMinimapParams(fieldWidth, fieldDepth);
  }

  // マップビュー中はメインカメラが俯瞰映像を描画するため、オフスクリーン描画は不要
  m_pendingMinimapParams = params;
  if (!m_isMapView) {
    // 変化駆動レンダリング：初回表示・ページ/フィールド変更・中心移動・
    // ズームや表示範囲の変化があった場合のみ再描画を要求する。
    // 静止中は毎フレームのGPU再描画を行わない。
    const float span = minimap_detail::ComputeMinimapWorldSpan(params);
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

  minimap_detail::MarkerBounds mapBounds;
  if (m_isMapView) {
    mapBounds = minimap_detail::GetMapViewMarkerBounds();
  } else {
    mapBounds.x = game::ui::kMinimapX;
    mapBounds.y = game::ui::kMinimapY;
    mapBounds.width = game::ui::kMinimapWidth;
    mapBounds.height = game::ui::kMinimapHeight;
    if (ui) {
      mapBounds.x = ui->x;
      mapBounds.y = ui->y;
      mapBounds.width = ui->width;
      mapBounds.height = ui->height;
    }
  }
  const bool markerSurfaceVisible = m_isMapView || (ui && ui->visible);

  if (ui && marker && ballT) {
    // 全体マップビューは実カメラ(傾いた透視投影)で描画されるため、
    // HUD常時ミニマップ(正射影)とは別の投影式を使う（ズレ防止）。
    float screenX = 0.0f;
    float screenY = 0.0f;
    bool ballInView;
    if (m_isMapView) {
      ballInView = minimap_detail::ProjectWorldToMapViewScreen(
          ctx, m_cfg.cameraEntity, ballT->position, screenX, screenY);
    } else {
      float u = 0.5f;
      float v = 0.5f;
      ballInView = minimap_detail::ProjectToMinimap(ballT->position.x, ballT->position.z, params, u, v);
      u = std::clamp(u, 0.02f, 0.98f);
      v = std::clamp(v, 0.02f, 0.98f);
      screenX = mapBounds.x + u * mapBounds.width;
      screenY = mapBounds.y + v * mapBounds.height;
    }

    // 内側実心●の位置設定
    marker->x = screenX - 10.0f;
    marker->y = screenY - 10.0f;
    marker->visible = false;

    if (ballIcon) {
      float ballIconSize = 24.0f;
      if (m_isMapView) {
        ballIconSize = 34.0f;
      }
      ballIcon->width = ballIconSize;
      ballIcon->height = ballIconSize;
      ballIcon->x = screenX - ballIcon->width * 0.5f;
      ballIcon->y = screenY - ballIcon->height * 0.5f;
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
        pulseMarker->x = screenX - ringSize * 0.5f;
        pulseMarker->y = screenY - ringSize * 0.5f - ringSize * kGlyphOpticalCenterCorrection;
        // width/heightをfontSizeに追従させないと、Center揃え時にx,yを中心に
        // 描画されず画面右端方向へズレる（MinimapControllerUI.cppの注記参照）。
        pulseMarker->width = ringSize;
        pulseMarker->height = ringSize;
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

  std::string hoveredLink;
  float hoveredIconX = 0.0f;
  float hoveredIconY = 0.0f;
  const auto mousePosition = ctx.input.GetMousePosition();
  UpdateFlagFilterToggles(ctx);
  const auto visibleFlagKinds = minimap_detail::ResolveFlagVisibility(
      m_flagFilterEnabled, m_flagFilterAvailable);

  // 全ホールの投影と、右下マップ上でのリンク名ホバー判定。
  for (auto &icon : m_mapHoleIcons) {
    if (auto *iconUI = ctx.world.Get<UIImage>(icon.iconEntity)) {
      const size_t filterIndex = static_cast<size_t>(
          minimap_detail::ClassifyFlag(icon.isTarget, icon.hopsToTarget));
      const bool filterEnabled =
          filterIndex < visibleFlagKinds.size() &&
          visibleFlagKinds[filterIndex];
      iconUI->grayscaleTint = true;
      iconUI->tintColor = game::scenes::HoleVisualRules::GetColor(
          icon.isTarget, icon.hopsToTarget);
      if (markerSurfaceVisible) {
        float u = 0.0f;
        float v = 0.0f;
        if (minimap_detail::ProjectToMinimap(icon.worldPos.x, icon.worldPos.y, params, u, v)) {
          float normalSize = 14.0f;
          if (icon.isPlayable) {
            normalSize = 20.0f;
          }
          float mapSize = 6.0f;
          if (icon.isPlayable) {
            mapSize = 28.0f;
          }
          if (icon.isTarget) {
            iconUI->width = 34.0f;
            if (m_isMapView) {
              iconUI->width = 44.0f;
            }
          } else {
            iconUI->width = normalSize;
            if (m_isMapView) {
              iconUI->width = mapSize;
            }
          }
          iconUI->height = iconUI->width;
          iconUI->x = mapBounds.x + u * mapBounds.width - iconUI->width * 0.5f;
          iconUI->y = mapBounds.y + v * mapBounds.height - iconUI->height * 0.5f;
          if (icon.isTarget) {
            iconUI->alpha = 1.0f;
          } else if (icon.isPlayable) {
            iconUI->alpha = 0.72f;
          } else {
            iconUI->alpha = 0.18f;
          }
          iconUI->visible =
              markerSurfaceVisible && !m_isMapView && filterEnabled;
          const minimap_detail::MarkerBounds iconBounds{
              iconUI->x, iconUI->y, iconUI->width, iconUI->height};
          if (iconUI->visible && minimap_detail::ContainsScreenPoint(
                                     iconBounds,
                                     static_cast<float>(mousePosition.x),
                                     static_cast<float>(mousePosition.y),
                                     3.0f)) {
            hoveredLink = icon.linkTarget;
            hoveredIconX = iconUI->x + iconUI->width * 0.5f;
            hoveredIconY = iconUI->y;
          }
        } else {
          iconUI->visible = false;
        }
      } else {
        iconUI->visible = false;
      }
    }
  }

  if (auto *hoverLabel = ctx.world.Get<UIText>(m_holeHoverLabelEntity)) {
    hoverLabel->visible = !hoveredLink.empty() && !m_isMapView && m_isVisible;
    if (hoverLabel->visible) {
      hoverLabel->text = core::ToWString(hoveredLink);
      hoverLabel->width = std::clamp(
          36.0f + static_cast<float>(hoverLabel->text.size()) * 13.0f,
          110.0f, 286.0f);
      hoverLabel->x = std::clamp(
          hoveredIconX - hoverLabel->width * 0.5f, mapBounds.x + 4.0f,
          mapBounds.x + mapBounds.width - hoverLabel->width - 4.0f);
      hoverLabel->y = std::max(mapBounds.y + 4.0f,
                               hoveredIconY - hoverLabel->height - 6.0f);
    }
  }

  // TrajectoryPredictorの実軌道点を、地形・ホールより前面へ投影する。
  if (!m_isMapView && ui) {
    for (size_t i = 0; i < m_minimapGuideDotEntities.size(); ++i) {
      auto *dot = ctx.world.Get<UIText>(m_minimapGuideDotEntities[i]);
      if (!dot) continue;
      dot->visible = false;
      if (i >= trajectoryEntities.size()) continue;
      const auto *trajectoryTransform =
          ctx.world.Get<Transform>(trajectoryEntities[i]);
      const auto *trajectoryRenderer =
          ctx.world.Get<MeshRenderer>(trajectoryEntities[i]);
      if (!trajectoryTransform || !trajectoryRenderer ||
          !trajectoryRenderer->isVisible) {
        continue;
      }
      float u = 0.0f;
      float v = 0.0f;
      if (!minimap_detail::ProjectToMinimap(
              trajectoryTransform->position.x, trajectoryTransform->position.z,
              params, u, v)) {
        continue;
      }
      const float size = std::max(4.0f, 8.0f - static_cast<float>(i) * 0.12f);
      dot->width = size;
      dot->height = size;
      dot->style.fontSize = size;
      dot->style.color.w =
          std::max(0.28f, 0.95f - static_cast<float>(i) * 0.022f);
      dot->x = mapBounds.x + u * mapBounds.width - size * 0.5f;
      dot->y = mapBounds.y + v * mapBounds.height - size * 0.5f;
      dot->visible = ui->visible;
    }
  } else {
    for (auto dotEntity : m_minimapGuideDotEntities) {
      if (auto *dot = ctx.world.Get<UIText>(dotEntity)) dot->visible = false;
    }
  }

  // 着弾点プレビュー(トップビュー専用): 着弾中心マーカー + ばらつき範囲円
  // 全体マップビューは実カメラの透視投影のため、ProjectWorldToMapViewScreenで
  // 投影する（傾いたカメラの遠近感を無視するとズレるため）。
  {
    auto *rangeTxt  = ctx.world.Get<UIText>(m_landingPreviewRangeEntity);
    auto *centerTxt = ctx.world.Get<UIText>(m_landingPreviewCenterEntity);
    float screenX = 0.0f, screenY = 0.0f;
    const bool inView = m_isMapView && m_landingPreviewVisible &&
        minimap_detail::ProjectWorldToMapViewScreen(
            ctx, m_cfg.cameraEntity, m_landingPreviewCenter, screenX, screenY);

    if (inView) {
      if (centerTxt) {
        const float cs = centerTxt->style.fontSize;
        centerTxt->x = screenX - cs * 0.5f;
        centerTxt->y = screenY - cs * 0.5f - cs * kGlyphOpticalCenterCorrection;
        centerTxt->visible = true;
      }
      if (rangeTxt) {
        // ○グリフの見た目上の直径を、ばらつき半径ぶんワールドでオフセットした
        // 点を同じ透視投影で再投影し、実画面上のピクセル半径として求める
        // （正射影の一定倍率換算では傾いたカメラ下で不正確になるため）。
        float edgeX = screenX;
        float edgeY = screenY;
        DirectX::XMFLOAT3 edgeWorld = m_landingPreviewCenter;
        edgeWorld.x += m_landingPreviewRadius;
        float pixelRadius = 20.0f;
        if (minimap_detail::ProjectWorldToMapViewScreen(
                ctx, m_cfg.cameraEntity, edgeWorld, edgeX, edgeY)) {
          const float rdx = edgeX - screenX;
          const float rdy = edgeY - screenY;
          pixelRadius = std::sqrt(rdx * rdx + rdy * rdy);
        }
        const float ringSize = std::clamp(pixelRadius * 2.0f, 16.0f, 480.0f);
        rangeTxt->x = screenX - ringSize * 0.5f;
        rangeTxt->y = screenY - ringSize * 0.5f - ringSize * kGlyphOpticalCenterCorrection;
        rangeTxt->width = ringSize;
        rangeTxt->height = ringSize;
        rangeTxt->style.fontSize = ringSize;
        rangeTxt->visible = true;
      }
    } else {
      if (centerTxt) centerTxt->visible = false;
      if (rangeTxt) rangeTxt->visible = false;
    }
  }

  // エイムピン(中クリックで設置した狙い所): HUDミニマップ・全体マップの両方に表示する。
  // 全体マップビューは実カメラの透視投影のため、HUDミニマップ(正射影)とは
  // 投影式を分ける（ボールマーカーと同じ理由）。
  {
    auto *pinTxt = ctx.world.Get<UIText>(m_aimPinMarkerEntity);
    const auto *aimPin = ctx.world.GetGlobal<AimPinState>();
    bool pinInView = false;
    float screenX = 0.0f;
    float screenY = 0.0f;
    if (aimPin && aimPin->active) {
      if (m_isMapView) {
        pinInView = minimap_detail::ProjectWorldToMapViewScreen(
            ctx, m_cfg.cameraEntity, aimPin->worldPosition, screenX, screenY);
      } else {
        float u = 0.5f, v = 0.5f;
        pinInView = minimap_detail::ProjectToMinimap(
            aimPin->worldPosition.x, aimPin->worldPosition.z, params, u, v);
        screenX = mapBounds.x + u * mapBounds.width;
        screenY = mapBounds.y + v * mapBounds.height;
      }
    }
    if (pinTxt) {
      if (pinInView) {
        const float ps = pinTxt->style.fontSize;
        pinTxt->x = screenX - ps * 0.5f;
        pinTxt->y = screenY - ps * 0.5f - ps * kGlyphOpticalCenterCorrection;
        pinTxt->visible = true;
      } else {
        pinTxt->visible = false;
      }
    }
  }

  // 座標および距離表示
  // 全体マップビューは実カメラの透視投影のため、UVの線形逆変換ではなく
  // エイムピンと同じ地形レイキャストでマウス位置のワールド座標を求める
  // （ボールマーカー等と同じ理由でズレを避けるため）。
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

    DirectX::XMFLOAT3 hoverWorld{0.0f, 0.0f, 0.0f};
    // 探索距離はレイの進行距離（水平距離ではない）。マップを引いて見ている
    // ときほどカメラが高くなり、画面奥（浅い角度）では地表に届くまでの進行
    // 距離が長くなるため、フィールド全体を覆える余裕を持たせる。
    const bool hasHit = inMap && game::utils::RaycastScreenToTerrain(
        ctx, m_cfg.cameraEntity, static_cast<float>(mouseX),
        static_cast<float>(mouseY), m_cfg.terrain, 5000.0f, hoverWorld);

    if (hasHit && coordTxt && distTxt) {
      // 座標表示（ミニマップ内固定位置）
      coordTxt->x = mapBounds.x + 10.0f;
      coordTxt->y = mapBounds.y + mapBounds.height - 35.0f;
      coordTxt->text = std::format(L"座標: ({:.1f}, {:.1f})", hoverWorld.x, hoverWorld.z);
      coordTxt->visible = true;

      // ボールからの距離
      float dx = hoverWorld.x - ballT->position.x;
      float dz = hoverWorld.z - ballT->position.z;
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
  float targetHelpAlpha = 0.0f;
  if (m_mapHelpVisible) {
    targetHelpAlpha = 1.0f;
  }
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
  m_lastRenderedSpan = minimap_detail::ComputeMinimapWorldSpan(m_pendingMinimapParams);
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

} // namespace game::controllers
