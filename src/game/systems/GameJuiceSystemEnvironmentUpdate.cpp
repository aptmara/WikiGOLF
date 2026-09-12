/**
 * @file GameJuiceSystemEnvironmentUpdate.cpp
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

void GameJuiceSystem::UpdateEnvironmentParticles(core::GameContext &ctx,
                                                 ecs::Entity targetEntity) {
  const float gravity = 9.8f;
  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);

  for (auto &p : m_envParticles) {
    if (p.lifetime <= 0.0f)
      continue;

    p.lifetime -= effectDt;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t) {
      const bool isSoftCloud =
          p.kind == EnvironmentParticleKind::SandDust ||
          p.kind == EnvironmentParticleKind::IceMist ||
          p.kind == EnvironmentParticleKind::StoneDust ||
          p.kind == EnvironmentParticleKind::WaterMist ||
          p.kind == EnvironmentParticleKind::LavaSmoke;
      if (isSoftCloud) {
        float dragStrength = 1.9f;
        float targetRise = 0.16f;
        float expansion = 2.8f;
        if (p.kind == EnvironmentParticleKind::IceMist) {
          dragStrength = 2.8f;
          targetRise = 0.08f;
          expansion = 2.1f;
        } else if (p.kind == EnvironmentParticleKind::StoneDust) {
          dragStrength = 2.5f;
          targetRise = 0.04f;
          expansion = 2.4f;
        } else if (p.kind == EnvironmentParticleKind::WaterMist) {
          dragStrength = 3.2f;
          targetRise = 0.02f;
          expansion = 1.7f;
        } else if (p.kind == EnvironmentParticleKind::LavaSmoke) {
          dragStrength = 1.25f;
          targetRise = 0.72f;
          expansion = 3.4f;
        }
        const float drag = std::exp(-dragStrength * effectDt);
        p.velocity.x *= drag;
        p.velocity.y += (targetRise - p.velocity.y) * 1.35f * effectDt;
        p.velocity.z *= drag;

        t->position.x += p.velocity.x * effectDt;
        t->position.y += p.velocity.y * effectDt;
        t->position.z += p.velocity.z * effectDt;

        float progress = 1.0f - (p.lifetime / p.maxLifetime);
        float scale = p.baseScale * (1.0f + progress * expansion);
        t->scale = {scale * (1.0f + progress * 0.45f),
                    scale * (0.62f + progress * 0.32f), scale};
      } else {
        float particleGravity = gravity;
        float drag = 0.18f;
        if (p.kind == EnvironmentParticleKind::SandGrain) {
          particleGravity = 13.5f;
          drag = 0.35f;
        } else if (p.kind == EnvironmentParticleKind::SandClump) {
          particleGravity = 11.5f;
          drag = 0.55f;
        } else if (p.kind == EnvironmentParticleKind::IceShard) {
          particleGravity = 12.5f;
          drag = 0.12f;
        } else if (p.kind == EnvironmentParticleKind::StoneChip) {
          particleGravity = 15.5f;
          drag = 0.42f;
        } else if (p.kind == EnvironmentParticleKind::WaterDrop) {
          particleGravity = 14.5f;
          drag = 0.08f;
        } else if (p.kind == EnvironmentParticleKind::Ember) {
          particleGravity = 3.2f;
          drag = 0.48f;
        }
        const float dragRatio = std::exp(-drag * effectDt);
        p.velocity.x *= dragRatio;
        p.velocity.y -= particleGravity * effectDt;
        p.velocity.z *= dragRatio;

        t->position.x += p.velocity.x * effectDt;
        t->position.y += p.velocity.y * effectDt;
        t->position.z += p.velocity.z * effectDt;

        // 回転更新
        XMVECTOR rot = XMLoadFloat4(&t->rotation);
        XMVECTOR angVel = XMLoadFloat3(&p.angularVelocity);
        XMVECTOR deltaRot = XMQuaternionRotationRollPitchYaw(
            angVel.m128_f32[0] * effectDt, angVel.m128_f32[1] * effectDt,
            angVel.m128_f32[2] * effectDt);
        rot = XMQuaternionMultiply(rot, deltaRot);
        XMStoreFloat4(&t->rotation, rot);

        if (t->position.y < p.groundHeight) {
          t->position.y = p.groundHeight + 0.002f;
          if ((p.kind == EnvironmentParticleKind::SandGrain ||
               p.kind == EnvironmentParticleKind::SandClump ||
               p.kind == EnvironmentParticleKind::IceShard ||
               p.kind == EnvironmentParticleKind::StoneChip) &&
              std::abs(p.velocity.y) > 0.16f) {
            float rebound = 0.16f;
            if (p.kind == EnvironmentParticleKind::IceShard) {
              rebound = 0.34f;
            } else if (p.kind == EnvironmentParticleKind::StoneChip) {
              rebound = 0.22f;
            }
            p.velocity.y = -p.velocity.y * rebound;
            p.velocity.x *= 0.48f + rebound * 0.55f;
            p.velocity.z *= 0.48f + rebound * 0.55f;
          } else if (p.kind == EnvironmentParticleKind::WaterDrop) {
            p.velocity = {0, 0, 0};
            t->scale.x *= 1.7f;
            t->scale.y *= 0.18f;
            t->scale.z *= 1.7f;
            p.lifetime = std::min(p.lifetime, 0.13f);
          } else {
            p.velocity = {0, 0, 0};
            p.lifetime = std::min(p.lifetime, 0.22f);
          }
        }
      }
    }

    if (mr) {
      float lifeRatio = std::max(0.0f, p.lifetime / p.maxLifetime);
      mr->color = p.baseColor;
      if (p.kind == EnvironmentParticleKind::SandDust ||
          p.kind == EnvironmentParticleKind::IceMist ||
          p.kind == EnvironmentParticleKind::StoneDust ||
          p.kind == EnvironmentParticleKind::WaterMist ||
          p.kind == EnvironmentParticleKind::LavaSmoke) {
        float progress = 1.0f - lifeRatio;
        float fadeStart = 0.46f;
        if (p.kind == EnvironmentParticleKind::WaterMist) {
          fadeStart = 0.24f;
        } else if (p.kind == EnvironmentParticleKind::LavaSmoke) {
          fadeStart = 0.55f;
          mr->color.x *= 0.72f + lifeRatio * 0.28f;
          mr->color.y *= 0.62f + lifeRatio * 0.38f;
        }
        float alphaHold = 1.0f - SmoothFade(progress, fadeStart, 1.0f);
        mr->color.w = p.baseColor.w * std::clamp(alphaHold, 0.0f, 1.0f);
      } else if (p.kind == EnvironmentParticleKind::SandGrain ||
                 p.kind == EnvironmentParticleKind::SandClump ||
                 p.kind == EnvironmentParticleKind::StoneChip) {
        mr->color.w = p.baseColor.w * SmoothFade(lifeRatio, 0.0f, 0.20f);
      } else if (p.kind == EnvironmentParticleKind::IceShard) {
        const float glint = 0.78f + std::sin((1.0f - lifeRatio) * XM_2PI * 5.0f) * 0.22f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.45f) * glint;
      } else if (p.kind == EnvironmentParticleKind::WaterDrop) {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.72f);
      } else if (p.kind == EnvironmentParticleKind::Ember) {
        const float flicker = 0.70f + std::sin((1.0f - lifeRatio) * XM_2PI * 8.0f) * 0.30f;
        mr->color.x *= 0.82f + lifeRatio * 0.34f;
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.38f) * flicker;
      } else {
        mr->color.w = p.baseColor.w * std::pow(lifeRatio, 0.55f);
      }

      if (p.lifetime <= 0.0f) {
        mr->isVisible = false;
      }
    }
  }

}

} // namespace game::systems

