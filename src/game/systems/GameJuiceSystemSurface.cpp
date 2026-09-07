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

void GameJuiceSystem::CreateSandSurfaceEntities(core::GameContext &ctx) {
  const auto craterMesh = ctx.resource.LoadMesh("builtin/sand_crater");
  const auto basicShader = ctx.resource.LoadShader(
      "Basic", L"Assets/shaders/BasicVS.hlsl",
      L"Assets/shaders/BasicPS.hlsl");

  m_sandImprints.clear();
  m_sandImprints.resize(kSandImprintCount);
  for (SandImprint &imprint : m_sandImprints) {
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
  m_sandImprintWriteIndex = 0;

  m_sandCollarEntity = m_entityOwner.Create(ctx.world);
  auto &collarTransform = ctx.world.Add<Transform>(m_sandCollarEntity);
  collarTransform.position = {0.0f, -100.0f, 0.0f};
  collarTransform.scale = {0.085f, 0.42f, 0.085f};

  auto &collarRenderer = ctx.world.Add<MeshRenderer>(m_sandCollarEntity);
  collarRenderer.mesh = craterMesh;
  collarRenderer.shader = basicShader;
  collarRenderer.color = {1.05f, 0.91f, 0.62f, 0.42f};
  collarRenderer.isVisible = false;
  collarRenderer.isTransparent = true;
  collarRenderer.blendMode = BlendMode::Alpha;
  collarRenderer.maxDrawDistance = 55.0f;
  collarRenderer.boundsScale = 1.8f;
}

void GameJuiceSystem::SpawnSandImprint(core::GameContext &ctx,
                                       const XMFLOAT3 &position, float scale,
                                       float lifetime,
                                       const XMFLOAT4 &color) {
  if (m_sandImprints.empty()) {
    return;
  }

  SandImprint &imprint = m_sandImprints[m_sandImprintWriteIndex];
  m_sandImprintWriteIndex =
      (m_sandImprintWriteIndex + 1) % kSandImprintCount;
  imprint.lifetime = std::max(lifetime, 0.1f);
  imprint.maxLifetime = imprint.lifetime;
  imprint.startScale = std::max(scale, 0.02f);
  imprint.baseColor = color;

  if (auto *transform = ctx.world.Get<Transform>(imprint.entity)) {
    transform->position = position;
    transform->position.y += 0.0025f;
    transform->scale = {imprint.startScale, 0.55f,
                        imprint.startScale * (0.82f + Rand01() * 0.24f)};
    XMStoreFloat4(&transform->rotation,
                  XMQuaternionRotationRollPitchYaw(0.0f, Rand01() * XM_2PI,
                                                   0.0f));
  }
  if (auto *renderer = ctx.world.Get<MeshRenderer>(imprint.entity)) {
    renderer->color = color;
    renderer->isVisible = true;
  }
}

void GameJuiceSystem::UpdateSandSurfaceEffects(core::GameContext &ctx,
                                               ecs::Entity targetEntity) {
  const float effectDt =
      std::min(ctx.dt, game::physics::kMaxSimulationDeltaTime);
  for (SandImprint &imprint : m_sandImprints) {
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
  auto *collarRenderer = ctx.world.Get<MeshRenderer>(m_sandCollarEntity);
  auto *collarTransform = ctx.world.Get<Transform>(m_sandCollarEntity);
  const bool isInSand = state && ballTransform && ballCollider &&
                        state->isBallGrounded &&
                        state->currentMaterial == TerrainMaterial::Bunker;

  if (!isInSand) {
    if (collarRenderer) {
      collarRenderer->isVisible = false;
    }
    m_sandTrackTimer = 0.0f;
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
  const float sink = ComputeSurfaceSinkDepth(
      TerrainMaterial::Bunker, verticalImpact, speed, ballCollider->radius);
  const float surfaceHeight =
      ballTransform->position.y - ballCollider->radius + sink;

  if (collarTransform && collarRenderer) {
    collarTransform->position = {ballTransform->position.x,
                                 surfaceHeight + 0.003f,
                                 ballTransform->position.z};
    const float collarScale = 0.075f + sink * 1.15f +
                              std::clamp(speed / 30.0f, 0.0f, 0.025f);
    collarTransform->scale = {collarScale, 0.46f, collarScale};
    collarRenderer->color = {1.06f, 0.91f, 0.61f,
                             std::clamp(0.34f + sink * 5.0f, 0.34f, 0.50f)};
    collarRenderer->isVisible = true;
  }

  if (speed > 0.35f) {
    m_sandTrackTimer += effectDt;
    const float interval = std::clamp(0.11f - speed * 0.002f, 0.045f, 0.11f);
    if (m_sandTrackTimer >= interval) {
      m_sandTrackTimer = 0.0f;
      SpawnSandImprint(
          ctx,
          {ballTransform->position.x, surfaceHeight, ballTransform->position.z},
          0.050f + std::clamp(speed / 300.0f, 0.0f, 0.035f),
          0.75f + Rand01() * 0.55f,
          {0.86f, 0.70f, 0.43f, 0.16f});
    }
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
    mr.color = {0.6f, 0.8f, 1.2f, 0.0f};

    m_ripples[i].entity = e;
    m_ripples[i].lifetime = 0.0f;
  }
  m_rippleWriteIndex = 0;
}

} // namespace game::systems
