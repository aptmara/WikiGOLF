#include "TrajectoryPredictor.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsFriction.h"
#include "../systems/WikiTerrainSystem.h"
#include <algorithm>
#include <cmath>

namespace game::controllers {

using namespace DirectX;
using namespace game::components;

void TrajectoryPredictor::Initialize(core::GameContext &ctx, size_t dotCount) {
  m_dots.clear();
  m_dots.reserve(dotCount);

  for (size_t i = 0; i < dotCount; ++i) {
    auto e = ctx.world.CreateEntity();
    auto &t = ctx.world.Add<Transform>(e);
    t.scale = {0.15f, 0.15f, 0.15f};

    auto &mr = ctx.world.Add<MeshRenderer>(e);
    mr.mesh = ctx.resource.LoadMesh("builtin/cube");
    mr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                        L"Assets/shaders/BasicPS.hlsl");
    mr.color = {1.0f, 1.0f, 0.0f, 0.9f};
    mr.isVisible = false;

    m_dots.push_back(e);
  }
}

void TrajectoryPredictor::Update(core::GameContext &ctx, const Params &params) {
  if (params.powerRatio > 0.0f) {
    auto *arrowMR = ctx.world.Get<MeshRenderer>(params.arrowEntity);
    if (arrowMR) {
      arrowMR->isVisible = false;
    }
  }

  if (m_dots.empty()) {
    return;
  }

  auto *ballRB = ctx.world.Get<RigidBody>(params.ballEntity);
  if (ballRB) {
    float ballSpeed = std::sqrt(ballRB->velocity.x * ballRB->velocity.x +
                                ballRB->velocity.y * ballRB->velocity.y +
                                ballRB->velocity.z * ballRB->velocity.z);
    if (ballSpeed > 0.1f) {
      Hide(ctx);
      return;
    }
  }

  float initialSpeed = params.maxPower * params.powerRatio;
  XMVECTOR dirXZ = XMLoadFloat3(&params.shotDirection);
  float rad = XMConvertToRadians(params.launchAngle);
  float vy = std::sin(rad) * initialSpeed;
  float vxz = std::cos(rad) * initialSpeed;

  XMVECTOR vel = XMVectorScale(dirXZ, vxz);
  vel = XMVectorSetY(vel, vy);

  auto *ballT = ctx.world.Get<Transform>(params.ballEntity);
  if (!ballT) {
    return;
  }
  XMVECTOR pos = XMLoadFloat3(&ballT->position);

  if (params.terrainSystem) {
    float startX = XMVectorGetX(pos);
    float startZ = XMVectorGetZ(pos);
    float startGroundY = params.terrainSystem->GetHeight(startX, startZ);
    float ballRadius = 0.2f;
    pos = XMVectorSetY(pos, startGroundY + ballRadius);
  }

  auto *rb = ctx.world.Get<RigidBody>(params.ballEntity);
  float drag = rb ? rb->drag : 0.30f;
  float restitution = rb ? rb->restitution : 0.35f;
  float mass = rb ? rb->mass : 0.0459f;

  auto *golfState = ctx.world.GetGlobal<GolfGameState>();
  auto terrainData =
      params.terrainSystem ? params.terrainSystem->GetTerrainData() : nullptr;

  auto getMaterial = [&](float x, float z) -> TerrainMaterial {
    if (!terrainData || terrainData->materialMap.empty()) {
      return TerrainMaterial::Fairway;
    }
    float u = x / terrainData->config.worldWidth + 0.5f;
    float v = 0.5f - z / terrainData->config.worldDepth;
    int ix = static_cast<int>(u * (terrainData->config.resolutionX - 1));
    int iz = static_cast<int>(v * (terrainData->config.resolutionZ - 1));
    if (ix < 0 || ix >= terrainData->config.resolutionX || iz < 0 ||
        iz >= terrainData->config.resolutionZ) {
      return TerrainMaterial::Fairway;
    }
    uint8_t id =
        terrainData->materialMap[iz * terrainData->config.resolutionX + ix];
    return static_cast<TerrainMaterial>(id);
  };

  XMVECTOR prevPos = pos;

  auto safeLength = [](XMVECTOR v) {
    float lenSq = XMVectorGetX(XMVector3LengthSq(v));
    if (lenSq < 0.0f || !std::isfinite(lenSq)) {
      return 0.0f;
    }
    return std::sqrt(lenSq);
  };

  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);
  float frameDt = std::clamp(ctx.dt, 0.008f, 0.033f);
  const int physicsSubSteps = 4;
  float subDt = frameDt / static_cast<float>(physicsSubSteps);
  const int simSubStepsPerDot = 12;

  for (size_t i = 0; i < m_dots.size(); ++i) {
    auto e = m_dots[i];
    auto *t = ctx.world.Get<Transform>(e);
    auto *mr = ctx.world.Get<MeshRenderer>(e);

    if (!mr || !t) {
      continue;
    }

    XMVECTOR currentPos = prevPos;

    for (int step = 0; step < simSubStepsPerDot; ++step) {
      XMVECTOR acc = gravity;

      float groundY = 0.0f;
      XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);
      bool isGrounded = false;

      if (params.terrainSystem) {
        float px = XMVectorGetX(currentPos);
        float pz = XMVectorGetZ(currentPos);
        groundY = params.terrainSystem->GetHeight(px, pz);

        float hX = params.terrainSystem->GetHeight(px + 0.1f, pz);
        float hZ = params.terrainSystem->GetHeight(px, pz + 0.1f);
        float gradX = (hX - groundY) / 0.1f;
        float gradZ = (hZ - groundY) / 0.1f;
        XMVECTOR slopeVec = XMVectorSet(-gradX, 1.0f, -gradZ, 0.0f);
        groundNormal = XMVector3Normalize(slopeVec);

        float ballBottom = XMVectorGetY(currentPos);
        float penetration = groundY - ballBottom;

        if (penetration > 0.0f) {
          currentPos = XMVectorSetY(currentPos, groundY);

          float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
          if (vn < 0.0f) {
            vel = XMVectorSubtract(
                vel, XMVectorScale(groundNormal, vn * (1.0f + restitution)));
          }

          isGrounded = true;
        } else if (penetration > -0.1f) {
          isGrounded = true;
        }
      }

      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(
            groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      if (isGrounded) {
        float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
        if (vn < 0.0f) {
          vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
        }

        float currentSpeed = safeLength(vel);
        float terrainScale = terrainData ? terrainData->config.friction : 1.0f;
        TerrainMaterial mat =
            getMaterial(XMVectorGetX(currentPos), XMVectorGetZ(currentPos));
        float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
        float frictionAccel = game::systems::ComputeGrassRollingAcceleration(
            currentSpeed, ny, mat, terrainScale);

        if (golfState) {
          float scale = golfState->rollingFrictionScale;
          if (!std::isfinite(scale) || scale < 0.05f) {
            scale = 1.0f;
          }
          frictionAccel *= scale;
        }

        float tangentialAcc = safeLength(acc);
        float staticLimit = frictionAccel * 1.2f;

        if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
          vel = XMVectorZero();
          acc = XMVectorZero();
        } else if (currentSpeed > 0.0001f) {
          float drop = frictionAccel * subDt;
          if (currentSpeed <= drop) {
            vel = XMVectorZero();
          } else {
            vel = XMVectorScale(vel, (currentSpeed - drop) / currentSpeed);
          }
        }

        float speedAfter = safeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
          vel = XMVectorZero();
        }
      }

      float speed = safeLength(vel);
      if (speed > 0.001f) {
        float K = 0.000876f;
        float dragForce = K * drag * speed * speed;
        float dragAccMag = dragForce / std::max(mass, 0.001f);

        XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
        XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMag);

        acc = XMVectorAdd(acc, dragAcc);
      }

      if (golfState && golfState->windSpeed > 0.0f) {
        float windForce = golfState->windSpeed * 0.1f;
        XMVECTOR windVec =
            XMVectorSet(golfState->windDirection.x, 0, golfState->windDirection.y, 0);
        acc = XMVectorAdd(acc, XMVectorScale(windVec, windForce));
      }

      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      currentPos = XMVectorAdd(currentPos, XMVectorScale(vel, subDt));

      float speedFinal = safeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }
    }

    float finalSpeed = safeLength(vel);
    float currentGroundY = 0.0f;
    if (params.terrainSystem) {
      float px = XMVectorGetX(currentPos);
      float pz = XMVectorGetZ(currentPos);
      currentGroundY = params.terrainSystem->GetHeight(px, pz);
    }

    bool isOnGround = XMVectorGetY(currentPos) <= currentGroundY + 0.01f;
    if (finalSpeed < 0.008f && isOnGround) {
      mr->isVisible = false;
      for (size_t j = i + 1; j < m_dots.size(); ++j) {
        auto *remainMR = ctx.world.Get<MeshRenderer>(m_dots[j]);
        if (remainMR) {
          remainMR->isVisible = false;
        }
      }
      break;
    }

    XMVECTOR segmentVec = XMVectorSubtract(currentPos, prevPos);
    float length = XMVectorGetX(XMVector3Length(segmentVec));
    if (length < 0.001f) {
      mr->isVisible = false;
      continue;
    }

    XMVECTOR midPoint = XMVectorAdd(prevPos, XMVectorScale(segmentVec, 0.5f));
    if (params.terrainSystem) {
      float midX = XMVectorGetX(midPoint);
      float midZ = XMVectorGetZ(midPoint);
      float midGroundY = params.terrainSystem->GetHeight(midX, midZ);
      float midY = XMVectorGetY(midPoint);
      if (midY < midGroundY + 0.1f) {
        midPoint = XMVectorSetY(midPoint, midGroundY + 0.1f);
      }
    }

    XMStoreFloat3(&t->position, midPoint);

    float baseThickness = 0.15f;
    if (params.isMapView) {
      baseThickness *= 2.0f;
    }
    t->scale = {baseThickness, baseThickness, length};

    XMVECTOR dir = XMVector3Normalize(segmentVec);
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    if (std::abs(XMVectorGetY(dir)) > 0.99f) {
      up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    }

    XMVECTOR zAxis = dir;
    XMVECTOR xAxis = XMVector3Normalize(XMVector3Cross(up, zAxis));
    XMVECTOR yAxis = XMVector3Cross(zAxis, xAxis);

    XMMATRIX rotMat = XMMatrixIdentity();
    rotMat.r[0] = xAxis;
    rotMat.r[1] = yAxis;
    rotMat.r[2] = zAxis;
    XMStoreFloat4(&t->rotation, XMQuaternionRotationMatrix(rotMat));

    mr->isVisible = XMVectorGetY(midPoint) >= 0.0f;
    prevPos = currentPos;
  }
}

void TrajectoryPredictor::Hide(core::GameContext &ctx) {
  for (auto e : m_dots) {
    auto *mr = ctx.world.Get<MeshRenderer>(e);
    if (mr) {
      mr->isVisible = false;
    }
  }
}

const std::vector<ecs::Entity> &TrajectoryPredictor::GetDots() const {
  return m_dots;
}

} // namespace game::controllers
