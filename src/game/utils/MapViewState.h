#pragma once
/**
 * @file MapViewState.h
 * @brief マップビュー時のスカイボックス可視制御ヘルパー
 */

#include "../components/Skybox.h"
#include <DirectXMath.h>
#include <algorithm>

namespace game::utils {

/**
 * @brief マップビュー時にスカイボックスの描画を抑制・復元するための状態
 *
 * 俯瞰モードに入った瞬間だけスカイボックスを隠し、通常ビューへ戻った際に
 * 元の可視状態へ戻す。
 */
struct MapViewSkyboxState {
  bool previousMapViewState = false; ///< 直前のマップビュー状態
  bool cachedVisibility = true;      ///< マップビュー突入前の可視状態を保持

  /**
   * @brief マップビュー状態に応じてスカイボックスの可視状態を同期
   * @param isMapView 現在のマップビュー状態
   * @param skybox 対象スカイボックス
   */
  void Sync(bool isMapView, game::components::Skybox &skybox) {
    if (isMapView == previousMapViewState) {
      return; // 状態が変わっていない場合は何もしない
    }

    if (isMapView) {
      cachedVisibility = skybox.isVisible;
      skybox.isVisible = false;
    } else {
      skybox.isVisible = cachedVisibility;
    }

    previousMapViewState = isMapView;
  }

  /**
   * @brief 状態を初期化
   * @param initialVisibility 現在のスカイボックス可視状態
   */
  void Reset(bool initialVisibility) {
    cachedVisibility = initialVisibility;
    previousMapViewState = false;
  }
};

/**
 * @brief マップ中心座標をフィールド範囲内に収める
 * @param center 目標中心 (X:左右, Y:前後=Z軸相当)
 * @param fieldWidth フィールド全幅
 * @param fieldDepth フィールド全奥行
 * @param padding 端からの余白（ホール等が見切れないようにするためのマージン）
 * @return 収められた中心座標
 */
inline DirectX::XMFLOAT2 ClampMapCenter(const DirectX::XMFLOAT2 &center,
                                        float fieldWidth, float fieldDepth,
                                        float padding = 0.0f) {
  float halfW = (std::max)(0.0f, fieldWidth * 0.5f - padding);
  float halfD = (std::max)(0.0f, fieldDepth * 0.5f - padding);
  DirectX::XMFLOAT2 clamped = center;
  clamped.x = std::clamp(clamped.x, -halfW, halfW);
  clamped.y = std::clamp(clamped.y, -halfD, halfD);
  return clamped;
}

/**
 * @brief ズーム値を範囲内に収める
 */
inline float ClampMapZoom(float zoom, float minZoom, float maxZoom) {
  return std::clamp(zoom, minZoom, maxZoom);
}

} // namespace game::utils
