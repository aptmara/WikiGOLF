/**
 * @file GameJuiceSystemSurface.cpp
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

void GameJuiceSystem::CreateSurfaceEffectEntities(core::GameContext &ctx) {
  const auto craterMesh = ctx.resource.LoadMesh("builtin/sand_crater");
  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");

  m_surfaceMarks.clear();
  m_surfaceMarks.resize(kSurfaceMarkCount);
  for (SurfaceMark &imprint : m_surfaceMarks) {
    imprint.entity = m_entityOwner.Create(ctx.world);
    auto &transform = ctx.world.Add<Transform>(imprint.entity);
    transform.position = {0.0f, -100.0f, 0.0f};
    transform.scale = {0.1f, 0.1f, 0.1f};

    auto &renderer = ctx.world.Add<MeshRenderer>(imprint.entity);
    renderer.mesh = craterMesh;
    renderer.shader = basicShader;
    renderer.color = {1.0f, 0.9f, 0.66f, 0.0f};
    renderer.isVisible = false;
    renderer.isTransparent = true;
    renderer.blendMode = BlendMode::Alpha;
    renderer.maxDrawDistance = 65.0f;
    renderer.boundsScale = 1.4f;
  }
  m_surfaceMarkWriteIndex = 0;

  m_surfaceCollarEntity = m_entityOwner.Create(ctx.world);
  auto &collarTransform = ctx.world.Add<Transform>(m_surfaceCollarEntity);
  collarTransform.position = {0.0f, -100.0f, 0.0f};
  collarTransform.scale = {0.085f, 0.42f, 0.085f};

  auto &collarRenderer = ctx.world.Add<MeshRenderer>(m_surfaceCollarEntity);
  collarRenderer.mesh = craterMesh;
  collarRenderer.shader = basicShader;
  collarRenderer.color = {1.05f, 0.91f, 0.62f, 0.42f};
  collarRenderer.isVisible = false;
  collarRenderer.isTransparent = true;
  collarRenderer.blendMode = BlendMode::Alpha;
  collarRenderer.maxDrawDistance = 55.0f;
  collarRenderer.boundsScale = 1.8f;
}

void GameJuiceSystem::SpawnSurfaceMark(core::GameContext &ctx,
                                       const XMFLOAT3 &position,
                                       TerrainMaterial material, float scale,
                                       float lifetime, const XMFLOAT4 &color) {
  if (m_surfaceMarks.empty()) {
    return;
  }

  SurfaceMark &imprint = m_surfaceMarks[m_surfaceMarkWriteIndex];
  m_surfaceMarkWriteIndex =
      (m_surfaceMarkWriteIndex + 1) % kSurfaceMarkCount;
  imprint.lifetime = std::max(lifetime, 0.1f);
  imprint.maxLifetime = imprint.lifetime;
  imprint.startScale = std::max(scale, 0.02f);
  imprint.baseColor = color;
  const bool isEnergyMark = material == TerrainMaterial::Ice ||
                            material == TerrainMaterial::Water ||
                            material == TerrainMaterial::Lava;

  if (auto *transform = ctx.world.Get<Transform>(imprint.entity)) {
    transform->position = position;
    transform->position.y += 0.0025f;
    transform->scale = {imprint.startScale, isEnergyMark ? 0.012f : 0.55f,
                        imprint.startScale * (0.82f + Rand01() * 0.24f)};
    XMStoreFloat4(&transform->rotation,
                  XMQuaternionRotationRollPitchYaw(0.0f, Rand01() * XM_2PI,
                                                   0.0f));
  }
  if (auto *renderer = ctx.world.Get<MeshRenderer>(imprint.entity)) {
    if (isEnergyMark) {
      renderer->mesh = ctx.resource.LoadMesh("builtin/cylinder");
      renderer->shader = ctx.resource.LoadShader(
          "Particle", L"shaders/ParticleVS.hlsl",
          L"shaders/ParticlePS.hlsl");
      renderer->blendMode = BlendMode::Add;
      renderer->customFlags = {5.0f, 0.68f, 0.0f, 0.0f};
    } else {
      renderer->mesh = ctx.resource.LoadMesh("builtin/sand_crater");
      renderer->shader = ctx.resource.LoadShader(
          "Basic", L"Assets/shaders/BasicVS.hlsl",
          L"Assets/shaders/BasicPS.hlsl");
      renderer->blendMode = BlendMode::Alpha;
      renderer->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    renderer->isTransparent = true;
    renderer->color = color;
    renderer->isVisible = true;
  }
}

void GameJuiceSystem::UpdateSurfaceEffects(core::GameContext &ctx,
                                           ecs::Entity targetEntity) {
  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);
  for (SurfaceMark &imprint : m_surfaceMarks) {
    if (imprint.lifetime <= 0.0f) {
      continue;
    }
    imprint.lifetime = std::max(0.0f, imprint.lifetime - effectDt);
    const float lifeRatio = imprint.lifetime / imprint.maxLifetime;
    if (auto *renderer = ctx.world.Get<MeshRenderer>(imprint.entity)) {
      const float fade = std::pow(std::clamp(lifeRatio, 0.0f, 1.0f), 1.8f);
      renderer->color = imprint.baseColor;
      renderer->color.w = imprint.baseColor.w * fade;
      if (imprint.lifetime <= 0.0f) {
        renderer->isVisible = false;
      }
    }
  }

  auto *state = ctx.world.GetGlobal<GolfGameState>();
  auto *ballTransform = ctx.world.Get<Transform>(targetEntity);
  auto *ballBody = ctx.world.Get<RigidBody>(targetEntity);
  auto *ballCollider = ctx.world.Get<Collider>(targetEntity);
  auto *collarRenderer = ctx.world.Get<MeshRenderer>(m_surfaceCollarEntity);
  auto *collarTransform = ctx.world.Get<Transform>(m_surfaceCollarEntity);
  const bool hasSurface = state && ballTransform && ballCollider &&
                          state->isBallGrounded &&
                          state->currentMaterial != TerrainMaterial::None;

  if (!hasSurface) {
    if (collarRenderer) {
      collarRenderer->isVisible = false;
    }
    m_surfaceTrackTimer = 0.0f;
    return;
  }

  float speed = 0.0f;
  float verticalImpact = 0.0f;
  if (ballBody) {
    speed = std::sqrt(ballBody->velocity.x * ballBody->velocity.x +
                      ballBody->velocity.y * ballBody->velocity.y +
                      ballBody->velocity.z * ballBody->velocity.z);
    verticalImpact = std::max(0.0f, -ballBody->velocity.y);
  }
  const TerrainMaterial material = state->currentMaterial;
  const float sink = ComputeSurfaceSinkDepth(
      material, verticalImpact, speed, ballCollider->radius);
  const float surfaceHeight =
      ballTransform->position.y - ballCollider->radius + sink;

  if (collarTransform && collarRenderer) {
    XMFLOAT4 collarColor = {0.28f, 0.86f, 0.20f, 0.38f};
    float collarScale = 0.095f + std::clamp(speed / 120.0f, 0.0f, 0.08f);
    float collarThickness = 0.012f;
    bool useEnergyRing = false;
    switch (material) {
    case TerrainMaterial::Rough:
      collarColor = {0.12f, 0.54f, 0.075f, 0.48f};
      collarScale *= 1.18f;
      break;
    case TerrainMaterial::Bunker:
      collarColor = {1.06f, 0.91f, 0.61f,
                     std::clamp(0.34f + sink * 5.0f, 0.34f, 0.50f)};
      collarScale = 0.075f + sink * 1.15f +
                    std::clamp(speed / 30.0f, 0.0f, 0.025f);
      collarThickness = 0.46f;
      break;
    case TerrainMaterial::Green:
      collarColor = {0.40f, 1.20f, 0.28f, 0.42f};
      collarScale *= 0.92f;
      break;
    case TerrainMaterial::Ice:
      collarColor = {0.54f, 1.32f, 1.75f, 0.72f};
      collarScale *= 1.28f;
      useEnergyRing = true;
      break;
    case TerrainMaterial::Stone:
      collarColor = {0.66f, 0.62f, 0.57f, 0.42f};
      collarScale *= 1.06f;
      break;
    case TerrainMaterial::Water:
      collarColor = {0.24f, 0.94f, 1.68f, 0.76f};
      collarScale *= 1.42f;
      useEnergyRing = true;
      break;
    case TerrainMaterial::Lava:
      collarColor = {1.82f, 0.32f, 0.025f, 0.78f};
      collarScale *= 1.34f;
      useEnergyRing = true;
      break;
    default:
      break;
    }

    collarTransform->position = {ballTransform->position.x,
                                 surfaceHeight + 0.003f,
                                 ballTransform->position.z};
    collarTransform->scale = {collarScale, collarThickness, collarScale};
    if (useEnergyRing) {
      collarRenderer->mesh = ctx.resource.LoadMesh("builtin/cylinder");
      collarRenderer->shader = ctx.resource.LoadShader(
          "Particle", L"shaders/ParticleVS.hlsl",
          L"shaders/ParticlePS.hlsl");
      collarRenderer->blendMode = BlendMode::Add;
      collarRenderer->customFlags = {5.0f, 0.35f, 0.0f, 0.0f};
    } else {
      collarRenderer->mesh = ctx.resource.LoadMesh("builtin/sand_crater");
      collarRenderer->shader = ctx.resource.LoadShader(
          "Basic", L"Assets/shaders/BasicVS.hlsl",
          L"Assets/shaders/BasicPS.hlsl");
      collarRenderer->blendMode = BlendMode::Alpha;
      collarRenderer->customFlags = {0.0f, 0.0f, 0.0f, 0.0f};
    }
    collarRenderer->color = collarColor;
    collarRenderer->isTransparent = true;
    collarRenderer->isVisible = material == TerrainMaterial::Bunker ||
                                speed > 0.20f;
  }

  if (speed > 0.35f) {
    float interval = std::clamp(0.085f - speed * 0.0022f, 0.030f, 0.085f);
    float markScale = 0.065f + std::clamp(speed / 260.0f, 0.0f, 0.055f);
    float markLifetime = 1.10f + Rand01() * 0.65f;
    XMFLOAT4 markColor = {0.12f, 0.38f, 0.07f, 0.30f};
    switch (material) {
    case TerrainMaterial::Rough:
      markScale *= 1.32f;
      markLifetime += 0.40f;
      markColor = {0.065f, 0.25f, 0.04f, 0.38f};
      break;
    case TerrainMaterial::Bunker:
      interval = std::clamp(0.075f - speed * 0.0018f, 0.028f, 0.075f);
      markScale *= 1.22f;
      markLifetime += 0.55f;
      markColor = {0.86f, 0.70f, 0.43f, 0.24f};
      break;
    case TerrainMaterial::Green:
      markScale *= 0.88f;
      markColor = {0.16f, 0.52f, 0.10f, 0.28f};
      break;
    case TerrainMaterial::Ice:
      markScale *= 1.35f;
      markLifetime += 0.35f;
      markColor = {0.52f, 1.22f, 1.62f, 0.60f};
      break;
    case TerrainMaterial::Stone:
      markScale *= 1.12f;
      markLifetime += 0.45f;
      markColor = {0.30f, 0.28f, 0.26f, 0.38f};
      break;
    case TerrainMaterial::Water:
      interval *= 0.72f;
      markScale *= 1.55f;
      markLifetime *= 0.72f;
      markColor = {0.22f, 0.82f, 1.52f, 0.64f};
      break;
    case TerrainMaterial::Lava:
      markScale *= 1.42f;
      markLifetime += 0.80f;
      markColor = {1.60f, 0.25f, 0.015f, 0.66f};
      break;
    default:
      break;
    }

    m_surfaceTrackTimer += effectDt;
    if (m_surfaceTrackTimer >= interval) {
      m_surfaceTrackTimer = std::fmod(m_surfaceTrackTimer, interval);
      SpawnSurfaceMark(
          ctx,
          {ballTransform->position.x, surfaceHeight, ballTransform->position.z},
          material, markScale, markLifetime, markColor);
    }
  } else {
    m_surfaceTrackTimer = 0.0f;
  }
}

void GameJuiceSystem::CreateRippleEntities(core::GameContext &ctx) {
  m_ripples.clear();
  m_ripples.resize(kRippleCount);

  for (int i = 0; i < kRippleCount; ++i) {
    auto e = m_entityOwner.Create(ctx.world);
    auto &t = ctx.world.Add<Transform>(e);
    t.position = {0, -100, 0};
    t.scale = {0.5f, 0.02f, 0.5f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cylinder");
    mr.shader = ctx.resource.LoadShader("Particle", L"shaders/ParticleVS.hlsl",
                                        L"shaders/ParticlePS.hlsl");
    mr.isVisible = false;
    mr.isTransparent = true;
    mr.blendMode = BlendMode::Add;
    mr.color = {0.6f, 0.8f, 1.2f, 0.0f};
    mr.customFlags = {5.0f, 0.0f, 0.0f, 0.0f};

    m_ripples[i].entity = e;
    m_ripples[i].lifetime = 0.0f;
  }
  m_rippleWriteIndex = 0;
}

} // namespace game::systems
