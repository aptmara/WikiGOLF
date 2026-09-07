/**
 * @file GameJuiceSystemImpact.cpp
 * @brief Game Juiceの責務別実装です。
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
#include <windows.h>

#undef min
#undef max

namespace game::systems {

using namespace DirectX;
using namespace game::components;
using namespace juice_detail;

void GameJuiceSystem::CreateImpactParticleEntities(core::GameContext &ctx) {
  m_impactParticles.clear();
  m_impactParticles.reserve(kImpactParticleCount);

  for (int i = 0; i < kImpactParticleCount; ++i) {
    auto e = m_entityOwner.Create(ctx.world);

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
    mr.blendMode = BlendMode::Alpha;
    mr.customFlags = {0, 0, 0, 0};

    ImpactParticle particle;
    particle.entity = e;
    particle.lifetime = 0.0f;
    particle.kind = ImpactParticleKind::Burst;
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

  for (int i = 0; i < kImpactBurstCount; ++i) {
    auto &p = m_impactParticles[i];

    // === 多層構造の爆発エフェクト ===
    int layer = i % 4; // 4層構造
    float layerOffset = layer * 0.25f;
    float layerSpeed = baseSpeed * (1.0f - layerOffset * 0.3f);

    // 放射状に速度を設定（スパイラル風）
    float baseAngle = (float)i / (float)kImpactBurstCount * XM_2PI;
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
      mr->mesh = ctx.resource.LoadMesh("builtin/cube");
      mr->blendMode = BlendMode::Alpha;
      mr->customFlags = {0, 0, 0, 0};
      p.kind = ImpactParticleKind::Burst;
      p.angularVelocity = {0, 0, 0};

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

void GameJuiceSystem::TriggerShotEffect(core::GameContext &ctx,
                                        const DirectX::XMFLOAT3 &position,
                                        const DirectX::XMFLOAT3 &direction,
                                        float power, JudgeType judge) {
  XMVECTOR dirVec = XMLoadFloat3(&direction);
  if (XMVectorGetX(XMVector3LengthSq(dirVec)) < 0.0001f) {
    dirVec = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
  }
  dirVec = XMVector3Normalize(XMVectorSetY(dirVec, 0.0f));
  XMFLOAT3 dir;
  XMStoreFloat3(&dir, dirVec);

  XMVECTOR rightVec =
      XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), dirVec));
  XMFLOAT3 right;
  XMStoreFloat3(&right, rightVec);

  float quality = 0.85f;
  if (judge == JudgeType::Special) {
    quality = 1.55f;
    TriggerHitStop(0.07f, 0.0f);
    TriggerCameraShake(0.32f, 0.22f);
  } else if (judge == JudgeType::Great) {
    quality = 1.25f;
    TriggerHitStop(0.045f, 0.0f);
    TriggerCameraShake(0.22f, 0.18f);
  } else if (judge == JudgeType::Nice) {
    quality = 0.95f;
    TriggerCameraShake(0.14f, 0.14f);
  } else if (judge == JudgeType::Miss) {
    quality = 0.62f;
    TriggerCameraShake(0.18f, 0.16f);
  }

  const int count = std::min(kImpactBurstCount, 34);
  const float power01 = std::clamp(power, 0.0f, 1.0f);
  for (int i = 0; i < count; ++i) {
    auto &p = m_impactParticles[i];
    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (!t || !mr) {
      continue;
    }

    const int layer = i % 5;
    const float side = RandCentered();
    const float forward = 0.12f + Rand01() * 0.34f;
    t->position = position;
    t->position.x += right.x * side * 0.72f + dir.x * forward;
    t->position.y += 0.05f + Rand01() * 0.16f;
    t->position.z += right.z * side * 0.72f + dir.z * forward;

    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI);
    XMStoreFloat4(&t->rotation, rot);

    XMFLOAT3 tint = ShotJudgeColor(judge, i);
    const bool isMiss = judge == JudgeType::Miss;
    const bool isSpark =
        judge == JudgeType::Special || judge == JudgeType::Great;

    if (layer == 0 && isSpark) {
      p.kind = ImpactParticleKind::ShotRing;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.baseScale = 0.32f + power01 * 0.26f + quality * 0.08f;
      p.maxLifetime = 0.38f + quality * 0.12f;
      p.baseColor = ScaleColor(tint, 2.4f + quality * 0.8f, 0.92f);
      p.velocity = {dir.x * (2.0f + quality * 2.2f),
                    0.25f + quality * 0.42f,
                    dir.z * (2.0f + quality * 2.2f)};
      p.angularVelocity = {0.0f, 10.0f + Rand01() * 8.0f, 0.0f};
    } else if (layer <= 2 && !isMiss) {
      p.kind = ImpactParticleKind::ShotSpark;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.baseScale = 0.08f + power01 * 0.07f + Rand01() * 0.04f;
      p.maxLifetime = 0.42f + Rand01() * 0.22f + quality * 0.12f;
      p.baseColor = ScaleColor(tint, 2.0f + quality * 1.1f, 0.95f);
      float speed = 3.8f + power01 * 4.5f + quality * 2.2f;
      p.velocity = {dir.x * speed + right.x * side * 5.2f,
                    0.7f + Rand01() * 1.4f + quality * 0.35f,
                    dir.z * speed + right.z * side * 5.2f};
      p.angularVelocity = {RandCentered() * 8.0f, RandCentered() * 12.0f,
                           RandCentered() * 8.0f};
    } else {
      p.kind = ImpactParticleKind::ShotDust;
      mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
      mr->blendMode = BlendMode::Alpha;
      float missFlag = 0.0f;
      float baseScale = 0.14f;
      float maxLifetime = 0.52f;
      XMFLOAT4 baseColor{0.86f, 0.78f, 0.54f, 0.72f};
      if (isMiss) {
        missFlag = 1.0f;
        baseScale = 0.22f;
        maxLifetime = 0.75f;
        baseColor = {0.72f, 0.46f, 0.30f, 0.9f};
      }
      mr->customFlags = {1.0f, missFlag, 0.0f, 0.0f};
      p.baseScale = baseScale + power01 * 0.08f + Rand01() * 0.06f;
      p.maxLifetime = maxLifetime + Rand01() * 0.24f;
      p.baseColor = baseColor;
      float speed = 1.4f + power01 * 2.0f;
      p.velocity = {-dir.x * speed + right.x * side * 2.8f,
                    0.35f + Rand01() * 0.8f,
                    -dir.z * speed + right.z * side * 2.8f};
      p.angularVelocity = {RandCentered() * 4.0f, RandCentered() * 4.0f,
                           RandCentered() * 4.0f};
    }

    p.lifetime = p.maxLifetime;
    t->scale = {p.baseScale, p.baseScale, p.baseScale};
    mr->color = p.baseColor;
    mr->isTransparent = true;
    mr->isVisible = true;
  }
}

void GameJuiceSystem::UpdateImpactParticles(core::GameContext &ctx) {
  const float gravity = 15.0f;
  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);

  for (auto &p : m_impactParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= effectDt;

    float particleGravity = gravity;
    float drag = 0.98f;
    if (p.kind == ImpactParticleKind::Star) {
      particleGravity = 4.0f;
      drag = 0.985f;
    } else if (p.kind == ImpactParticleKind::Sparkle) {
      particleGravity = 2.0f;
      drag = 0.975f;
    } else if (p.kind == ImpactParticleKind::Glint) {
      particleGravity = 0.0f;
      drag = 0.965f;
    } else if (p.kind == ImpactParticleKind::Confetti) {
      particleGravity = 7.5f;
      drag = 0.99f;
    } else if (p.kind == ImpactParticleKind::ShotSpark) {
      particleGravity = 6.0f;
      drag = 0.965f;
    } else if (p.kind == ImpactParticleKind::ShotDust) {
      particleGravity = 3.2f;
      drag = 0.94f;
    } else if (p.kind == ImpactParticleKind::ShotRing) {
      particleGravity = 0.0f;
      drag = 0.955f;
    }

    // 物理更新
    p.velocity.y -= particleGravity * effectDt;
    p.velocity.x *= drag; // 空気抵抗
    p.velocity.z *= drag;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      t->position.x += p.velocity.x * effectDt;
      t->position.y += p.velocity.y * effectDt;
      t->position.z += p.velocity.z * effectDt;

      // 縮小しながらフェードアウト
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      float progress = 1.0f - lifeRatio;
      if (p.kind == ImpactParticleKind::Star) {
        float pulse = 0.86f + std::sin(progress * XM_2PI * 3.0f) * 0.14f;
        float scale = p.baseScale * pulse * (0.35f + lifeRatio * 0.85f);
        t->scale = {scale, scale, scale * 0.18f};
      } else if (p.kind == ImpactParticleKind::Sparkle) {
        float scale = p.baseScale * (0.15f + lifeRatio * 1.15f);
        t->scale = {scale * 0.35f, scale * 2.2f, scale * 0.35f};
      } else if (p.kind == ImpactParticleKind::Glint) {
        float pulse = std::sin(std::clamp(progress * 1.4f, 0.0f, 1.0f) * XM_PI);
        float scale = p.baseScale * (0.4f + pulse * 1.25f);
        t->scale = {scale * 2.8f, scale * 0.18f, scale * 2.8f};
      } else if (p.kind == ImpactParticleKind::Confetti) {
        float scale = p.baseScale * std::pow(lifeRatio, 0.35f);
        t->scale = {scale * 1.55f, scale * 0.22f, scale * 0.95f};
      } else if (p.kind == ImpactParticleKind::ShotSpark) {
        float scale = p.baseScale * (0.25f + lifeRatio * 1.05f);
        t->scale = {scale * 0.32f, scale * 1.9f, scale * 0.32f};
      } else if (p.kind == ImpactParticleKind::ShotDust) {
        float scale = p.baseScale * (1.0f + progress * 2.35f);
        t->scale = {scale * 1.2f, scale * 0.5f, scale * 1.2f};
      } else if (p.kind == ImpactParticleKind::ShotRing) {
        float pulse = std::sin(std::clamp(progress * 1.35f, 0.0f, 1.0f) * XM_PI);
        float scale = p.baseScale * (0.45f + progress * 3.2f + pulse * 0.45f);
        t->scale = {scale * 2.2f, scale * 0.08f, scale * 2.2f};
      } else {
        float scale = p.baseScale * std::pow(lifeRatio, 0.5f);
        t->scale = {scale, scale, scale};
      }

      XMVECTOR rot = XMLoadFloat4(&t->rotation);
      XMVECTOR angVel = XMLoadFloat3(&p.angularVelocity);
      XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(
          angVel.m128_f32[0] * effectDt, angVel.m128_f32[1] * effectDt,
          angVel.m128_f32[2] * effectDt);
      rot = XMQuaternionMultiply(rot, deltaRot);
      XMStoreFloat4(&t->rotation, rot);
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      float progress = 1.0f - lifeRatio;
      mr->color = p.baseColor;
      if (p.kind == ImpactParticleKind::Star) {
        float twinkle = 0.78f + std::sin(progress * XM_2PI * 5.0f) * 0.22f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.45f) * twinkle;
      } else if (p.kind == ImpactParticleKind::Sparkle) {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.85f);
      } else if (p.kind == ImpactParticleKind::Glint) {
        float pulse = std::sin(std::clamp(progress * 1.4f, 0.0f, 1.0f) * XM_PI);
        mr->color.w = p.baseColor.w * pulse * std::pow(lifeRatio, 0.35f);
      } else if (p.kind == ImpactParticleKind::Confetti) {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.7f);
      } else if (p.kind == ImpactParticleKind::ShotSpark) {
        float twinkle = 0.78f + std::sin(progress * XM_2PI * 6.0f) * 0.22f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.72f) * twinkle;
      } else if (p.kind == ImpactParticleKind::ShotDust) {
        float hold = 1.0f - SmoothFade(progress, 0.5f, 1.0f);
        mr->color.w = p.baseColor.w * std::clamp(hold, 0.0f, 1.0f);
      } else if (p.kind == ImpactParticleKind::ShotRing) {
        float pulse = std::sin(std::clamp(progress * 1.35f, 0.0f, 1.0f) * XM_PI);
        mr->color.w = p.baseColor.w * pulse * std::pow(lifeRatio, 0.45f);
      } else {
        mr->color.w = lifeRatio; // アルファ減衰
      }

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

// =============================================================================
// マテリアルエフェクト
// =============================================================================

} // namespace game::systems
