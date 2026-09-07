/**
 * @file GameJuiceSystemEnvironment.cpp
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

void GameJuiceSystem::TriggerMaterialEffect(
    core::GameContext &ctx, const DirectX::XMFLOAT3 &position,
    game::components::TerrainMaterial material, float strength) {
  const bool isBunker = material == TerrainMaterial::Bunker;
  int count = std::clamp(static_cast<int>(strength * 6.0f), 1, 6);
  if (isBunker) {
    count = std::clamp(34 + static_cast<int>(strength * 22.0f), 34, 56);
  }
  const auto particleShader = ctx.resource.LoadShader(
      "Particle", L"shaders/ParticleVS.hlsl", L"shaders/ParticlePS.hlsl");
  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");

  if (isBunker) {
    SpawnSandImprint(ctx, position, 0.17f + strength * 0.18f,
                     1.15f + strength * 0.80f,
                     {1.02f, 0.89f, 0.62f, 0.24f});
    TriggerCameraShake(0.035f + strength * 0.065f,
                       0.12f + strength * 0.12f);
  }

  for (int k = 0; k < count; ++k) {
    auto &p = m_envParticles[m_envWriteIndex];
    m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

    p.lifetime = 0.5f + Rand01() * 0.5f;
    p.maxLifetime = p.lifetime;
    p.groundHeight = position.y;

    auto *t = ctx.world.Get<Transform>(p.entity);
    auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

    if (t && mr) {
      t->position = position;
      if (isBunker) {
        t->position.y += 0.012f;
      } else {
        t->position.y += 0.01f;
      }

      mr->isVisible = true;
      mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};

      const float speed = strength * 5.0f * (0.8f + Rand01() * 0.4f);
      const float angle = Rand01() * XM_2PI;
      const float upBias = 0.5f + Rand01() * 0.5f;

      p.velocity.x = std::cos(angle) * speed * 0.5f;
      p.velocity.y = speed * upBias;
      p.velocity.z = std::sin(angle) * speed * 0.5f;

      // 個別パラメータ
      switch (material) {
      case TerrainMaterial::Bunker: {
        const bool makeDust = (k % 4) == 0;
        const bool makeClump = !makeDust && (k % 5) == 0;
        const float radialSpeed =
            (1.2f + strength * 4.8f) * (0.55f + Rand01() * 0.75f);

        if (makeDust) {
          p.kind = EnvironmentParticleKind::SandDust;
          mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr->shader = particleShader;
          mr->isTransparent = true;
          mr->blendMode = BlendMode::Alpha;
          float firstParticleFlag = 0.0f;
          if (k == 0) {
            firstParticleFlag = 1.0f;
          }
          mr->customFlags = {1.0f, firstParticleFlag, 0.0f, 0.0f};
          const float warmth = Rand01() * 0.10f;
          p.baseColor = {1.05f + warmth, 0.82f + warmth * 0.7f,
                         0.45f + warmth * 0.35f, 0.93f};
          p.baseScale = 0.18f + strength * 0.22f + Rand01() * 0.13f;
          p.lifetime = 1.0f + strength * 0.55f + Rand01() * 0.55f;
          p.velocity = {std::cos(angle) * radialSpeed * 0.28f,
                        0.45f + strength * 1.05f + Rand01() * 0.65f,
                        std::sin(angle) * radialSpeed * 0.28f};
          p.angularVelocity = {0, 0, 0};
          t->scale = {p.baseScale, p.baseScale * 0.72f, p.baseScale};
        } else if (makeClump) {
          p.kind = EnvironmentParticleKind::SandClump;
          mr->mesh = ctx.resource.LoadMesh("builtin/rock");
          mr->shader = basicShader;
          mr->isTransparent = false;
          mr->blendMode = BlendMode::Opaque;
          const float shade = Rand01() * 0.16f;
          p.baseColor = {0.84f + shade, 0.63f + shade * 0.75f,
                         0.34f + shade * 0.35f, 1.0f};
          p.baseScale = 0.040f + strength * 0.050f + Rand01() * 0.035f;
          p.lifetime = 0.72f + Rand01() * 0.48f;
          p.velocity = {std::cos(angle) * radialSpeed * 0.72f,
                        1.4f + strength * 3.0f + Rand01() * 1.2f,
                        std::sin(angle) * radialSpeed * 0.72f};
          p.angularVelocity = {RandCentered() * 24.0f,
                               RandCentered() * 30.0f,
                               RandCentered() * 24.0f};
          t->scale = {p.baseScale * 1.35f, p.baseScale * 0.75f,
                      p.baseScale};
        } else {
          p.kind = EnvironmentParticleKind::SandGrain;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = basicShader;
          mr->isTransparent = false;
          mr->blendMode = BlendMode::Opaque;
          const float brightness = Rand01() * 0.18f;
          p.baseColor = {0.94f + brightness, 0.72f + brightness * 0.72f,
                         0.39f + brightness * 0.35f, 1.0f};
          p.baseScale = 0.014f + strength * 0.018f + Rand01() * 0.018f;
          p.lifetime = 0.58f + Rand01() * 0.44f;
          p.velocity = {std::cos(angle) * radialSpeed,
                        1.0f + strength * 3.8f + Rand01() * 1.8f,
                        std::sin(angle) * radialSpeed};
          p.angularVelocity = {RandCentered() * 40.0f,
                               RandCentered() * 48.0f,
                               RandCentered() * 40.0f};
          t->scale = {p.baseScale * (0.65f + Rand01()),
                      p.baseScale * (0.45f + Rand01() * 0.8f),
                      p.baseScale * (0.65f + Rand01())};
        }
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::Rough:
      case TerrainMaterial::Fairway:
      case TerrainMaterial::Green: {
        // 芝・葉っぱ
        p.kind = EnvironmentParticleKind::GrassClip;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube");
        mr->shader = particleShader;
        mr->isTransparent = true;
        mr->blendMode = BlendMode::Alpha;
        mr->customFlags.x = 0.0f;
        mr->customFlags.y = 0.0f;

        if (material == game::components::TerrainMaterial::Rough) {
          p.baseColor = {0.12f, 0.38f, 0.10f, 1.0f}; // 濃い緑
          float sizeJitter = 0.7f + ((float)(rand() % 100) / 100.0f) * 0.6f;
          p.baseScale = (0.1f + strength * 0.15f) * sizeJitter;
        } else if (material == game::components::TerrainMaterial::Green) {
          p.baseColor = {0.18f, 0.78f, 0.28f, 1.0f}; // 鮮やか
          p.baseScale = 0.05f + strength * 0.05f; // 小さい
        } else {
          p.baseColor = {0.25f, 0.58f, 0.18f, 1.0f}; // 普通
          p.baseScale = 0.08f + strength * 0.1f;
        }
        mr->color = p.baseColor;

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
        p.kind = EnvironmentParticleKind::GenericDebris;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube"); // 岩片
        mr->shader = basicShader;
        mr->isTransparent = false;
        mr->blendMode = BlendMode::Opaque;
        p.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
        mr->color = p.baseColor;
        mr->customFlags.x = 0.0f;
        mr->customFlags.y = 0.0f;
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
    auto e = m_entityOwner.Create(ctx.world);

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

void GameJuiceSystem::EmitEnvironmentParticles(core::GameContext &ctx,
                                               ecs::Entity targetEntity) {
  auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!state || !state->isBallGrounded || state->currentBallSpeed < 0.5f) {
    return;
  }

  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);
  m_envEmitTimer += effectDt;
  // 速度が速いほどたくさん出す
  const float speed01 =
      std::clamp(state->currentBallSpeed / 28.0f, 0.0f, 1.0f);
  float interval =
      0.07f / (std::max<float>(1.0f, state->currentBallSpeed * 0.2f));

  if (m_envEmitTimer >= interval) {
    m_envEmitTimer = 0.0f;

    auto *targetT = ctx.world.Get<Transform>(targetEntity);
    if (!targetT)
      return;

    // 放出量
    int count = 1;
    if (state->currentMaterial == TerrainMaterial::Bunker) {
      count = 6 + static_cast<int>(speed01 * 8.0f);
    } else if (state->currentMaterial == TerrainMaterial::Rough ||
               state->currentMaterial == TerrainMaterial::Fairway) {
      count = 1 + static_cast<int>(speed01 * 1.8f);
    } else if (state->currentMaterial == TerrainMaterial::Green) {
      if (state->currentBallSpeed > 2.5f) {
        count = 1;
      } else {
        count = 0;
      }
    }

    for (int k = 0; k < count; ++k) {
      auto &p = m_envParticles[m_envWriteIndex];
      m_envWriteIndex = (m_envWriteIndex + 1) % kEnvParticleCount;

      p.lifetime = 0.75f + Rand01() * 0.45f;
      p.maxLifetime = p.lifetime;

      auto *t = ctx.world.Get<Transform>(p.entity);
      auto *mr = ctx.world.Get<MeshRenderer>(p.entity);

      if (t && mr) {
        auto *collider = ctx.world.Get<Collider>(targetEntity);
        auto *body = ctx.world.Get<RigidBody>(targetEntity);
        float sink = 0.0f;
        if (collider && state->currentMaterial == TerrainMaterial::Bunker) {
          float verticalImpact = 0.0f;
          if (body) {
            verticalImpact = std::max(0.0f, -body->velocity.y);
          }
          sink = ComputeSurfaceSinkDepth(
              TerrainMaterial::Bunker, verticalImpact,
              state->currentBallSpeed, collider->radius);
        }
        float radius = game::physics::kBallRadius;
        if (collider) {
          radius = collider->radius;
        }
        p.groundHeight = targetT->position.y - radius + sink;
        t->position = {targetT->position.x, p.groundHeight + 0.01f,
                       targetT->position.z};

        mr->isVisible = true;
        mr->isTransparent = true;
        mr->blendMode = BlendMode::Alpha;
        mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};

        // マテリアル別の設定
        switch (state->currentMaterial) {
        case TerrainMaterial::Bunker: {
          auto *rb = ctx.world.Get<RigidBody>(targetEntity);
          XMVECTOR v = XMVectorZero();
          if (rb) {
            v = XMLoadFloat3(&rb->velocity);
          }
          const bool makeDust = (k % 3) == 0;
          const bool makeClump = !makeDust && speed01 > 0.35f && (k % 5) == 0;
          if (makeDust) {
            p.kind = EnvironmentParticleKind::SandDust;
            mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
            mr->shader = ctx.resource.LoadShader(
                "Particle", L"shaders/ParticleVS.hlsl",
                L"shaders/ParticlePS.hlsl");
            mr->isTransparent = true;
            mr->blendMode = BlendMode::Alpha;
            mr->customFlags = {1.0f, 1.0f, 0.0f, 0.0f};
            const float warmth = Rand01() * 0.08f;
            p.baseColor = {1.02f + warmth, 0.81f + warmth * 0.7f,
                           0.46f + warmth * 0.4f, 0.92f};
            p.baseScale = 0.14f + Rand01() * 0.12f + speed01 * 0.10f;
            t->scale = {p.baseScale, p.baseScale * 0.7f, p.baseScale};
            p.lifetime = 0.72f + Rand01() * 0.45f + speed01 * 0.45f;
            v = XMVectorScale(v, -0.10f - speed01 * 0.10f);
            XMStoreFloat3(&p.velocity, v);
            p.velocity.x += RandCentered() * (0.5f + speed01 * 0.5f);
            p.velocity.y = 0.28f + Rand01() * 0.55f + speed01 * 0.35f;
            p.velocity.z += RandCentered() * (0.5f + speed01 * 0.5f);
            p.angularVelocity = {0, 0, 0};
          } else {
            p.kind = EnvironmentParticleKind::SandGrain;
            const char *meshName = "builtin/cube";
            if (makeClump) {
              p.kind = EnvironmentParticleKind::SandClump;
              meshName = "builtin/rock";
            }
            mr->mesh = ctx.resource.LoadMesh(meshName);
            mr->shader = ctx.resource.LoadShader(
                "Basic", L"Assets/shaders/BasicVS.hlsl",
                L"Assets/shaders/BasicPS.hlsl");
            mr->isTransparent = false;
            mr->blendMode = BlendMode::Opaque;
            mr->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
            p.baseColor = {1.02f, 0.79f, 0.44f, 1.0f};
            p.baseScale = 0.014f + speed01 * 0.016f;
            float scaleX = 0.7f;
            float scaleY = 0.5f;
            if (makeClump) {
              p.baseColor = {0.86f, 0.64f, 0.35f, 1.0f};
              p.baseScale = 0.040f + speed01 * 0.035f;
              scaleX = 1.4f;
              scaleY = 0.8f;
            }
            t->scale = {p.baseScale * scaleX, p.baseScale * scaleY,
                        p.baseScale};
            const float angle = Rand01() * XM_2PI;
            const float scatter = 0.8f + speed01 * 3.4f;
            p.velocity = {std::cos(angle) * scatter,
                          0.55f + Rand01() * 1.25f + speed01 * 1.4f,
                          std::sin(angle) * scatter};
            p.angularVelocity = {RandCentered() * 32.0f,
                                 RandCentered() * 40.0f,
                                 RandCentered() * 32.0f};
            p.lifetime = 0.42f + Rand01() * 0.38f;
          }
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
          break;
        }
        case TerrainMaterial::Rough:
        case TerrainMaterial::Fairway: {
          // 芝片: 立方体(薄く)、緑、弾ける、回転
          p.kind = EnvironmentParticleKind::GrassClip;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          mr->customFlags.x = 0.0f; // 芝は通常の四角
          mr->customFlags.y = 0.0f;
          if (state->currentMaterial == TerrainMaterial::Rough) {
            p.baseColor = {0.08f, 0.34f + Rand01() * 0.08f, 0.08f, 1.0f};
          } else {
            p.baseColor = {0.22f, 0.56f + Rand01() * 0.14f, 0.14f, 1.0f};
          }
          mr->color = p.baseColor;
          p.baseScale = 0.055f + Rand01() * 0.055f + speed01 * 0.025f;
          float sizeJitter = 0.65f + Rand01() * 0.75f;
          p.baseScale *= sizeJitter;
          // 板状にする
          t->scale = {p.baseScale * (2.2f + Rand01() * 0.7f),
                      p.baseScale * 0.16f,
                      p.baseScale * (1.3f + Rand01() * 0.5f)};

          // 四方に弾ける
          float angle = Rand01() * XM_2PI;
          float speed = 2.2f + Rand01() * 3.0f + speed01 * 2.0f;
          p.velocity.x = std::cos(angle) * speed;
          p.velocity.y = 0.8f + Rand01() * 1.6f + speed01 * 0.6f;
          p.velocity.z = std::sin(angle) * speed;

          float angScale = 38.0f + speed01 * 18.0f;
          p.angularVelocity.x = RandCentered() * angScale;
          p.angularVelocity.y = RandCentered() * angScale;
          p.angularVelocity.z = RandCentered() * angScale;
          break;
        }
        case TerrainMaterial::Green: {
          // グリーン: 細かい削れ粉だけを控えめに出す
          p.kind = EnvironmentParticleKind::GrassClip;
          mr->mesh = ctx.resource.LoadMesh("builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          p.baseColor = {0.28f, 0.72f, 0.26f, 0.95f};
          mr->color = p.baseColor;
          mr->customFlags.x = 0.0f;
          mr->customFlags.y = 0.0f;
          p.baseScale = 0.035f + Rand01() * 0.025f;
          t->scale = {p.baseScale * 2.0f, p.baseScale * 0.12f,
                      p.baseScale * 1.4f};

          float angle = Rand01() * XM_2PI;
          float speed = 1.2f + Rand01() * 1.5f + speed01;
          p.velocity.x = std::cos(angle) * speed;
          p.velocity.y = 0.35f + Rand01() * 0.55f;
          p.velocity.z = std::sin(angle) * speed;

          float angScale = 24.0f;
          p.angularVelocity.x = RandCentered() * angScale;
          p.angularVelocity.y = RandCentered() * angScale;
          p.angularVelocity.z = RandCentered() * angScale;
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
} // namespace game::systems
