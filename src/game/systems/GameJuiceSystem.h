#pragma once
/**
 * @file GameJuiceSystem.h
 * @brief ゲームの演出効果（Game Juice）を管理するシステム
 *
 * カメラシェイク、FOV変化、トレイル、インパクトエフェクトなど
 * プレイヤーのアクションに対するフィードバック演出を提供する。
 */

#include "../../ecs/Entity.h"
#include <DirectXMath.h>
#include <vector>

namespace core {
struct GameContext;
}

namespace game::systems {

/**
 * @brief Game Juice（演出効果）システム
 *
 * 使用方法:
 * 1. Initialize() でエンティティプールを作成
 * 2. 毎フレーム Update() を呼ぶ
 * 3. イベント発生時に TriggerXXX() を呼ぶ
 */
class GameJuiceSystem {
public:
  GameJuiceSystem() = default;
  ~GameJuiceSystem() = default;

  // コピー禁止
  GameJuiceSystem(const GameJuiceSystem &) = delete;
  GameJuiceSystem &operator=(const GameJuiceSystem &) = delete;

  /// @brief 初期化（エンティティプール作成）
  /// @param ctx ゲームコンテキスト
  void Initialize(core::GameContext &ctx);

  /// @brief 毎フレーム更新
  /// @param ctx ゲームコンテキスト
  /// @param cameraEntity カメラエンティティ（シェイク・FOV適用対象）
  /// @param targetEntity トレイル追跡対象エンティティ（ボールなど）
  void Update(core::GameContext &ctx, ecs::Entity cameraEntity,
              ecs::Entity targetEntity);

  // === カメラシェイク ===

  /// @brief カメラシェイクを発火
  /// @param intensity 揺れの強さ（0.0〜1.0推奨）
  /// @param duration 継続時間（秒）
  void TriggerCameraShake(float intensity, float duration);

  // === FOV変化 ===

  /// @brief 目標FOVを設定（度単位）
  /// @param fov 目標FOV（度）
  void SetTargetFov(float fov);

  /// @brief 基本FOVにリセット
  void ResetFov();

  /// @brief 現在のFOVを取得（度単位）
  float GetCurrentFov() const { return m_currentFov; }

  // === インパクトエフェクト ===

  /// @brief 判定タイプ（パーティクルの色・挙動に影響）
  enum class JudgeType { None, Great, Nice, Miss };

  /// @brief インパクトエフェクト発火
  /// @param ctx ゲームコンテキスト
  /// @param position エフェクト発生位置
  /// @param power エフェクトの強さ（パーティクル速度に影響）
  /// @param judge 判定タイプ（デフォルト: None）
  void TriggerImpactEffect(core::GameContext &ctx,
                           const DirectX::XMFLOAT3 &position, float power,
                           JudgeType judge = JudgeType::None);

  // === トレイル ===

  /// @brief トレイルをリセット（ページ遷移時などに呼ぶ）
  void ResetTrail();

private:
  // --- カメラシェイク ---
  float m_shakeIntensity = 0.0f;
  float m_shakeDuration = 0.0f;
  float m_shakeTimer = 0.0f;
  float m_shakeFrequency = 25.0f;

  // --- FOV ---
  float m_baseFov = 60.0f;
  float m_currentFov = 60.0f;
  float m_targetFov = 60.0f;

  // --- トレイル ---
  std::vector<ecs::Entity> m_trailEntities;
  std::vector<DirectX::XMFLOAT3> m_trailPositions;
  int m_trailWriteIndex = 0;
  float m_trailUpdateTimer = 0.0f;
  static constexpr int kTrailCount = 40;               // 派手に増量
  static constexpr float kTrailUpdateInterval = 0.02f; // 50Hz

  // --- インパクトエフェクト ---
  struct ImpactParticle {
    ecs::Entity entity = UINT32_MAX;
    DirectX::XMFLOAT3 velocity = {0, 0, 0};
    float lifetime = 0.0f;
  };
  std::vector<ImpactParticle> m_impactParticles;
  static constexpr int kImpactParticleCount = 80; // 大幅増量で派手に

  // --- 内部処理 ---
  void UpdateCameraShake(core::GameContext &ctx, ecs::Entity cameraEntity);
  void UpdateFov(core::GameContext &ctx, ecs::Entity cameraEntity);
  void UpdateTrail(core::GameContext &ctx, ecs::Entity targetEntity);
  void UpdateImpactParticles(core::GameContext &ctx);

  void CreateTrailEntities(core::GameContext &ctx);
  void CreateImpactParticleEntities(core::GameContext &ctx);
};

} // namespace game::systems
