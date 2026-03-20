#pragma once
/**
 * @file CameraController.h
 * @brief TPSオービットカメラ・マップ俯瞰カメラの制御を担当するコントローラー
 *
 * 入力: GameContext（入力・カメラ/ボールTransform）、m_terrainSystem
 * 変更: カメラTransform/FOV更新、m_cameraYaw/Pitch/Distance 管理、衝突補正
 * 出力: 正規化済みショット方向ベクトル（m_shotDirection）
 */

#include "../../core/GameContext.h"
#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <memory>

namespace game::systems {
class WikiTerrainSystem;
class GameJuiceSystem;
} // namespace game::systems

namespace game::controllers {

/**
 * @brief TPSオービットカメラ・マップ俯瞰カメラを管理するコントローラー
 */
class CameraController {
public:
  struct Config {
    ecs::Entity cameraEntity;    ///< 制御対象カメラEntityID
    ecs::Entity ballEntity;      ///< ボールEntityID（追従用）
    ecs::Entity floorEntity;     ///< 床EntityID（衝突除外用）
    float fieldScale = 4.0f;     ///< kFieldScale
    game::systems::WikiTerrainSystem *terrain = nullptr;   ///< 地形高さ参照
    game::systems::GameJuiceSystem   *gameJuice = nullptr; ///< FOV演出用
  };

  /// @brief 初期化（エンティティIDと依存システムを受け取る）
  void Initialize(Config cfg);
  void SetBallEntity(ecs::Entity ballEntity) { m_cfg.ballEntity = ballEntity; }

  // ------------------------------------------------------------------
  // 毎フレーム呼び出し
  // ------------------------------------------------------------------

  /// @brief 非マップビュー時のカメラ更新（UpdateCamera 相当）
  void Update(core::GameContext &ctx);

  /// @brief マウス・キーボード入力からYaw/Pitch/距離を更新（非マップビュー専用）
  /// @param mouseX 現フレームのマウスX
  /// @param mouseY 現フレームのマウスY
  void ProcessInput(core::GameContext &ctx, int mouseX, int mouseY);

  // ------------------------------------------------------------------
  // イベント通知
  // ------------------------------------------------------------------

  /// @brief ExecuteShot 呼び出し直前に呼ぶ（追従フラグ初期化、開始位置記録）
  /// @param power ショット初速。追従開始閾値の計算に使用する
  void OnShotStart(core::GameContext &ctx, float power);

  /// @brief RestoringCamera フェーズでフェードアウト完了後に呼ぶ（カメラ即座再配置）
  void RestoreAfterFade(core::GameContext &ctx);

  /// @brief ページ遷移時（TransitionToPage / OnEnter）のカメラリセット
  void ResetForTransition(float fieldScale);

  /// @brief クラブ変更時にカメラ距離目標を更新
  /// @param recommendedDistance ClubController::GetRecommendedCameraDistance()
  /// @param recommendedHeight   ClubController::GetRecommendedCameraHeight()
  void SetTargetDistanceAndHeight(float recommendedDistance,
                                  float recommendedHeight);

  // ------------------------------------------------------------------
  // アクセサ（他コントローラ・WikiGolfScene から参照）
  // ------------------------------------------------------------------

  /// @brief 現在のショット方向（水平正規化済み）
  const DirectX::XMFLOAT3 &GetShotDirection() const { return m_shotDirection; }

  /// @brief ショット中かどうか（カメラ追尾中か）
  bool IsCameraChasing() const { return m_isCameraChasing; }

  /// @brief 現在のカメラYaw
  float GetYaw()      const { return m_cameraYaw; }
  /// @brief 現在のカメラPitch
  float GetPitch()    const { return m_cameraPitch; }
  /// @brief 現在のカメラ距離
  float GetDistance() const { return m_cameraDistance; }

private:
  // ------------------------------------------------------------------
  // 内部処理
  // ------------------------------------------------------------------

  /// @brief ショット中の追従ロジック（固定注視→追尾）
  void UpdateChaseCamera(core::GameContext &ctx,
                         const DirectX::XMFLOAT3 &ballPos,
                         DirectX::XMFLOAT3 &camPos,
                         DirectX::XMFLOAT4 &camRot);

  /// @brief TPSオービット基準位置の計算
  void CalcOrbitPosition(const DirectX::XMFLOAT3 &ballPos,
                         DirectX::XMVECTOR &outPos,
                         DirectX::XMVECTOR &outRot) const;

  /// @brief カメラと地形/壁の衝突補正
  /// @return 補正が行われた場合 true
  bool CheckCameraCollision(core::GameContext &ctx,
                            const DirectX::XMVECTOR &targetPos,
                            const DirectX::XMVECTOR &lookAtPos,
                            DirectX::XMVECTOR &outPos);

  // ------------------------------------------------------------------
  // 設定
  // ------------------------------------------------------------------
  Config m_cfg;

  // ------------------------------------------------------------------
  // カメラ状態
  // ------------------------------------------------------------------
  float m_cameraYaw      = 0.0f;
  float m_cameraPitch    = 0.5f;
  float m_cameraDistance = 60.0f;
  float m_targetCameraDistance = 60.0f;
  float m_targetCameraHeight   = 20.0f;

  /// @brief ショット開始時のカメラ位置（固定注視フェーズ用）
  DirectX::XMFLOAT3 m_shotStartCamPos = {0, 0, 0};
  /// @brief 追尾モードに入ったか
  bool m_isCameraChasing = false;
  /// @brief 追尾開始距離閾値（初速から ExecuteShot 時に設定）
  float m_cameraChaseThreshold = 60.0f;

  /// @brief カメラ前方から算出したショット方向（水平正規化済み）
  DirectX::XMFLOAT3 m_shotDirection = {0.0f, 0.0f, 1.0f};

  /// @brief ProcessInput 用前フレームのマウス座標
  int m_prevMouseX = 0;
  int m_prevMouseY = 0;
};

} // namespace game::controllers
