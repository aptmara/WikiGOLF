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
    t.position = {0, -100, 0};    // 画面外
    t.scale = {0.1f, 0.1f, 0.1f}; // 大きめ

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/sphere");
    mr.shader = ctx.resource.LoadShader("Basic", L"shaders/BasicVS.hlsl",
                                        L"shaders/BasicPS.hlsl");

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
                                          float power, JudgeType judge) {
  LOG_DEBUG("GameJuice",
            "Impact effect triggered at ({:.2f}, {:.2f}, {:.2f}) power={:.2f} "
            "judge={}",
            position.x, position.y, position.z, power, (int)judge);

  // 判定によって派手さを調整
  float speedMultiplier = 1.0f;
  float sizeMultiplier = 1.0f;
  float lifetimeMultiplier = 1.0f;

  switch (judge) {
  case JudgeType::Great:
    speedMultiplier = 1.5f;    // より高速に飛び散る
    sizeMultiplier = 1.5f;     // 大きめ
    lifetimeMultiplier = 1.3f; // 長寿命
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
          // 中心は白く
          if (layer == 0) {
            r = 1.0f;
            g = 1.0f;
            b = 0.9f;
          }
        }
        break;

      case JudgeType::Nice:
        // 青白い爽やかな爆発
        {
          float blueShift = (float)(rand() % 100) / 100.0f * 0.2f;
          r = 0.4f + blueShift;
          g = 0.7f + blueShift;
          b = 1.0f;
          if (layer == 0) {
            r = 0.8f;
            g = 0.95f;
            b = 1.0f;
          }
        }
        break;

      case JudgeType::Miss:
        // 赤～オレンジの残念な爆発
        {
          float redShift = (float)(rand() % 100) / 100.0f * 0.3f;
          r = 1.0f;
          g = 0.3f + redShift;
          b = 0.1f;
          if (layer == 0) {
            r = 1.0f;
            g = 0.5f;
            b = 0.3f;
          }
        }
        break;

      default:
        // 虹色（デフォルト）
        {
          float hue =
              (float)i / (float)kImpactParticleCount * 360.0f + power * 60.0f;
          hue = std::fmod(hue, 360.0f);
          float h = hue / 60.0f;
          int hi = (int)h % 6;
          float f = h - (int)h;
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
          if (layer == 0) {
            r = 0.5f + r * 0.5f;
            g = 0.5f + g * 0.5f;
            b = 0.5f + b * 0.5f;
          }
        }
        break;
      }

      // 発光感
      float brightness = 1.5f + (1.0f - layerOffset) * 0.8f;
      if (layer == 0) {
        brightness = 2.5f;
      }

      mr->color = {r * brightness, g * brightness, b * brightness, 1.0f};
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
