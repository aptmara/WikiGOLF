/**
 * @file GameJuiceSystem.cpp
 * @brief ゲームの演出効果（Game Juice）システム実装
 */

#include "GameJuiceSystem.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <algorithm>
#include <cmath>

// Windowsマクロ対策
#include <windows.h> // 必要なら
#undef min
#undef max

namespace game::systems {

using namespace DirectX;
using namespace game::components;

void GameJuiceSystem::Initialize(core::GameContext &ctx) {
  LOG_INFO("GameJuice", "Initializing Game Juice System...");

  // トレイルエンティティ作成
  CreateTrailEntities(ctx);

  // インパクトパーティクルエンティティ作成
  CreateImpactParticleEntities(ctx);

  // 環境パーティクルエンティティ作成
  CreateEnvironmentParticleEntities(ctx);

  // リップルエフェクト
  CreateRippleEntities(ctx);

  // FOV初期化
  m_baseFov = 60.0f;
  m_currentFov = m_baseFov;
  m_targetFov = m_baseFov;

  LOG_INFO("GameJuice", "Game Juice System initialized.");
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

void GameJuiceSystem::CreateTrailEntities(core::GameContext &ctx) {
  m_trailEntities.clear();
  m_trailPositions.clear();
  m_trailBaseColors.clear();
  m_trailPositions.resize(kTrailCount, {0, -100, 0}); // 画面外で初期化
  m_trailBaseColors.resize(kTrailCount);

  for (int i = 0; i < kTrailCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};     // 画面外
    t.scale = {0.08f, 0.08f, 0.08f}; // 大きめ

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isTransparent = true;

    // 虹色グラデーション（派手に）
    float ratio = (float)i / (float)(kTrailCount - 1);
    float hue = ratio * 360.0f; // 0-360度
    float h = hue / 60.0f;
    int hi = (int)h % 6;
    float f = h - (int)h;
    float r, g, b;
    switch (hi) {
    case 0:
      r = 1.0f;
      g = f;
      b = 0.0f;
      break;
    case 1:
      r = 1.0f - f;
      g = 1.0f;
      b = 0.0f;
      break;
    case 2:
      r = 0.0f;
      g = 1.0f;
      b = f;
      break;
    case 3:
      r = 0.0f;
      g = 1.0f - f;
      b = 1.0f;
      break;
    case 4:
      r = f;
      g = 0.0f;
      b = 1.0f;
      break;
    default:
      r = 1.0f;
      g = 0.0f;
      b = 1.0f - f;
      break;
    }
    float alpha = 0.95f * (1.0f - ratio * 0.6f);
    mr.color = {r * 1.5f, g * 1.5f, b * 1.5f, alpha}; // 発光感
    m_trailBaseColors[i] = mr.color;
    mr.isVisible = false;

    m_trailEntities.push_back(e);
  }

  m_trailWriteIndex = 0;
  m_trailUpdateTimer = 0.0f;
}

void GameJuiceSystem::ResetTrail() {
  m_trailWriteIndex = 0;
  m_trailUpdateTimer = 0.0f;
  for (auto &pos : m_trailPositions) {
    pos = {0, -100, 0};
  }
}

void GameJuiceSystem::UpdateTrail(core::GameContext &ctx,
                                  ecs::Entity targetEntity) {
  if (!ctx.world.IsAlive(targetEntity))
    return;

  auto *targetT = ctx.world.Get<Transform>(targetEntity);
  if (!targetT)
    return;

  // ボールが動いているかチェック
  auto *rb = ctx.world.Get<components::RigidBody>(targetEntity);
  bool isMoving = false;
  if (rb) {
    float speed = std::sqrt(rb->velocity.x * rb->velocity.x +
                            rb->velocity.y * rb->velocity.y +
                            rb->velocity.z * rb->velocity.z);
    isMoving = speed > 0.5f;
  }

  // 動いていない場合はトレイルを非表示
  if (!isMoving) {
    for (auto e : m_trailEntities) {
      auto *mr = ctx.world.Get<MeshRenderer>(e);
      if (mr) {
        mr->color.w *= 0.85f;
        if (mr->color.w < 0.02f) {
          mr->isVisible = false;
        }
      }
    }
    return;
  }

  float speed =
      std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.y * rb->velocity.y +
                rb->velocity.z * rb->velocity.z);
  float speedNormalized = std::clamp(speed / 35.0f, 0.0f, 1.0f);
  auto *shotState = ctx.world.GetGlobal<components::ShotState>();
  components::ShotJudgement judgement =
      shotState ? shotState->judgement : components::ShotJudgement::None;

  // 一定間隔で位置を記録
  m_trailUpdateTimer += ctx.dt;
  if (m_trailUpdateTimer >= kTrailUpdateInterval) {
    m_trailUpdateTimer = 0.0f;

    // リングバッファに書き込み
    m_trailPositions[m_trailWriteIndex] = targetT->position;
    m_trailWriteIndex = (m_trailWriteIndex + 1) % kTrailCount;
  }

  // トレイルエンティティの位置とスケールを更新
  for (int i = 0; i < kTrailCount; ++i) {
    // リングバッファのインデックス計算（古い順）
    int posIndex = (m_trailWriteIndex + i) % kTrailCount;
    auto &pos = m_trailPositions[posIndex];

    auto e = m_trailEntities[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (t && mr) {
      t->position = pos;

      // 古いほど小さく
      float ratio = (float)i / (float)(kTrailCount - 1);
      float fade = std::pow(1.0f - ratio, 1.5f);
      float sizeEase = 0.65f + 0.45f * std::pow(1.0f - ratio, 2.3f);
      float scaleBase = (0.07f + speedNormalized * 0.05f) * sizeEase;
      t->scale = {scaleBase, scaleBase, scaleBase};

      DirectX::XMFLOAT4 baseColor = m_trailBaseColors[posIndex];
      DirectX::XMFLOAT3 tint = {1.0f, 0.5f + speedNormalized * 0.5f, 0.15f};
      if (judgement == components::ShotJudgement::Great) {
        tint = {1.2f, 1.0f, 0.35f};
      } else if (judgement == components::ShotJudgement::Nice) {
        tint = {0.4f, 0.9f, 1.2f};
      } else if (judgement == components::ShotJudgement::Miss) {
        tint = {0.9f, 0.3f, 0.3f};
      }

      float mix = 0.25f + speedNormalized * 0.55f;
      mr->color.x = baseColor.x * (1.0f - mix) + tint.x * mix;
      mr->color.y = baseColor.y * (1.0f - mix) + tint.y * mix;
      mr->color.z = baseColor.z * (1.0f - mix) + tint.z * mix;
      mr->color.w = baseColor.w * fade;

      // 画面外でなければ表示
      mr->isVisible = (pos.y > -50.0f);
    }
  }
}

// =============================================================================
// インパクトエフェクト
// =============================================================================

void GameJuiceSystem::CreateImpactParticleEntities(core::GameContext &ctx) {
  m_impactParticles.clear();
  m_impactParticles.reserve(kImpactParticleCount);

  for (int i = 0; i < kImpactParticleCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0}; // 画面外
    t.scale = {0.1f, 0.1f, 0.1f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.color = {1.0f, 0.8f, 0.2f, 1.0f}; // 黄金色
    mr.isVisible = false;
    mr.isTransparent = true;

    ImpactParticle particle;
    particle.entity = e;
    particle.lifetime = 0.0f;
    m_impactParticles.push_back(particle);
  }
}

void GameJuiceSystem::TriggerImpactEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float power, JudgeType judge) {
  LOG_DEBUG("GameJuice",
            "Impact effect triggered at ({:.2f}, {:.2f}, {:.2f}) power={:.2f} "
            "judge={}",
            position.x, position.y, position.z, power, (int)judge);

  // 演出テンポ: ヒットストップ + FOVパンチ
  TriggerHitStop(0.06f + power * 0.04f);
  m_fovPunchStrength = 8.0f + power * 6.0f;
  m_fovPunchDuration = 0.18f;
  m_fovPunchTimer = m_fovPunchDuration;

  // 判定によって派手さを調整
  float speedMultiplier = 1.0f;
  float sizeMultiplier = 1.0f;
  float lifetimeMultiplier = 1.0f;

  switch (judge) {
  case JudgeType::Great:
    speedMultiplier = 1.8f;    // より高速に飛び散る
    sizeMultiplier = 1.6f;     // 大きめ
    lifetimeMultiplier = 1.35f; // 長寿命
    break;
  case JudgeType::Special:
    speedMultiplier = 2.0f;
    sizeMultiplier = 1.8f;
    lifetimeMultiplier = 1.45f;
    break;
  case JudgeType::Nice:
    speedMultiplier = 1.2f;
    sizeMultiplier = 1.2f;
    lifetimeMultiplier = 1.1f;
    break;
  case JudgeType::Miss:
    speedMultiplier = 0.7f; // 弱め
    sizeMultiplier = 0.8f;
    lifetimeMultiplier = 0.8f;
    break;
  default:
    break;
  }

  float baseSpeed = (8.0f + power * 15.0f) * speedMultiplier;
  float spreadFactor = 1.0f + power * 0.5f;

  for (int i = 0; i < kImpactParticleCount; ++i) {
    auto &p = m_impactParticles[i];

    // === 多層構造の爆発エフェクト ===
    int layer = i % 4; // 4層構造
    float layerOffset = layer * 0.25f;
    float layerSpeed = baseSpeed * (1.0f - layerOffset * 0.3f);

    // 放射状に速度を設定（スパイラル風）
    float baseAngle = (float)i / (float)kImpactParticleCount * XM_2PI;
    float spiralOffset = (float)layer * 0.3f;
    float angle = baseAngle + spiralOffset;

    // 上向きのばらつき（花火風に上に多く）
    float upAngle =
        XM_PIDIV4 * (1.0f + ((float)(rand() % 100) / 100.0f) * 1.5f);
    if (layer == 0)
      upAngle *= 1.3f;

    p.velocity.x =
        std::cos(angle) * std::cos(upAngle) * layerSpeed * spreadFactor;
    p.velocity.y = std::sin(upAngle) * layerSpeed * 1.2f;
    p.velocity.z =
        std::sin(angle) * std::cos(upAngle) * layerSpeed * spreadFactor;

    // ランダムなばらつき
    p.velocity.x += ((float)(rand() % 100) / 100.0f - 0.5f) * 5.0f;
    p.velocity.y += ((float)(rand() % 100) / 100.0f) * 3.0f;
    p.velocity.z += ((float)(rand() % 100) / 100.0f - 0.5f) * 5.0f;

    // 寿命
    p.lifetime =
        (0.6f + ((float)(rand() % 100) / 100.0f) * 0.5f + layer * 0.15f) *
        lifetimeMultiplier;
    p.maxLifetime = p.lifetime;

    // 初期位置設定
    auto *t = ctx.world.Get<Transform>(p.entity);
    if (t) {
      t->position = position;
      t->position.x += ((float)(rand() % 100) / 100.0f - 0.5f) * 0.3f;
      t->position.y += 0.1f + ((float)(rand() % 100) / 100.0f) * 0.2f;
      t->position.z += ((float)(rand() % 100) / 100.0f - 0.5f) * 0.3f;

      float baseScale = (0.15f + power * 0.1f) * sizeMultiplier;
      float scaleVar = 0.8f + ((float)(rand() % 100) / 100.0f) * 0.4f;
      float scale = baseScale * scaleVar;
      t->scale = {scale, scale, scale};
      p.baseScale = scale;
    }

    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (mr) {
      mr->isVisible = true;

      float r, g, b;

      // === 判定ごとの色設定 ===
      switch (judge) {
      case JudgeType::Great:
        // 金色～白の豪華な爆発
        {
          float goldShift = (float)(rand() % 100) / 100.0f * 0.3f;
          r = 1.0f;
          g = 0.8f + goldShift;
          b = 0.2f + goldShift * 0.5f;
          if (layer == 0) {
            r = 1.0f;
            g = 1.0f;
            b = 0.9f;
          }
        }
        break;
      case JudgeType::Special:
        // 虹色に近い強調
        {
          float hue = ((float)(rand() % 360)) * (XM_PI / 180.0f);
          r = 0.6f + std::sin(hue) * 0.4f;
          g = 0.6f + std::sin(hue + 2.094f) * 0.4f;
          b = 0.6f + std::sin(hue + 4.188f) * 0.4f;
        }
        break;
      case JudgeType::Nice:
        // 青白い
        {
          r = 0.4f;
          g = 0.7f;
          b = 1.0f;
        }
        break;
      case JudgeType::Miss:
        // 赤
        {
          r = 1.0f;
          g = 0.3f;
          b = 0.1f;
        }
        break;
      default:
        r = 1.0f;
        g = 0.8f;
        b = 0.4f;
        break;
      }

      // 発光感
      float brightness = 2.0f + (1.0f - layerOffset) * 1.5f;
      p.baseColor = {r * brightness, g * brightness, b * brightness, 1.0f};
      mr->color = p.baseColor;
    }
  }
}

void GameJuiceSystem::UpdateImpactParticles(core::GameContext &ctx) {
  const float gravity = 15.0f;

  for (auto &p : m_impactParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= ctx.dt;

    // 物理更新
    p.velocity.y -= gravity * ctx.dt;
    p.velocity.x *= 0.98f; // 空気抵抗
    p.velocity.z *= 0.98f;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      t->position.x += p.velocity.x * ctx.dt;
      t->position.y += p.velocity.y * ctx.dt;
      t->position.z += p.velocity.z * ctx.dt;

      // 縮小しながらフェードアウト
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      float scale = p.baseScale * std::pow(lifeRatio, 0.5f);
      t->scale = {scale, scale, scale};
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      mr->color = p.baseColor;
      mr->color.w = lifeRatio; // アルファ減衰

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

// =============================================================================
// マテリアルエフェクト
// =============================================================================

void GameJuiceSystem::TriggerMaterialEffect(
    core::GameContext &ctx, const DirectX::XMFLOAT3 &position,
    game::components::TerrainMaterial material, float strength) {

  // マテリアルに応じたエフェクト発生
  // EnvironmentParticle の放出ロジックを流用・拡張する

  // 放出数（強さに応じて）
  int count = std::max(1, (int)(strength * 10.0f));
  if (material == game::components::TerrainMaterial::Bunker)
    count = std::max(2, count * 3); // バンカーはさらに多め

  for (int k = 0; k < count; ++k) {
    auto &p = m_envParticles[m_envWriteIndex];
    m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

    p.lifetime = 0.5f + ((float)(rand() % 100) / 100.0f) * 0.5f;
    p.maxLifetime = p.lifetime;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t && mr) {
      t->position = position;
      // 接地点より少し上
      t->position.y += 0.1f;

      mr->isVisible = true;

      // 初速計算
      float speed =
          strength * 5.0f * (0.8f + ((float)(rand() % 100) / 100.0f) * 0.4f);
      float angle = ((float)(rand() % 100) / 100.0f) * XM_2PI;
      float upBias = 0.5f + ((float)(rand() % 100) / 100.0f) * 0.5f;

      p.velocity.x = std::cos(angle) * speed * 0.5f;
      p.velocity.y = speed * upBias;
      p.velocity.z = std::sin(angle) * speed * 0.5f;

      // 個別パラメータ
      switch (material) {
      case game::components::TerrainMaterial::Bunker:
        // 砂煙
        p.isDust = true;
        mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
        mr->color = {0.85f, 0.75f, 0.55f, 0.7f};
        mr->customFlags.x = 1.0f;
        p.baseScale = 0.24f + strength * 0.25f;
        t->scale = {p.baseScale, p.baseScale, p.baseScale};
        p.lifetime *= 1.4f;
        p.maxLifetime = p.lifetime;
        p.velocity.x *= 0.55f;
        p.velocity.z *= 0.55f;
        p.velocity.y *= 0.85f;
        p.angularVelocity = {0, 0, 0};
        break;

      case game::components::TerrainMaterial::Rough:
      case game::components::TerrainMaterial::Fairway:
      case game::components::TerrainMaterial::Green: {
        // 芝・葉っぱ
        p.isDust = false;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube");
        mr->customFlags.x = 0.0f;

        if (material == game::components::TerrainMaterial::Rough) {
          mr->color = {0.15f, 0.45f, 0.15f, 1.0f}; // 濃い緑
          float sizeJitter = 0.7f + ((float)(rand() % 100) / 100.0f) * 0.6f;
          p.baseScale = (0.1f + strength * 0.15f) * sizeJitter;
        } else if (material == game::components::TerrainMaterial::Green) {
          mr->color = {0.2f, 0.8f, 0.3f, 1.0f};   // 鮮やか
          p.baseScale = 0.05f + strength * 0.05f; // 小さい
        } else {
          mr->color = {0.25f, 0.6f, 0.2f, 1.0f}; // 普通
          p.baseScale = 0.08f + strength * 0.1f;
        }

        t->scale = {p.baseScale * 1.5f, p.baseScale * 0.1f, p.baseScale * 1.5f};

        float spinScale = 45.0f;
        p.angularVelocity.x =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.y =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.z =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        break;
      }

      default:
        // 岩など（汎用拡散）
        p.isDust = true; // 簡略化のためDust扱い
        mr->mesh = ctx.resource.LoadMesh("builtin/cube"); // 岩片
        mr->color = {0.5f, 0.5f, 0.5f, 1.0f};
        mr->customFlags.x = 0.0f;
        p.baseScale = 0.05f + strength * 0.1f;
        t->scale = {p.baseScale, p.baseScale, p.baseScale};
        break;
      }
    }
  }
}

void GameJuiceSystem::CreateEnvironmentParticleEntities(
    core::GameContext &ctx) {
  m_envParticles.clear();
  m_envParticles.resize(kEnvParticleCount);

  for (int i = 0; i < kEnvParticleCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};
    t.scale = {0.1f, 0.1f, 0.1f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    // 初期は全てcube、Emit時にmeshを切り替える（リソースロード済み前提）
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isVisible = false;
    mr.isTransparent = true;

    m_envParticles[i].entity = e;
    m_envParticles[i].lifetime = 0.0f;
  }
  m_envWriteIndex = 0;
}

void GameJuiceSystem::CreateRippleEntities(core::GameContext &ctx) {
  m_ripples.clear();
  m_ripples.resize(kRippleCount);

  for (int i = 0; i < kRippleCount; ++i) {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};
    t.scale = {0.5f, 0.02f, 0.5f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isVisible = false;
    mr.isTransparent = true;
    mr.color = {0.6f, 0.8f, 1.2f, 0.0f};

    m_ripples[i].entity = e;
    m_ripples[i].lifetime = 0.0f;
  }
  m_rippleWriteIndex = 0;
}

void GameJuiceSystem::EmitEnvironmentParticles(core::GameContext &ctx,
                                               ecs::Entity targetEntity) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state || !state->isBallGrounded || state->currentBallSpeed < 0.5f) {
    return;
  }

  m_envEmitTimer += ctx.dt;
  // 速度が速いほどたくさん出す
  float interval =
      0.05f / (std::max<float>(1.0f, state->currentBallSpeed * 0.2f));

  if (m_envEmitTimer >= interval) {
    m_envEmitTimer = 0.0f;

    auto *targetT = ctx.world.Get<Transform>(targetEntity);
    if (!targetT)
      return;

    // 放出量
    int count = (state->currentMaterial == TerrainMaterial::Bunker) ? 3 : 1;

    for (int k = 0; k < count; ++k) {
      auto &p = m_envParticles[m_envWriteIndex];
      m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

      p.lifetime = 0.8f + ((float)(rand() % 100) / 100.0f) * 0.4f;
      p.maxLifetime = p.lifetime;

      auto *t = ctx.world.Get<Transform>(p.entity);
      auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

      if (t && mr) {
        t->position = targetT->position;
        // 地面にめり込まないよう少し上げる
        t->position.y += 0.05f;

        mr->isVisible = true;

        // マテリアル別の設定
        switch (state->currentMaterial) {
        case TerrainMaterial::Bunker: {
          // 砂煙: 球体、薄茶色、上昇・拡散
          p.isDust = true;
          mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr->color = {0.85f, 0.75f, 0.55f, 0.6f}; // サンドベージュ
          mr->customFlags.x = 1.0f;                // isDustフラグをシェーダーへ
          p.baseScale = 0.18f + ((float)(rand() % 100) / 100.0f) * 0.14f;
          t->scale = {p.baseScale, p.baseScale, p.baseScale};
          p.lifetime *= 1.4f;
          p.maxLifetime = p.lifetime;

          // 進行方向の逆にふわっと広がる
          auto *rb = ctx.world.Get<RigidBody>(targetEntity);
          XMVECTOR v = rb ? XMLoadFloat3(&rb->velocity) : XMVectorZero();
          v = XMVectorScale(v, -0.3f); // 速度の3割で逆走

          float spread = 0.5f;
          XMStoreFloat3(&p.velocity, v);
          p.velocity.x += ((float)(rand() % 100) / 100.0f - 0.5f) * spread;
          p.velocity.y +=
              0.5f + ((float)(rand() % 100) / 100.0f) * 0.5f; // 少し浮く
          p.velocity.z += ((float)(rand() % 100) / 100.0f - 0.5f) * spread;
          p.velocity.x *= 0.65f;
          p.velocity.z *= 0.65f;
          p.velocity.y *= 0.9f;

          p.angularVelocity = {0, 0, 0};
          break;
        }
        case TerrainMaterial::Rough:
        case TerrainMaterial::Fairway: {
          // 芝片: 立方体(薄く)、緑、弾ける、回転
          p.isDust = false;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->customFlags.x = 0.0f; // 芝は通常の四角
          if (state->currentMaterial == TerrainMaterial::Rough) {
            mr->color = {0.1f, 0.4f, 0.1f, 1.0f}; // 深緑
          } else {
            mr->color = {0.3f, 0.7f, 0.2f, 1.0f}; // 明るい緑
          }
          p.baseScale = 0.05f + ((float)(rand() % 100) / 100.0f) * 0.05f;
          float sizeJitter =
              0.6f + ((float)(rand() % 100) / 100.0f) * 0.8f;
          p.baseScale *= sizeJitter;
          // 板状にする
          t->scale = {p.baseScale * 2.0f, p.baseScale * 0.2f,
                      p.baseScale * 1.5f};

          // 四方に弾ける
          float angle = ((float)(rand() % 100) / 100.0f) * XM_2PI;
          float speed = 2.5f + ((float)(rand() % 100) / 100.0f) * 3.5f;
          p.velocity.x = std::cos(angle) * speed;
          p.velocity.y = 1.0f + ((float)(rand() % 100) / 100.0f) * 2.0f;
          p.velocity.z = std::sin(angle) * speed;

          float angScale = 35.0f;
          p.angularVelocity.x =
              ((float)(rand() % 100) / 100.0f - 0.5f) * angScale;
          p.angularVelocity.y =
              ((float)(rand() % 100) / 100.0f - 0.5f) * angScale;
          p.angularVelocity.z =
              ((float)(rand() % 100) / 100.0f - 0.5f) * angScale;
          break;
        }
        default:
          mr->isVisible = false;
          p.lifetime = 0;
          break;
        }
      }
    }
  }
}

void GameJuiceSystem::UpdateEnvironmentParticles(core::GameContext &ctx,
                                                 ecs::Entity targetEntity) {
  const float gravity = 9.8f;
  const float airResistance = 1.5f;

  for (auto &p : m_envParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= ctx.dt;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      if (p.isDust) {
        // 砂煙: 重力少なめ、空気抵抗強め、拡大
        p.velocity.x -= p.velocity.x * airResistance * ctx.dt;
        p.velocity.y +=
            (0.2f - p.velocity.y) * airResistance * ctx.dt; // わずかに上昇
        p.velocity.z -= p.velocity.z * airResistance * ctx.dt;

        t->position.x += p.velocity.x * ctx.dt;
        t->position.y += p.velocity.y * ctx.dt;
        t->position.z += p.velocity.z * ctx.dt;

        // 拡大フェード
        float progress = 1.0f - (p.lifetime / p.maxLifetime);
        float scale = p.baseScale * (1.0f + progress * 3.0f);
        t->scale = {scale, scale, scale};
      } else {
        // 芝片: 物理、回転
        p.velocity.y -= gravity * ctx.dt;

        t->position.x += p.velocity.x * ctx.dt;
        t->position.y += p.velocity.y * ctx.dt;
        t->position.z += p.velocity.z * ctx.dt;

        // 回転更新
        XMVECTOR rot = XMLoadFloat4(&t->rotation);
        XMVECTOR angVel = XMLoadFloat3(&p.angularVelocity);
        XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(
            angVel.m128_f32[0] * ctx.dt, angVel.m128_f32[1] * ctx.dt,
            angVel.m128_f32[2] * ctx.dt);
        rot = XMQuaternionMultiply(rot, deltaRot);
        XMStoreFloat4(&t->rotation, rot);

        // 地面で停止/消滅
        if (t->position.y < 0.0f) {
          t->position.y = 0.0f;
          p.lifetime *= 0.5f; // 地面に付いたらすぐ消える
          p.velocity = {0, 0, 0};
        }
      }
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      mr->color.w = lifeRatio; // アルファ減衰

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }

}

void GameJuiceSystem::TriggerConfetti(core::GameContext &ctx,
                                      const DirectX::XMFLOAT3 &position,
                                      float burstPower) {
  static const DirectX::XMFLOAT3 palette[] = {
      {1.2f, 0.3f, 0.35f}, {0.3f, 1.1f, 0.5f}, {0.35f, 0.6f, 1.2f},
      {1.0f, 0.9f, 0.35f}, {1.1f, 0.45f, 1.1f}};

  int count = std::min(kImpactParticleCount, 60);
  for (int i = 0; i < count; ++i) {
    auto &p = m_impactParticles[i];
    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (!t || !mr)
      continue;

    float angle = ((float)(rand() % 100) / 100.0f) * XM_2PI;
    float speed = (2.5f + burstPower * 3.0f) *
                  (0.6f + ((float)(rand() % 100) / 100.0f) * 0.6f);
    float up = 2.0f + burstPower * 2.0f;
    p.velocity.x = std::cos(angle) * speed;
    p.velocity.z = std::sin(angle) * speed;
    p.velocity.y = up * (0.7f + ((float)(rand() % 100) / 100.0f) * 0.6f);

    p.maxLifetime = 0.9f + ((float)(rand() % 100) / 100.0f) * 0.4f;
    p.lifetime = p.maxLifetime;

    t->position = position;
    t->position.y += 0.2f;

    float scale = 0.08f + burstPower * 0.05f;
    t->scale = {scale, scale * 0.4f, scale};
    p.baseScale = scale;

    DirectX::XMFLOAT3 tint = palette[i % (sizeof(palette) / sizeof(palette[0]))];
    p.baseColor = {tint.x, tint.y, tint.z, 1.0f};
    mr->color = p.baseColor;
    mr->isTransparent = true;
    mr->isVisible = true;
  }
}

void GameJuiceSystem::TriggerRippleEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float baseRadius, float strength) {
  if (m_ripples.empty())
    return;

  auto &r = m_ripples[m_rippleWriteIndex];
  m_rippleWriteIndex = (m_rippleWriteIndex + 1) % kRippleCount;

  r.maxLifetime = 0.45f + strength * 0.4f;
  r.lifetime = r.maxLifetime;
  r.startScale = std::max(baseRadius, 0.1f);

  if (auto *t = ctx.world.Get<Transform>(r.entity)) {
    t->position = position;
    t->position.y += 0.02f; // 地面より少し上
    t->scale = {r.startScale, 0.02f, r.startScale};
  }

  if (auto *mr = ctx.world.Get<MeshRenderer>(r.entity)) {
    mr->isVisible = true;
    mr->color.w = 0.6f;
  }
}

void GameJuiceSystem::UpdateRipples(core::GameContext &ctx) {
  for (auto &r : m_ripples) {
    if (r.lifetime <= 0.0f)
      continue;

    r.lifetime -= ctx.dt;
    float progress = 1.0f - (r.lifetime / r.maxLifetime);
    float ease = std::pow(progress, 0.85f);

    auto *t = ctx.world.Get<Transform>(r.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(r.entity);
    if (t && mr) {
      float scaleMul = 1.0f + ease * 3.0f;
      t->scale = {r.startScale * scaleMul, 0.02f,
                  r.startScale * scaleMul};
      mr->color.w = (1.0f - ease) * 0.6f;
      if (r.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

} // namespace game::systems
