#pragma once
/**
 * @file LoadingScene.h
 * @brief ローディング画面シーン（ゴルフボール物理演出）
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../graphics/TextStyle.h"
#include "../../resources/ResourceManager.h"
#include "../components/WikiComponents.h"
#include <DirectXMath.h>
#include <functional>
#include <future>
#include <memory>
#include <vector>

namespace core {
class Scene;
}

namespace game::scenes {

/// @brief ローディングシーン
/// @details ゴルフボールが上から降ってきて溜まる演出を表示し、
///          完了後にフェードアウトして次のシーンへ遷移する
class LoadingScene : public core::Scene {
public:
  /// @brief コンストラクタ
  /// @param nextSceneFactory 次のシーンを生成するファクトリ関数
  explicit LoadingScene(
      std::function<std::unique_ptr<core::Scene>()> nextSceneFactory);

  const char *GetName() const override { return "LoadingScene"; }

  void OnEnter(core::GameContext &ctx) override;
  void OnUpdate(core::GameContext &ctx) override;
  void OnExit(core::GameContext &ctx) override;

private:
  /// @brief ゴルフボールの状態
  struct BallState {
    ecs::Entity entity;
    DirectX::XMFLOAT3 velocity;
    DirectX::XMFLOAT3 angularVelocity;
    bool settled; ///< 静止したかどうか
  };

  /// @brief ゴルフボールをスポーンする
  void SpawnBall(core::GameContext &ctx);

  /// @brief 物理シミュレーション更新
  void UpdatePhysics(core::GameContext &ctx, float dt);

  /// @brief すべてのボールが静止したか判定
  bool AreAllBallsSettled();

  /// @brief フェードアウト処理
  void UpdateFade(core::GameContext &ctx, float dt);

  /// @brief 床と壁を生成
  void CreateBoundaries(core::GameContext &ctx);

  /// @brief カメラの軽い揺らぎ
  void UpdateCamera(core::GameContext &ctx, float dt);

  /// @brief UIの更新
  void UpdateUI(core::GameContext &ctx);

  /// @brief フェードに合わせた縮小・透明演出
  void ApplyFadeToScene(core::GameContext &ctx);

  // 次シーン生成ファクトリ
  std::function<std::unique_ptr<core::Scene>()> m_nextSceneFactory;

  // ボール関連
  std::vector<BallState> m_balls;
  resources::MeshHandle m_ballMeshHandle;
  int m_spawnedCount = 0;
  float m_spawnTimer = 0.0f;

  // 演出設定（巨大ボール積載仕様）
  static constexpr int TOTAL_BALLS = 24;         ///< 量感を出すための総数
  static constexpr float SPAWN_INTERVAL = 0.15f; ///< 心地よいテンポ
  static constexpr float BALL_RADIUS = 3.5f;     ///< 物理半径（衝突用の基準）
  static constexpr float BALL_MODEL_SCALE =
      3.0f; ///< 見た目を大きくする係数（当たり判定は変えない）
  static constexpr float GRAVITY = -120.0f; ///< しっかり落とす
  static constexpr float RESTITUTION =
      0.28f; ///< 軽く弾ませて「動いている感」を出す
  static constexpr float FRICTION = 0.82f; ///< 横滑りを抑える
  static constexpr float AIR_DRAG =
      0.94f; ///< 毎フレームの減衰（dtを掛けた指数減衰）
  static constexpr float ANGULAR_DAMPING = 0.9f; ///< 回転の減衰
  static constexpr float SETTLE_THRESHOLD =
      1.8f; ///< 早めに静止させて安定させる

  // フェードアウト
  float m_fadeAlpha = 0.0f;
  bool m_fadeStarted = false;
  float m_fadeDelay = 0.6f; ///< 全ボール静止後の待機時間
  static constexpr float FADE_SPEED = 1.5f;
  float m_sceneTime = 0.0f;
  float m_logTimer = 0.0f;
  bool m_spawnFinishedLogged = false;
  bool m_allSettledLogged = false;
  bool m_fadeLogged = false;
  int m_movingCount = 0;
  float m_maxSpeed = 0.0f;
  float m_avgSpeed = 0.0f;
  int m_settledCount = 0;
  int m_lastSettledCount = 0;
  float m_stuckTimer = 0.0f;
  DirectX::XMFLOAT3 m_lastMovingPos{0.0f, 0.0f, 0.0f};
  bool m_hasMovingSample = false;
  float m_forceFinishTimer = 0.0f; // 強制終了用タイマー

  // UI
  ecs::Entity m_textEntity;
  ecs::Entity m_progressTextEntity;
  ecs::Entity m_captionTextEntity;
  ecs::Entity m_fadeOverlay;
  graphics::TextStyle m_primaryStyle{};
  graphics::TextStyle m_progressStyle{};
  graphics::TextStyle m_captionStyle{};
  float m_tipTimer = 0.0f;
  size_t m_tipIndex = 0;

  // カメラ
  ecs::Entity m_cameraEntity;
  float m_cameraTime = 0.0f;

  // 床
  ecs::Entity m_floorEntity;
  std::vector<ecs::Entity> m_wallEntities;
  ecs::Entity m_backdropEntity;

  // ステージ寸法
  static constexpr float ARENA_HALF_WIDTH = 24.0f;
  static constexpr float ARENA_HALF_DEPTH = 14.0f;
  static constexpr float FLOOR_Y = -8.0f;

  // 非同期ロード
  std::future<std::unique_ptr<game::components::WikiGlobalData>> m_loadTask;
  bool m_isLoading = false;
  bool m_loadCompleted = false;
};

} // namespace game::scenes
