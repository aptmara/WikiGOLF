#pragma once
/**
 * @file MinimapController.h
 * @brief ミニマップの描画更新・全体マップビューのカメラ/UI制御を担当するコントローラー
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include "../systems/MapSys.h"
#include "../utils/MapViewState.h"
#include <DirectXMath.h>
#include <memory>
#include <vector>
#include <string>

namespace game::controllers {

/**
 * @brief ミニマップおよび全体俯瞰マップの制御コントローラー
 */
class MinimapController {
public:
  struct Config {
    ecs::Entity cameraEntity;
    ecs::Entity ballEntity;
    float fieldScale = 4.0f;
  };

  void Initialize(Config cfg, core::GameContext &ctx);
  void SetBallEntity(ecs::Entity ballEntity) { m_cfg.ballEntity = ballEntity; }

  /// @brief ミニマップ・マップUIなどのEntityを初期化・登録
  void InitializeUI(core::GameContext &ctx);

  // ------------------------------------------------------------------
  // 毎フレーム呼び出し
  // ------------------------------------------------------------------

  /// @brief 右上ミニマップの更新
  void UpdateMinimap(core::GameContext &ctx, float fieldWidth, float fieldDepth, const DirectX::XMFLOAT3& shotDirection);

  /// @brief 俯瞰マップカメラの更新（マップビュー有効時）
  void UpdateMapCamera(core::GameContext &ctx, float fieldWidth, float fieldDepth);

  /// @brief 入力処理（Mキーでトグル、マップビュー時のパン/ズームなど）
  void ProcessInput(core::GameContext &ctx, int mouseX, int mouseY, float fieldWidth, float fieldDepth, ecs::Entity skyboxEntity);

  // ------------------------------------------------------------------
  // イベント・状態操作
  // ------------------------------------------------------------------

  /// @brief マップ中心をボール位置に同期
  void SyncMapCenterToBall(core::GameContext &ctx, float dt, float fieldWidth, float fieldDepth, bool forceSnap = false);

  /// @brief ページ遷移時などにマップ上のアイコン（リンク穴など）をクリア
  void ClearHoleIcons(core::GameContext &ctx);

  /// @brief マップ上のリンク穴アイコンを追加
  void AddHoleIcon(core::GameContext &ctx, float x, float z,
                   const std::string& linkTarget, bool isTargetHole,
                   bool isPlayableHole = true, int hopsToTarget = -1);

  /**
   * @brief 経路評価後のホールアイコン情報を更新します。山内陽
   * @details 物理ホール生成後に距離計算が完了するため、マップ表示も後追いで同期します。
   */
  void UpdateHoleIconEvaluation(const std::string& linkTarget,
                                bool isPlayableHole,
                                int hopsToTarget);

  /// @brief マップビュー状態のトグル
  void ToggleMapView(core::GameContext &ctx, ecs::Entity skyboxEntity);

  /// @brief 現在マップビュー状態かどうか
  bool IsMapView() const { return m_isMapView; }
  void* GetMapSRV() const { return m_minimapRenderer ? m_minimapRenderer->GetSRV() : nullptr; }

  /// @brief ミニマップUI全体の表示/非表示切り替え（ロード中は非表示にするため）
  void SetVisible(core::GameContext& ctx, bool visible);

  // ------------------------------------------------------------------
  // Getter / Setter
  // ------------------------------------------------------------------
  
  float GetMapZoom() const { return m_mapZoom; }
  void SetMapZoom(float zoom) { m_mapZoom = zoom; m_targetMapZoom = zoom; }

private:
  Config m_cfg;

  // 俯瞰マップビュー
  float m_mapZoom = 1.0f;
  DirectX::XMFLOAT2 m_mapCenter = {0.0f, 0.0f};
  float m_minMapZoom = 0.005f;
  const float m_baseMaxMapZoom = 15.0f;
  float m_maxMapZoom = m_baseMaxMapZoom;
  float m_mapFollowLerp = 8.0f;
  bool m_isMapView = false;
  game::utils::MapViewSkyboxState m_mapViewSkyboxState;

  // マップビューUX強化
  float m_targetMapZoom = 1.0f;
  DirectX::XMFLOAT2 m_mapPanVelocity = {0.0f, 0.0f};
  float m_lastMapClickTime = 0.0f;
  int m_lastMapClickX = 0;
  int m_lastMapClickY = 0;
  float m_mapBoundaryHitTime = 0.0f;
  float m_markerPulseTimer = 0.0f;
  bool m_mapHelpVisible = false;
  bool m_isVisible = true; ///< HUDの表示状態です。ロード中の更新再表示を防ぎます。山内陽

  int m_prevMouseX = 0;
  int m_prevMouseY = 0;

  // ミニマップ（右上常時表示）
  std::unique_ptr<game::systems::MapSys> m_minimapRenderer;

  ecs::Entity m_minimapEntity = UINT32_MAX;
  ecs::Entity m_minimapMarkerEntity = UINT32_MAX;      ///< 自ボール用内側ドットマーカー（●）
  ecs::Entity m_minimapBallIconEntity = UINT32_MAX;    ///< 自ボール用画像アイコンです。山内陽
  ecs::Entity m_minimapPulseMarkerEntity = UINT32_MAX; ///< 自ボール用外側パルスサークル（○）
  ecs::Entity m_minimapFlagMarkerEntity = UINT32_MAX;  ///< ターゲットピン用パルスマーカー（🚩）
  std::vector<ecs::Entity> m_minimapGuideDotEntities;  ///< ショット方向案内用のドット配列（·）
  ecs::Entity m_minimapHelpEntity = UINT32_MAX;

  // マップビュー追加UI
  ecs::Entity m_mapZoomIndicatorBg = UINT32_MAX;
  ecs::Entity m_mapZoomIndicatorText = UINT32_MAX;
  ecs::Entity m_mapCoordText = UINT32_MAX;
  ecs::Entity m_mapDistanceText = UINT32_MAX;
  ecs::Entity m_mapHelpPanelBg = UINT32_MAX;
  ecs::Entity m_mapHelpTitle = UINT32_MAX;
  std::vector<ecs::Entity> m_mapHelpLines;

  float m_mapOpenHintTimer = 0.0f; ///< マップ開始時の操作ヒント表示タイマー
  ecs::Entity m_mapOpenHintBg = UINT32_MAX; ///< 操作ヒント背景UI
  ecs::Entity m_mapOpenHintText = UINT32_MAX; ///< 操作ヒントテキストUI


  struct MapHoleIcon {
    ecs::Entity iconEntity;
    DirectX::XMFLOAT2 worldPos;
    std::string linkTarget;
    bool isTarget;
    bool isPlayable;
    int hopsToTarget;
  };
  std::vector<MapHoleIcon> m_mapHoleIcons;
};

} // namespace game::controllers
