#pragma once
/**
 * @file MapViewState.h
 * @brief マップビュー時のスカイボックス可視制御ヘルパー
 */

#include "../components/Skybox.h"

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

} // namespace game::utils
