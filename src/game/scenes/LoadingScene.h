#pragma once
/**
 * @file LoadingScene.h
 * @brief ローディング画面シーン（ゴルフボール物理演出）
 */

#include "../../core/Scene.h"
#include "../../ecs/Entity.h"
#include "../../resources/ResourceManager.h"
#include <DirectXMath.h>
#include <functional>
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
    bool settled; ///< 静止したかどうか
  };

  /// @brief ゴルフボールをスポーンする
  void SpawnBall(core::GameContext &ctx);

  /// @brief 物理シミュレーション更新
  void UpdatePhysics(core::GameContext &ctx, float dt);

  /// @brief すべてのボールが静止したか判定
  bool AreAllBallsSettled() const;

  /// @brief フェードアウト処理
  void UpdateFade(core::GameContext &ctx, float dt);

  /// @brief 床と壁を生成
  void CreateBoundaries(core::GameContext &ctx);

  // 次シーン生成ファクトリ
  std::function<std::unique_ptr<core::Scene>()> m_nextSceneFactory;

  // ボール関連
  std::vector<BallState> m_balls;
  resources::MeshHandle m_ballMeshHandle;
  int m_spawnedCount = 0;
  float m_spawnTimer = 0.0f;

  // 演出設定（巨大ボール積載仕様）
  static constexpr int TOTAL_BALLS = 20;         ///< 水槽を埋めるのに適した量
  static constexpr float SPAWN_INTERVAL = 0.25f; ///< 早すぎず遅すぎない間隔
  static constexpr float BALL_RADIUS = 10.0f;    ///< 巨大ボール
  static constexpr float GRAVITY = -150.0f;      ///< 巨大なのでドスンと落とす
  static constexpr float RESTITUTION = 0.0f; ///< 全く跳ねない（積み重なり重視）
  static constexpr float FRICTION = 0.6f;    ///< 少し滑る（隙間を埋める）
  static constexpr float SETTLE_THRESHOLD =
      0.8f; ///< 早めに静止させて安定させる

  // フェードアウト
  float m_fadeAlpha = 0.0f;
  bool m_fadeStarted = false;
  float m_fadeDelay = 0.5f; ///< 全ボール静止後の待機時間
  static constexpr float FADE_SPEED = 1.5f;

  // UI
  ecs::Entity m_textEntity;
  ecs::Entity m_fadeOverlay;

  // カメラ
  ecs::Entity m_cameraEntity;

  // 床
  ecs::Entity m_floorEntity;
  std::vector<ecs::Entity> m_wallEntities;
};

} // namespace game::scenes
