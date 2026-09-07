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
      if (p.kind == EnvironmentParticleKind::SandDust) {
        const float drag = std::exp(-1.9f * effectDt);
        p.velocity.x *= drag;
        p.velocity.y += (0.16f - p.velocity.y) * 1.35f * effectDt;
        p.velocity.z *= drag;

        t->position.x += p.velocity.x * effectDt;
        t->position.y += p.velocity.y * effectDt;
        t->position.z += p.velocity.z * effectDt;

        float progress = 1.0f - (p.lifetime / p.maxLifetime);
        float scale = p.baseScale * (1.0f + progress * 2.8f);
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
               p.kind == EnvironmentParticleKind::SandClump) &&
              std::abs(p.velocity.y) > 0.16f) {
            p.velocity.y = -p.velocity.y * 0.16f;
            p.velocity.x *= 0.48f;
            p.velocity.z *= 0.48f;
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
      if (p.kind == EnvironmentParticleKind::SandDust) {
        float progress = 1.0f - lifeRatio;
        float alphaHold = 1.0f - SmoothFade(progress, 0.46f, 1.0f);
        mr->color.w = p.baseColor.w * std::clamp(alphaHold, 0.0f, 1.0f);
      } else if (p.kind == EnvironmentParticleKind::SandGrain ||
                 p.kind == EnvironmentParticleKind::SandClump) {
        mr->color.w = p.baseColor.w * SmoothFade(lifeRatio, 0.0f, 0.20f);
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

