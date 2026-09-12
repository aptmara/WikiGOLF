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
  int count = 8 + static_cast<int>(strength * 12.0f);
  switch (material) {
  case TerrainMaterial::Bunker:
    count = 36 + static_cast<int>(strength * 24.0f);
    break;
  case TerrainMaterial::Rough:
    count = 16 + static_cast<int>(strength * 16.0f);
    break;
  case TerrainMaterial::Water:
    count = 24 + static_cast<int>(strength * 22.0f);
    break;
  case TerrainMaterial::Lava:
    count = 22 + static_cast<int>(strength * 20.0f);
    break;
  case TerrainMaterial::Ice:
  case TerrainMaterial::Stone:
    count = 18 + static_cast<int>(strength * 18.0f);
    break;
  case TerrainMaterial::None:
    count = 0;
    break;
  default:
    break;
  }
  count = std::clamp(count, 0, 60);
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
          const float leafTone = Rand01() * 0.10f;
          p.baseColor = {0.08f + leafTone * 0.25f, 0.28f + leafTone,
                         0.055f, 1.0f};
          p.baseScale = (0.075f + strength * 0.105f) *
                        (0.72f + Rand01() * 0.65f);
          p.lifetime = 0.72f + Rand01() * 0.48f;
        } else if (material == game::components::TerrainMaterial::Green) {
          p.baseColor = {0.22f, 0.72f + Rand01() * 0.12f, 0.19f, 0.88f};
          p.baseScale = 0.026f + strength * 0.038f;
          p.lifetime = 0.38f + Rand01() * 0.25f;
        } else {
          p.baseColor = {0.18f + Rand01() * 0.08f,
                         0.50f + Rand01() * 0.12f, 0.11f, 0.96f};
          p.baseScale = 0.048f + strength * 0.072f;
          p.lifetime = 0.50f + Rand01() * 0.34f;
        }
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;

        const float bladeLength = material == TerrainMaterial::Rough ? 2.8f : 1.8f;
        t->scale = {p.baseScale * bladeLength, p.baseScale * 0.10f,
                    p.baseScale * (0.72f + Rand01() * 0.52f)};

        float spinScale = 45.0f;
        p.angularVelocity.x =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.y =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        p.angularVelocity.z =
            ((float)(rand() % 100) / 100.0f - 0.5f) * spinScale;
        break;
      }

      case TerrainMaterial::Ice: {
        const bool mist = (k % 4) == 0;
        p.kind = mist ? EnvironmentParticleKind::IceMist
                      : EnvironmentParticleKind::IceShard;
        mr->mesh = ctx.resource.LoadMesh(mist ? "builtin/sphere" : "builtin/cube");
        mr->shader = particleShader;
        mr->isTransparent = true;
        mr->blendMode = mist ? BlendMode::Alpha : BlendMode::Add;
        mr->customFlags = {mist ? 1.0f : 2.0f, 0.0f, 0.0f, 0.0f};
        p.baseColor = mist ? XMFLOAT4{0.66f, 0.93f, 1.14f, 0.42f}
                           : XMFLOAT4{0.62f, 1.18f, 1.48f, 0.95f};
        p.baseScale = mist ? 0.12f + strength * 0.14f
                           : 0.025f + strength * 0.045f + Rand01() * 0.025f;
        p.lifetime = mist ? 0.72f + Rand01() * 0.38f
                          : 0.48f + Rand01() * 0.42f;
        const float radial = 1.2f + strength * 5.0f + Rand01() * 2.0f;
        p.velocity = {std::cos(angle) * radial,
                      mist ? 0.18f + Rand01() * 0.35f
                           : 1.5f + strength * 3.5f + Rand01() * 1.8f,
                      std::sin(angle) * radial};
        p.angularVelocity = {RandCentered() * 32.0f, RandCentered() * 38.0f,
                             RandCentered() * 32.0f};
        t->scale = mist ? XMFLOAT3{p.baseScale, p.baseScale * 0.48f, p.baseScale}
                        : XMFLOAT3{p.baseScale * 0.35f, p.baseScale * 2.8f,
                                   p.baseScale * 0.75f};
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::Stone: {
        const bool dust = (k % 5) == 0;
        p.kind = dust ? EnvironmentParticleKind::StoneDust
                      : EnvironmentParticleKind::StoneChip;
        mr->mesh = ctx.resource.LoadMesh(dust ? "builtin/sphere" : "builtin/rock");
        mr->shader = dust ? particleShader : basicShader;
        mr->isTransparent = dust;
        mr->blendMode = dust ? BlendMode::Alpha : BlendMode::Opaque;
        mr->customFlags = {dust ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
        const float shade = Rand01() * 0.16f;
        p.baseColor = dust ? XMFLOAT4{0.48f + shade, 0.45f + shade,
                                      0.41f + shade, 0.58f}
                           : XMFLOAT4{0.31f + shade, 0.30f + shade,
                                      0.29f + shade, 1.0f};
        p.baseScale = dust ? 0.11f + strength * 0.13f
                           : 0.035f + strength * 0.055f + Rand01() * 0.035f;
        p.lifetime = dust ? 0.70f + Rand01() * 0.38f
                          : 0.62f + Rand01() * 0.48f;
        const float radial = 1.0f + strength * 4.2f + Rand01() * 1.6f;
        p.velocity = {std::cos(angle) * radial,
                      dust ? 0.25f + Rand01() * 0.45f
                           : 1.2f + strength * 3.2f + Rand01() * 1.5f,
                      std::sin(angle) * radial};
        p.angularVelocity = {RandCentered() * 24.0f, RandCentered() * 30.0f,
                             RandCentered() * 24.0f};
        t->scale = dust ? XMFLOAT3{p.baseScale, p.baseScale * 0.42f, p.baseScale}
                        : XMFLOAT3{p.baseScale * 1.35f, p.baseScale * 0.8f,
                                   p.baseScale};
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::Water: {
        const bool mist = (k % 4) == 0;
        p.kind = mist ? EnvironmentParticleKind::WaterMist
                      : EnvironmentParticleKind::WaterDrop;
        mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
        mr->shader = particleShader;
        mr->isTransparent = true;
        mr->blendMode = mist ? BlendMode::Alpha : BlendMode::Add;
        mr->customFlags = {mist ? 1.0f : 3.0f, 0.0f, 0.0f, 0.0f};
        p.baseColor = mist ? XMFLOAT4{0.40f, 0.76f, 1.02f, 0.42f}
                           : XMFLOAT4{0.24f, 0.82f, 1.46f, 0.90f};
        p.baseScale = mist ? 0.13f + strength * 0.15f
                           : 0.025f + strength * 0.035f + Rand01() * 0.022f;
        p.lifetime = mist ? 0.55f + Rand01() * 0.30f
                          : 0.58f + Rand01() * 0.42f;
        const float radial = 1.7f + strength * 5.8f + Rand01() * 2.2f;
        p.velocity = {std::cos(angle) * radial,
                      mist ? 0.25f + Rand01() * 0.40f
                           : 2.0f + strength * 4.0f + Rand01() * 2.2f,
                      std::sin(angle) * radial};
        p.angularVelocity = {0, 0, 0};
        t->scale = mist ? XMFLOAT3{p.baseScale, p.baseScale * 0.38f, p.baseScale}
                        : XMFLOAT3{p.baseScale * 0.60f, p.baseScale * 1.7f,
                                   p.baseScale * 0.60f};
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::Lava: {
        const bool smoke = (k % 4) == 0;
        p.kind = smoke ? EnvironmentParticleKind::LavaSmoke
                       : EnvironmentParticleKind::Ember;
        mr->mesh = ctx.resource.LoadMesh(smoke ? "builtin/sphere" : "builtin/cube");
        mr->shader = particleShader;
        mr->isTransparent = true;
        mr->blendMode = smoke ? BlendMode::Alpha : BlendMode::Add;
        mr->customFlags = {smoke ? 1.0f : 4.0f, 0.0f, 0.0f, 0.0f};
        const float heat = Rand01() * 0.34f;
        p.baseColor = smoke ? XMFLOAT4{0.18f, 0.11f, 0.08f, 0.66f}
                            : XMFLOAT4{1.65f, 0.30f + heat, 0.025f, 1.0f};
        p.baseScale = smoke ? 0.14f + strength * 0.18f
                            : 0.022f + strength * 0.040f + Rand01() * 0.025f;
        p.lifetime = smoke ? 0.95f + Rand01() * 0.55f
                           : 0.64f + Rand01() * 0.55f;
        const float radial = 1.0f + strength * 4.4f + Rand01() * 1.8f;
        p.velocity = {std::cos(angle) * radial,
                      smoke ? 0.65f + Rand01() * 0.75f
                            : 1.9f + strength * 4.4f + Rand01() * 2.0f,
                      std::sin(angle) * radial};
        p.angularVelocity = {RandCentered() * 28.0f, RandCentered() * 36.0f,
                             RandCentered() * 28.0f};
        t->scale = smoke ? XMFLOAT3{p.baseScale, p.baseScale * 0.68f, p.baseScale}
                         : XMFLOAT3{p.baseScale * 0.5f, p.baseScale * 2.2f,
                                    p.baseScale * 0.5f};
        p.maxLifetime = p.lifetime;
        mr->color = p.baseColor;
        break;
      }

      case TerrainMaterial::None:
      default:
        p.kind = EnvironmentParticleKind::GenericDebris;
        mr->mesh = ctx.resource.LoadMesh("builtin/cube");
        mr->shader = basicShader;
        mr->isTransparent = false;
        mr->blendMode = BlendMode::Opaque;
        p.baseColor = {0.5f, 0.5f, 0.5f, 1.0f};
        mr->color = p.baseColor;
        mr->customFlags.x = 0.0f;
        mr->customFlags.y = 0.0f;
        p.baseScale = 0.05f + strength * 0.1f;
        t->scale = {p.baseScale, p.baseScale, p.baseScale};
        p.maxLifetime = p.lifetime;
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
    } else if (state->currentMaterial == TerrainMaterial::Ice) {
      count = 1 + static_cast<int>(speed01 * 2.0f);
    } else if (state->currentMaterial == TerrainMaterial::Stone) {
      count = state->currentBallSpeed > 2.0f ? 1 + static_cast<int>(speed01) : 0;
    } else if (state->currentMaterial == TerrainMaterial::Water) {
      count = 2 + static_cast<int>(speed01 * 4.0f);
    } else if (state->currentMaterial == TerrainMaterial::Lava) {
      count = 2 + static_cast<int>(speed01 * 3.0f);
    } else {
      count = 0;
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

          // 進行方向の後方へ芝片を巻き上げる。
          float angle = Rand01() * XM_2PI;
          float speed = 1.1f + Rand01() * 2.0f + speed01 * 1.4f;
          float trailX = body ? -body->velocity.x * (0.06f + speed01 * 0.05f) : 0.0f;
          float trailZ = body ? -body->velocity.z * (0.06f + speed01 * 0.05f) : 0.0f;
          p.velocity.x = trailX + std::cos(angle) * speed;
          p.velocity.y = 0.8f + Rand01() * 1.6f + speed01 * 0.6f;
          p.velocity.z = trailZ + std::sin(angle) * speed;

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

        case TerrainMaterial::Ice: {
          const bool mist = (k % 3) == 0;
          p.kind = mist ? EnvironmentParticleKind::IceMist
                        : EnvironmentParticleKind::IceShard;
          mr->mesh = ctx.resource.LoadMesh(mist ? "builtin/sphere" : "builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          mr->blendMode = mist ? BlendMode::Alpha : BlendMode::Add;
          mr->customFlags = {mist ? 1.0f : 2.0f, 0.0f, 0.0f, 0.0f};
          p.baseColor = mist ? XMFLOAT4{0.66f, 0.92f, 1.10f, 0.30f}
                             : XMFLOAT4{0.58f, 1.06f, 1.42f, 0.82f};
          p.baseScale = mist ? 0.075f + speed01 * 0.055f
                             : 0.015f + speed01 * 0.018f;
          t->scale = mist
                         ? XMFLOAT3{p.baseScale * 1.8f, p.baseScale * 0.25f,
                                    p.baseScale}
                         : XMFLOAT3{p.baseScale * 0.30f, p.baseScale * 2.4f,
                                    p.baseScale * 0.55f};
          p.lifetime = mist ? 0.42f + Rand01() * 0.25f
                            : 0.38f + Rand01() * 0.30f;
          const float side = RandCentered() * (0.8f + speed01 * 1.3f);
          p.velocity = {body ? -body->velocity.x * 0.075f + side : side,
                        mist ? 0.08f : 0.38f + Rand01() * 0.65f,
                        body ? -body->velocity.z * 0.075f + side : side};
          p.angularVelocity = {RandCentered() * 30.0f, RandCentered() * 38.0f,
                               RandCentered() * 30.0f};
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
          break;
        }

        case TerrainMaterial::Stone: {
          const bool dust = speed01 < 0.45f || (k % 3) == 0;
          p.kind = dust ? EnvironmentParticleKind::StoneDust
                        : EnvironmentParticleKind::StoneChip;
          mr->mesh = ctx.resource.LoadMesh(dust ? "builtin/sphere" : "builtin/rock");
          mr->shader = ctx.resource.LoadShader(
              dust ? "Particle" : "Basic",
              dust ? L"shaders/ParticleVS.hlsl" : L"Assets/shaders/BasicVS.hlsl",
              dust ? L"shaders/ParticlePS.hlsl" : L"Assets/shaders/BasicPS.hlsl");
          mr->isTransparent = dust;
          mr->blendMode = dust ? BlendMode::Alpha : BlendMode::Opaque;
          mr->customFlags = {dust ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
          p.baseColor = dust ? XMFLOAT4{0.48f, 0.45f, 0.41f, 0.34f}
                             : XMFLOAT4{0.34f, 0.33f, 0.31f, 1.0f};
          p.baseScale = dust ? 0.065f + speed01 * 0.055f
                             : 0.020f + speed01 * 0.022f;
          t->scale = dust ? XMFLOAT3{p.baseScale, p.baseScale * 0.30f, p.baseScale}
                          : XMFLOAT3{p.baseScale * 1.3f, p.baseScale * 0.7f,
                                     p.baseScale};
          p.lifetime = dust ? 0.42f + Rand01() * 0.28f
                            : 0.34f + Rand01() * 0.28f;
          const float side = RandCentered() * (0.7f + speed01 * 1.6f);
          p.velocity = {body ? -body->velocity.x * 0.045f + side : side,
                        dust ? 0.10f : 0.42f + Rand01() * 0.55f,
                        body ? -body->velocity.z * 0.045f - side : -side};
          p.angularVelocity = {RandCentered() * 22.0f, RandCentered() * 28.0f,
                               RandCentered() * 22.0f};
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
          break;
        }

        case TerrainMaterial::Water: {
          const bool mist = (k % 3) == 0;
          p.kind = mist ? EnvironmentParticleKind::WaterMist
                        : EnvironmentParticleKind::WaterDrop;
          mr->mesh = ctx.resource.LoadMesh("builtin/sphere");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          mr->blendMode = mist ? BlendMode::Alpha : BlendMode::Add;
          mr->customFlags = {mist ? 1.0f : 3.0f, 0.0f, 0.0f, 0.0f};
          p.baseColor = mist ? XMFLOAT4{0.36f, 0.70f, 0.96f, 0.30f}
                             : XMFLOAT4{0.22f, 0.76f, 1.38f, 0.84f};
          p.baseScale = mist ? 0.075f + speed01 * 0.070f
                             : 0.018f + speed01 * 0.024f;
          t->scale = mist ? XMFLOAT3{p.baseScale * 1.7f, p.baseScale * 0.28f,
                                     p.baseScale}
                          : XMFLOAT3{p.baseScale * 0.55f, p.baseScale * 1.7f,
                                     p.baseScale * 0.55f};
          p.lifetime = mist ? 0.38f + Rand01() * 0.26f
                            : 0.44f + Rand01() * 0.32f;
          const float side = RandCentered() * (1.0f + speed01 * 2.0f);
          p.velocity = {body ? -body->velocity.x * 0.085f + side : side,
                        mist ? 0.16f : 0.70f + Rand01() * 1.2f,
                        body ? -body->velocity.z * 0.085f - side : -side};
          p.angularVelocity = {0, 0, 0};
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
          break;
        }

        case TerrainMaterial::Lava: {
          const bool smoke = (k % 3) == 0;
          p.kind = smoke ? EnvironmentParticleKind::LavaSmoke
                         : EnvironmentParticleKind::Ember;
          mr->mesh = ctx.resource.LoadMesh(smoke ? "builtin/sphere" : "builtin/cube");
          mr->shader = ctx.resource.LoadShader(
              "Particle", L"shaders/ParticleVS.hlsl",
              L"shaders/ParticlePS.hlsl");
          mr->blendMode = smoke ? BlendMode::Alpha : BlendMode::Add;
          mr->customFlags = {smoke ? 1.0f : 4.0f, 0.0f, 0.0f, 0.0f};
          p.baseColor = smoke ? XMFLOAT4{0.15f, 0.09f, 0.065f, 0.48f}
                              : XMFLOAT4{1.58f, 0.42f + Rand01() * 0.25f,
                                         0.025f, 1.0f};
          p.baseScale = smoke ? 0.085f + speed01 * 0.080f
                              : 0.016f + speed01 * 0.022f;
          t->scale = smoke ? XMFLOAT3{p.baseScale, p.baseScale * 0.65f,
                                      p.baseScale}
                           : XMFLOAT3{p.baseScale * 0.45f, p.baseScale * 2.0f,
                                      p.baseScale * 0.45f};
          p.lifetime = smoke ? 0.72f + Rand01() * 0.45f
                             : 0.52f + Rand01() * 0.42f;
          const float side = RandCentered() * (0.8f + speed01 * 1.6f);
          p.velocity = {body ? -body->velocity.x * 0.055f + side : side,
                        smoke ? 0.48f + Rand01() * 0.45f
                              : 0.95f + Rand01() * 1.25f,
                        body ? -body->velocity.z * 0.055f - side : -side};
          p.angularVelocity = {RandCentered() * 20.0f, RandCentered() * 28.0f,
                               RandCentered() * 20.0f};
          p.maxLifetime = p.lifetime;
          mr->color = p.baseColor;
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
