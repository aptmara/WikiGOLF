#pragma once
/**
 * @file GameJuiceSystem.h
 * @brief ゲームの演出効果（Game Juice）を管理するシステム
 *
 * カメラシェイク、FOV変化、トレイル、インパクトエフェクトなど
 * プレイヤーのアクションに対するフィードバック演出を提供する。
 */

#include "../../ecs/Entity.h"
#include "../components/WikiComponents.h"
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

  // === タイムコントロール ===

  /// @brief 一時的なヒットストップを発火
  /// @param duration 継続時間（秒）
  /// @param timeScale 停止中のスケール（0.0fで完全停止）
  void TriggerHitStop(float duration, float timeScale = 0.0f);

  /// @brief スローモーションを開始
  /// @param duration 継続時間
  /// @param scale 適用するスケール
  void TriggerSlowMotion(float duration, float scale);

  /// @brief 未スケールのdtから有効タイムスケールを計算
  float ConsumeTimeScale(float unscaledDt);

  // === インパクトエフェクト ===

  /// @brief 判定タイプ（パーティクルの色・挙動に影響）
  enum class JudgeType { None, Great, Nice, Miss, Special };

  /// @brief インパクトエフェクト発火
  /// @param ctx ゲームコンテキスト
  /// @param position エフェクト発生位置
  /// @param power エフェクトの強さ（パーティクル速度に影響）
  /// @param judge 判定タイプ（デフォルト: None）
  void TriggerImpactEffect(core::GameContext &ctx,
                           const DirectX::XMFLOAT3 &position, float power,
                           JudgeType judge = JudgeType::None);

  /// @brief ショット時の精度別パーティクルを発火
  /// @param ctx ゲームコンテキスト
  /// @param position 発生位置
  /// @param direction ショット方向
  /// @param power ショットの強さ
  /// @param judge 判定タイプ
  void TriggerShotEffect(core::GameContext &ctx,
                         const DirectX::XMFLOAT3 &position,
                         const DirectX::XMFLOAT3 &direction, float power,
                         JudgeType judge);

  // === マテリアルエフェクト ===

  /// @brief マテリアルに応じたエフェクト発火
  /// @param ctx ゲームコンテキスト
  /// @param position 発生位置
  /// @param material マテリアル種別
  /// @param strength 強さ（0.0-1.0）
  void TriggerMaterialEffect(core::GameContext &ctx,
                             const DirectX::XMFLOAT3 &position,
                             game::components::TerrainMaterial material,
                             float strength);

  /// @brief リップルエフェクトを発火（強いバウンド時）
  void TriggerRippleEffect(core::GameContext &ctx,
                           const DirectX::XMFLOAT3 &position,
                           float baseRadius, float strength);

  /// @brief カップインなど祝祭時の星・紙吹雪・きらめきを発火
  void TriggerConfetti(core::GameContext &ctx,
                       const DirectX::XMFLOAT3 &position, float burstPower);

  // === トレイル ===

  /// @brief トレイルをリセット（ページ遷移時などに呼ぶ）
  void ResetTrail();

private:
  // --- カメラシェイク ---
  float m_shakeIntensity = 0.0f;
  float m_shakeDuration = 0.0f;
  float m_shakeTimer = 0.0f;
  float m_shakeFrequency = 25.0f;

  // --- タイムコントロール ---
  float m_timeScale = 1.0f;
  float m_hitStopTimer = 0.0f;
  float m_hitStopDuration = 0.0f;
  float m_hitStopScale = 0.0f;
  float m_slowMoTimer = 0.0f;
  float m_slowMoDuration = 0.0f;
  float m_slowMoScale = 1.0f;

  // --- FOV ---
  float m_baseFov = 60.0f;
  float m_currentFov = 60.0f;
  float m_targetFov = 60.0f;
  float m_fovPunchTimer = 0.0f;
  float m_fovPunchDuration = 0.0f;
  float m_fovPunchStrength = 0.0f;

  // --- トレイル ---
  std::vector<ecs::Entity> m_trailEntities;
  std::vector<DirectX::XMFLOAT3> m_trailPositions;
  std::vector<DirectX::XMFLOAT4> m_trailBaseColors;
  int m_trailWriteIndex = 0;
  float m_trailUpdateTimer = 0.0f;
  static constexpr int kTrailCount = 24;                ///< 軌跡プール数です。山内陽
  static constexpr float kTrailUpdateInterval = 0.025f; ///< 軌跡更新間隔です。山内陽

  // --- インパクトエフェクト ---
  enum class ImpactParticleKind {
    Burst,
    Confetti,
    Star,
    Sparkle,
    Glint,
    ShotSpark,
    ShotDust,
    ShotRing
  };

  struct ImpactParticle {
    ecs::Entity entity = UINT32_MAX;
    DirectX::XMFLOAT3 velocity = {0, 0, 0};
    DirectX::XMFLOAT3 angularVelocity = {0, 0, 0};
    float lifetime = 0.0f;
    float maxLifetime = 1.0f;
    float baseScale = 0.1f;
    DirectX::XMFLOAT4 baseColor = {1, 1, 1, 1};
    ImpactParticleKind kind = ImpactParticleKind::Burst;
  };
  std::vector<ImpactParticle> m_impactParticles;
  static constexpr int kImpactBurstCount = 48; ///< インパクト粒子数です。山内陽
  static constexpr int kImpactParticleCount = 96; ///< 祝祭を含む粒子数です。山内陽

  // --- 環境エフェクト（転がり・スライド） ---
  enum class EnvironmentParticleKind {
    GrassClip,
    SandDust,
    SandGrain,
    SandClump,
    GenericDebris
  };

  struct EnvironmentParticle {
    ecs::Entity entity = UINT32_MAX;
    DirectX::XMFLOAT3 velocity = {0, 0, 0};
    DirectX::XMFLOAT3 angularVelocity = {0, 0, 0};
    float lifetime = 0.0f;
    float maxLifetime = 1.0f;
    float baseScale = 0.1f;
    float groundHeight = 0.0f;
    DirectX::XMFLOAT4 baseColor = {1, 1, 1, 1};
    EnvironmentParticleKind kind = EnvironmentParticleKind::GrassClip;
  };
  std::vector<EnvironmentParticle> m_envParticles;
  int m_envWriteIndex = 0;
  float m_envEmitTimer = 0.0f;
  static constexpr int kEnvParticleCount = 256;

  // --- バンカー表面の窪み・ボール周囲の砂の盛り上がり ---
  struct SandImprint {
    ecs::Entity entity = UINT32_MAX;
    float lifetime = 0.0f;
    float maxLifetime = 1.0f;
    float startScale = 0.1f;
    DirectX::XMFLOAT4 baseColor = {1, 1, 1, 1};
  };
  std::vector<SandImprint> m_sandImprints;
  int m_sandImprintWriteIndex = 0;
  float m_sandTrackTimer = 0.0f;
  ecs::Entity m_sandCollarEntity = UINT32_MAX;
  static constexpr int kSandImprintCount = 28;

  // --- リップルエフェクト ---
  struct Ripple {
    ecs::Entity entity = UINT32_MAX;
    float lifetime = 0.0f;
    float maxLifetime = 1.0f;
    float startScale = 1.0f;
  };
  std::vector<Ripple> m_ripples;
  int m_rippleWriteIndex = 0;
  static constexpr int kRippleCount = 12; ///< リップル同時数です。山内陽

  // --- 内部処理 ---
  void UpdateCameraShake(core::GameContext &ctx, ecs::Entity cameraEntity);
  void UpdateFov(core::GameContext &ctx, ecs::Entity cameraEntity);
  void UpdateTrail(core::GameContext &ctx, ecs::Entity targetEntity);
  void UpdateImpactParticles(core::GameContext &ctx);
  void UpdateEnvironmentParticles(core::GameContext &ctx,
                                  ecs::Entity targetEntity);
  void UpdateSandSurfaceEffects(core::GameContext &ctx,
                                ecs::Entity targetEntity);
  void UpdateRipples(core::GameContext &ctx);
  float UpdateFovPunch(float dt);
  void EmitEnvironmentParticles(core::GameContext &ctx,
                                ecs::Entity targetEntity);

  void CreateTrailEntities(core::GameContext &ctx);
  void CreateImpactParticleEntities(core::GameContext &ctx);
  void CreateEnvironmentParticleEntities(core::GameContext &ctx);
  void CreateSandSurfaceEntities(core::GameContext &ctx);
  void SpawnSandImprint(core::GameContext &ctx,
                        const DirectX::XMFLOAT3 &position, float scale,
                        float lifetime, const DirectX::XMFLOAT4 &color);
  void CreateRippleEntities(core::GameContext &ctx);
};

} // namespace game::systems
