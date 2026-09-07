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

  game::systems::MapRenderParams params;
  if (m_isMapView) {
    params = minimap_detail::BuildMapViewParams(
        m_mapCenter, m_mapZoom, fieldWidth, fieldDepth);
  } else {
    params = minimap_detail::BuildHudMinimapParams(
        ctx, m_cfg.ballEntity, fieldWidth, fieldDepth);
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

  const float clipWidth = minimap_detail::ComputeMinimapWorldSpan(params);
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
    float u = 0.5f;
    float v = 0.5f;
    const bool ballInView =
        minimap_detail::ProjectToMinimap(ballT->position.x, ballT->position.z, params, u, v);
    if (!m_isMapView) {
      u = std::clamp(u, 0.02f, 0.98f);
      v = std::clamp(v, 0.02f, 0.98f);
    }

    // 内側実心●の位置設定
    marker->x = mapBounds.x + u * mapBounds.width - 10.0f;
    marker->y = mapBounds.y + v * mapBounds.height - 10.0f;
    marker->visible = false;

    if (ballIcon) {
      float ballIconSize = 24.0f;
      if (m_isMapView) {
        ballIconSize = 34.0f;
      }
      ballIcon->width = ballIconSize;
      ballIcon->height = ballIconSize;
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

  // ホール（カップ）アイコンのミニマップ投影座標更新およびフラッグアニメーション
  for (auto &icon : m_mapHoleIcons) {
    if (auto *iconUI = ctx.world.Get<UIImage>(icon.iconEntity)) {
      if (markerSurfaceVisible) {
        float u = 0.0f;
        float v = 0.0f;
        if (minimap_detail::ProjectToMinimap(icon.worldPos.x, icon.worldPos.y, params, u, v)) {
          float normalSize = 12.0f;
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
        if (minimap_detail::ProjectToMinimap(dotPos.x, dotPos.z, params, u, v) &&
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
        minimap_detail::ProjectToMinimap(m_landingPreviewCenter.x, m_landingPreviewCenter.z, params, u, v);

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
      float clipWidth = minimap_detail::ComputeMinimapWorldSpan(params);

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
