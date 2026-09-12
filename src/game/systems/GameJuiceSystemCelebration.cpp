/**
 * @file GameJuiceSystemCelebration.cpp
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

void GameJuiceSystem::TriggerConfetti(core::GameContext &ctx,
                                      const DirectX::XMFLOAT3 &position,
                                      float burstPower) {
  const int startIndex = kImpactBurstCount;
  const int count = kImpactParticleCount - startIndex;

  for (int i = 0; i < count; ++i) {
    auto &p = m_impactParticles[startIndex + i];
    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);
    if (!t || !mr)
      continue;

    float angle = Rand01() * XM_2PI;
    float ring = 0.18f + Rand01() * 0.42f;
    float radialSpeed = (2.0f + burstPower * 4.2f) * (0.7f + Rand01() * 0.65f);
    float up = 1.8f + burstPower * 3.2f;

    t->position = position;
    t->position.x += std::cos(angle) * ring;
    t->position.y += 0.25f + Rand01() * 0.45f;
    t->position.z += std::sin(angle) * ring;

    XMVECTOR rot = XMQuaternionRotationRollPitchYaw(Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI,
                                                    Rand01() * XM_2PI);
    XMStoreFloat4(&t->rotation, rot);

    p.velocity.x = std::cos(angle) * radialSpeed;
    p.velocity.z = std::sin(angle) * radialSpeed;
    p.velocity.y = up * (0.55f + Rand01() * 0.8f);
    p.angularVelocity = {RandCentered() * 12.0f, RandCentered() * 16.0f,
                         RandCentered() * 14.0f};

    const int layer = i % 6;
    if (layer == 0) {
      p.kind = ImpactParticleKind::Glint;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 0.45f + Rand01() * 0.18f;
      p.baseScale = 0.55f + burstPower * 0.28f + Rand01() * 0.18f;
      p.baseColor = {2.8f, 2.35f, 1.05f, 0.95f};
      p.velocity.x *= 0.35f;
      p.velocity.z *= 0.35f;
      p.velocity.y *= 0.25f;
    } else if (layer == 1 || layer == 2) {
      p.kind = ImpactParticleKind::Star;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 1.05f + Rand01() * 0.75f;
      p.baseScale = 0.18f + burstPower * 0.08f + Rand01() * 0.12f;
      p.baseColor = ScaleColor(CupInSparkleColor(i), 1.65f + Rand01() * 0.75f,
                               1.0f);
      p.velocity.x *= 0.72f;
      p.velocity.z *= 0.72f;
      p.velocity.y *= 0.78f;
    } else if (layer == 3 || layer == 4) {
      p.kind = ImpactParticleKind::Sparkle;
      mr->mesh = ctx.resource.LoadMesh("builtin/quad");
      mr->blendMode = BlendMode::Add;
      mr->customFlags = {0.0f, 1.0f, 0.0f, 0.0f};
      p.maxLifetime = 0.65f + Rand01() * 0.55f;
      p.baseScale = 0.08f + burstPower * 0.035f + Rand01() * 0.055f;
      p.baseColor = {2.4f, 2.1f, 0.85f, 0.9f};
      p.velocity.x *= 1.12f;
      p.velocity.z *= 1.12f;
      p.velocity.y *= 1.05f;
    } else {
      p.kind = ImpactParticleKind::Confetti;
      mr->mesh = ctx.resource.LoadMesh("builtin/cube");
      mr->blendMode = BlendMode::Alpha;
      mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
      p.maxLifetime = 1.1f + Rand01() * 0.55f;
      p.baseScale = 0.08f + burstPower * 0.05f + Rand01() * 0.04f;
      p.baseColor = ScaleColor(CupInSparkleColor(i + 3), 1.05f, 1.0f);
    }

    p.lifetime = p.maxLifetime;
    t->scale = {p.baseScale, p.baseScale, p.baseScale};
    mr->color = p.baseColor;
    mr->isTransparent = true;
    mr->isVisible = true;
  }
}

void GameJuiceSystem::TriggerRippleEffect(core::GameContext &ctx,
                                          const DirectX::XMFLOAT3 &position,
                                          float baseRadius, float strength,
                                          TerrainMaterial material) {
  if (m_ripples.empty())
    return;

  int ringCount = 1;
  XMFLOAT4 color = {0.52f, 0.92f, 0.42f, 0.48f};
  float lifetime = 0.44f + strength * 0.30f;
  float expansion = 2.4f;
  float thickness = 0.012f;
  switch (material) {
  case TerrainMaterial::Rough:
    color = {0.28f, 0.58f, 0.18f, 0.42f};
    lifetime = 0.52f + strength * 0.34f;
    expansion = 1.85f;
    thickness = 0.018f;
    break;
  case TerrainMaterial::Green:
    color = {0.62f, 1.08f, 0.48f, 0.40f};
    lifetime = 0.38f + strength * 0.24f;
    expansion = 2.8f;
    thickness = 0.008f;
    break;
  case TerrainMaterial::Ice:
    ringCount = 2;
    color = {0.56f, 1.24f, 1.62f, 0.72f};
    lifetime = 0.55f + strength * 0.38f;
    expansion = 3.4f;
    thickness = 0.007f;
    break;
  case TerrainMaterial::Stone:
    color = {0.72f, 0.68f, 0.62f, 0.48f};
    lifetime = 0.34f + strength * 0.22f;
    expansion = 1.55f;
    thickness = 0.024f;
    break;
  case TerrainMaterial::Water:
    ringCount = 3;
    color = {0.24f, 0.88f, 1.58f, 0.74f};
    lifetime = 0.68f + strength * 0.42f;
    expansion = 3.8f;
    thickness = 0.006f;
    break;
  case TerrainMaterial::Lava:
    ringCount = 2;
    color = {1.72f, 0.31f, 0.025f, 0.78f};
    lifetime = 0.62f + strength * 0.38f;
    expansion = 2.45f;
    thickness = 0.016f;
    break;
  default:
    break;
  }

  for (int i = 0; i < ringCount; ++i) {
    auto &r = m_ripples[m_rippleWriteIndex];
    m_rippleWriteIndex = (m_rippleWriteIndex + 1) % kRippleCount;
    const float layerRatio = static_cast<float>(i) /
                             static_cast<float>(std::max(ringCount, 1));
    r.maxLifetime = lifetime * (1.0f + layerRatio * 0.34f);
    r.lifetime = r.maxLifetime;
    r.startScale = std::max(baseRadius * (0.72f + layerRatio * 0.46f), 0.1f);
    r.expansion = expansion * (1.0f - layerRatio * 0.18f);
    r.thickness = thickness * (1.0f + layerRatio * 0.45f);
    r.baseColor = color;
    r.baseColor.w *= 1.0f - layerRatio * 0.34f;

    if (auto *t = ctx.world.Get<Transform>(r.entity)) {
      t->position = position;
      t->position.y += 0.003f + static_cast<float>(i) * 0.0015f;
      t->scale = {r.startScale, r.thickness, r.startScale};
    }

    if (auto *mr = ctx.world.Get<MeshRenderer>(r.entity)) {
      mr->isVisible = true;
      mr->color = r.baseColor;
      mr->customFlags = {5.0f, layerRatio, 0.0f, 0.0f};
    }
  }
}

void GameJuiceSystem::UpdateRipples(core::GameContext &ctx) {
  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);
  for (auto &r : m_ripples) {
    if (r.lifetime <= 0.0f)
      continue;

    r.lifetime -= effectDt;
    float progress = 1.0f - (r.lifetime / r.maxLifetime);
    float ease = std::pow(progress, 0.85f);

    auto *t = ctx.world.Get<Transform>(r.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(r.entity);
    if (t && mr) {
      float scaleMul = 1.0f + ease * r.expansion;
      t->scale = {r.startScale * scaleMul, r.thickness,
                  r.startScale * scaleMul};
      mr->color = r.baseColor;
      mr->color.w = r.baseColor.w * std::pow(1.0f - ease, 1.25f);
      if (r.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }
}

} // namespace game::systems

