/**
 * @file GameJuiceSystem.cpp
 * @brief ゲームの演出効果（Game Juice）システム実装
 */

#include "GameJuiceSystem.h"
#include "GameJuiceVisualRules.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "PhysicsFriction.h"
#include <algorithm>
#include <cmath>

// Windowsマクロ対策
#include <windows.h> // 必要なら
#undef min
#undef max

namespace game::systems {

using namespace DirectX;
using namespace game::components;
using namespace juice_detail;

void GameJuiceSystem::Initialize(core::GameContext &ctx) {
  LOG_INFO("GameJuice", "Initializing Game Juice System...");

  // トレイルエンティティ作成
  CreateTrailEntities(ctx);

  // インパクトパーティクルエンティティ作成
  CreateImpactParticleEntities(ctx);

  // 環境パーティクルエンティティ作成
  CreateEnvironmentParticleEntities(ctx);

  // バンカーの窪みとボール周囲の砂
  CreateSandSurfaceEntities(ctx);

  // リップルエフェクト
  CreateRippleEntities(ctx);

  // FOV初期化
  m_baseFov = 60.0f;
  m_currentFov = m_baseFov;
  m_targetFov = m_baseFov;

  LOG_INFO("GameJuice", "Game Juice System initialized.");
}

void GameJuiceSystem::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_trailEntities.clear();
  m_trailPositions.clear();
  m_trailBaseColors.clear();
  m_impactParticles.clear();
  m_envParticles.clear();
  m_sandImprints.clear();
  m_ripples.clear();
  m_sandCollarEntity = UINT32_MAX;
  m_hasLastTrailTargetPosition = false;
}

void GameJuiceSystem::Update(core::GameContext &ctx, ecs::Entity cameraEntity,
                             ecs::Entity targetEntity) {
  // カメラシェイク更新
  UpdateCameraShake(ctx, cameraEntity);

  // FOV更新
  UpdateFov(ctx, cameraEntity);

  // トレイル更新
  UpdateTrail(ctx, targetEntity);

  // インパクトパーティクル更新
  UpdateImpactParticles(ctx);

  // 環境パーティクル更新
  EmitEnvironmentParticles(ctx, targetEntity);
  UpdateEnvironmentParticles(ctx, targetEntity);
  UpdateSandSurfaceEffects(ctx, targetEntity);

  // リップル更新
  UpdateRipples(ctx);
}

// =============================================================================
// タイムコントロール
// =============================================================================

void GameJuiceSystem::TriggerHitStop(float duration, float timeScale) {
  m_hitStopDuration = std::max(0.0f, duration);
  m_hitStopTimer = m_hitStopDuration;
  m_hitStopScale = std::clamp(timeScale, 0.0f, 1.0f);
}

void GameJuiceSystem::TriggerSlowMotion(float duration, float scale) {
  m_slowMoDuration = std::max(0.0f, duration);
  m_slowMoTimer = std::max(m_slowMoTimer, m_slowMoDuration);
  m_slowMoScale = std::clamp(scale, 0.05f, 1.0f);
}

float GameJuiceSystem::ConsumeTimeScale(float unscaledDt) {
  // ヒットストップ優先
  float activeScale = 1.0f;
  if (m_hitStopTimer > 0.0f) {
    m_hitStopTimer = std::max(0.0f, m_hitStopTimer - unscaledDt);
    activeScale = m_hitStopScale;
  }

  if (m_slowMoTimer > 0.0f && m_slowMoDuration > 0.0f) {
    m_slowMoTimer = std::max(0.0f, m_slowMoTimer - unscaledDt);
    float progress = 1.0f - (m_slowMoTimer / m_slowMoDuration);
    float easeOut = 1.0f - std::pow(std::clamp(progress, 0.0f, 1.0f), 2.0f);
    float slowScale = m_slowMoScale + (1.0f - m_slowMoScale) * easeOut;
    activeScale = std::min(activeScale, slowScale);
  }

  m_timeScale = std::clamp(activeScale, 0.0f, 1.0f);
  return m_timeScale;
}

// =============================================================================
// カメラシェイク
// =============================================================================

void GameJuiceSystem::TriggerCameraShake(float intensity, float duration) {
  m_shakeIntensity = intensity;
  m_shakeDuration = duration;
  m_shakeTimer = 0.0f;
  LOG_DEBUG("GameJuice",
            "Camera shake triggered: intensity={:.2f}, duration={:.2f}",
            intensity, duration);
}

void GameJuiceSystem::UpdateCameraShake(core::GameContext &ctx,
                                        ecs::Entity cameraEntity) {
  if (m_shakeDuration <= 0.0f)
    return;

  if (!ctx.world.IsAlive(cameraEntity))
    return;

  auto *camT = ctx.world.Get<Transform>(cameraEntity);
  if (!camT)
    return;

  m_shakeTimer += ctx.dt;
  m_shakeDuration -= ctx.dt;

  if (m_shakeDuration <= 0.0f) {
    m_shakeDuration = 0.0f;
    m_shakeIntensity = 0.0f;
    return;
  }

  // 減衰計算（時間とともに弱くなる）
  float decay = m_shakeDuration / (m_shakeDuration + ctx.dt * 2.0f);
  float currentIntensity = m_shakeIntensity * decay;

  // Perlinノイズ風に複数周波数を重ね合わせ
  float t = m_shakeTimer;
  float offsetX = std::sin(t * m_shakeFrequency) * 0.5f +
                  std::sin(t * m_shakeFrequency * 2.3f) * 0.3f +
                  std::sin(t * m_shakeFrequency * 5.7f) * 0.2f;
  float offsetY = std::cos(t * m_shakeFrequency * 1.1f) * 0.5f +
                  std::cos(t * m_shakeFrequency * 3.1f) * 0.3f +
                  std::cos(t * m_shakeFrequency * 4.3f) * 0.2f;

  // カメラ位置にオフセット適用（一時的）
  camT->position.x += offsetX * currentIntensity;
  camT->position.y += offsetY * currentIntensity * 0.5f; // Y方向は控えめ
}

// =============================================================================
// FOV変化
// =============================================================================

void GameJuiceSystem::SetTargetFov(float fov) {
  m_targetFov = std::clamp(fov, 30.0f, 120.0f);
}

void GameJuiceSystem::ResetFov() { m_targetFov = m_baseFov; }

void GameJuiceSystem::UpdateFov(core::GameContext &ctx,
                                ecs::Entity cameraEntity) {
  // 滑らかに補間
  float speed = 8.0f;
  float punchOffset = UpdateFovPunch(ctx.dt);
  float desiredFov = m_targetFov - punchOffset;
  m_currentFov += (desiredFov - m_currentFov) * speed * ctx.dt;

  if (!ctx.world.IsAlive(cameraEntity))
    return;

  auto *cam = ctx.world.Get<Camera>(cameraEntity);
  if (cam) {
    cam->fov = XMConvertToRadians(m_currentFov);
  }
}

float GameJuiceSystem::UpdateFovPunch(float dt) {
  if (m_fovPunchTimer <= 0.0f || m_fovPunchDuration <= 0.0f ||
      m_fovPunchStrength <= 0.0f) {
    m_fovPunchTimer = 0.0f;
    return 0.0f;
  }

  m_fovPunchTimer = std::max(0.0f, m_fovPunchTimer - dt);
  float progress = 1.0f - (m_fovPunchTimer / m_fovPunchDuration);
  float eased = std::sin(std::clamp(progress, 0.0f, 1.0f) * XM_PI);
  return m_fovPunchStrength * eased;
}

// =============================================================================
// トレイル
// =============================================================================



} // namespace game::systems
