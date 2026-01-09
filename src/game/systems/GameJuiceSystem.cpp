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
  m_currentFov += (m_targetFov - m_currentFov) * speed * ctx.dt;

  if (!ctx.world.IsAlive(cameraEntity))
    return;

  auto *cam = ctx.world.Get<Camera>(cameraEntity);
  if (cam) {
    cam->fov = XMConvertToRadians(m_currentFov);
  }
}

// =============================================================================
// トレイル
// =============================================================================

void GameJuiceSystem::CreateTrailEntities(core::GameContext &ctx) {
  m_trailEntities.clear();
  m_trailPositions.clear();
  m_trailPositions.resize(kTrailCount, {0, -100, 0}); // 画面外で初期化

  for (int i = 0; i < kTrailCount; ++i) {
    auto e = ctx.world.CreateEntity();

    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0}; // 画面外
    t.scale = {0.05f, 0.05f, 0.05f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");

    // グラデーション（先端が明るい）
    float ratio = (float)i / (float)(kTrailCount - 1);
    float alpha = 0.8f * (1.0f - ratio);
    mr.color = {1.0f, 0.9f - ratio * 0.5f, 0.3f, alpha}; // オレンジ→赤グラデ
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
      if (mr)
        mr->isVisible = false;
    }
    return;
  }

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
      float scale = 0.08f * (1.0f - ratio * 0.7f);
      t->scale = {scale, scale, scale};

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
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");
    mr.color = {1.0f, 0.8f, 0.2f, 1.0f}; // 黄金色
    mr.isVisible = false;

    ImpactParticle particle;
    particle.entity = e;
    particle.lifetime = 0.0f;
    m_impactParticles.push_back(particle);
  }
}

void GameJuiceSystem::TriggerImpactEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float power) {
  LOG_DEBUG("GameJuice", "Impact effect triggered at ({:.2f}, {:.2f}, {:.2f})",
            position.x, position.y, position.z);

  float baseSpeed = 5.0f + power * 8.0f;

  for (int i = 0; i < kImpactParticleCount; ++i) {
    auto &p = m_impactParticles[i];

    // 放射状に速度を設定
    float angle = (float)i / (float)kImpactParticleCount * XM_2PI;
    float upAngle = XM_PIDIV4 + ((float)(rand() % 100) / 100.0f) * XM_PIDIV4;

    p.velocity.x = std::cos(angle) * std::cos(upAngle) * baseSpeed;
    p.velocity.y = std::sin(upAngle) * baseSpeed * 0.8f;
    p.velocity.z = std::sin(angle) * std::cos(upAngle) * baseSpeed;

    // ランダムなばらつき
    p.velocity.x += ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;
    p.velocity.z += ((float)(rand() % 100) / 100.0f - 0.5f) * 2.0f;

    p.lifetime = 0.4f + ((float)(rand() % 100) / 100.0f) * 0.2f;

    // 初期位置設定
    auto *t = ctx.world.Get<Transform>(p.entity);
    if (t) {
      t->position = position;
      t->position.y += 0.1f; // ボールの上から発生
      t->scale = {0.12f, 0.12f, 0.12f};
    }

    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (mr) {
      mr->isVisible = true;
      // パワーに応じて色変化（弱:黄→強:赤）
      float colorRatio = std::clamp(power, 0.0f, 1.0f);
      mr->color = {1.0f, 0.9f - colorRatio * 0.6f, 0.2f - colorRatio * 0.2f,
                   1.0f};
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

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      t->position.x += p.velocity.x * ctx.dt;
      t->position.y += p.velocity.y * ctx.dt;
      t->position.z += p.velocity.z * ctx.dt;

      // 縮小しながらフェードアウト
      float lifeRatio = std::max(0.0f, p.lifetime / 0.5f);
      float scale = 0.12f * lifeRatio;
      t->scale = {scale, scale, scale};
    }

    if (mr) {
      // アルファフェード
      float lifeRatio = std::max(0.0f, p.lifetime / 0.5f);
      mr->color.w = lifeRatio;

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

} // namespace game::systems
