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
    mr.isTransparent = true;
    mr.isVisible = false;

    m_dots.push_back(e);
  }

  // 着地点マーカーエンティティの作成
  m_landingEntity = ctx.world.CreateEntity();
  auto &lt = ctx.world.Add<Transform>(m_landingEntity);
  lt.scale = {0.45f, 0.08f, 0.45f}; // 扁平な円盤形

  auto &lmr = ctx.world.Add<MeshRenderer>(m_landingEntity);
  lmr.mesh = ctx.resource.LoadMesh("builtin/cube");
  lmr.shader = ctx.resource.LoadShader("Basic", L"Assets/shaders/BasicVS.hlsl",
                                       L"Assets/shaders/BasicPS.hlsl");
  lmr.color = {1.0f, 1.0f, 1.0f, 0.75f};
  lmr.isTransparent = true;
  lmr.isVisible = false;
}

void TrajectoryPredictor::Update(core::GameContext &ctx, const Params &params) {
  if (params.arrowEntity != UINT32_MAX) {
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

  // powerRatio == 0 の通常時は仮のパワー比率 (0.65) で軌道を描画する
  const bool isIdlePreview = (params.powerRatio <= 0.0f);
  const float effectivePowerRatio = isIdlePreview ? 0.65f : params.powerRatio;
  // グラデーションの基準色（先端と末尾）
  // 先端: 白がかった水色 末尾: 黄色》透明
  const XMFLOAT4 colorHead = isIdlePreview
      ? XMFLOAT4{0.55f, 0.90f, 1.00f, 0.70f}  // アイドル: 清澄なシアン
      : XMFLOAT4{1.00f, 1.00f, 0.85f, 1.00f}; // ショット: 白熱光
  const XMFLOAT4 colorTail = isIdlePreview
      ? XMFLOAT4{0.20f, 0.75f, 1.00f, 0.00f}  // アイドル: 透明へフェード
      : XMFLOAT4{1.00f, 0.40f, 0.05f, 0.00f}; // ショット: オレンジ》透明

  float initialSpeed = params.maxPower * effectivePowerRatio;
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

    // 先端(i=0)から末尾に向かって色・透明度・太さをグラデーションさせる
    const float nt = (m_dots.size() > 1)
        ? static_cast<float>(i) / static_cast<float>(m_dots.size() - 1)
        : 0.0f;
    XMFLOAT4 blendedColor;
    blendedColor.x = colorHead.x + (colorTail.x - colorHead.x) * nt;
    blendedColor.y = colorHead.y + (colorTail.y - colorHead.y) * nt;
    blendedColor.z = colorHead.z + (colorTail.z - colorHead.z) * nt;
    blendedColor.w = colorHead.w + (colorTail.w - colorHead.w) * nt;
    mr->color = blendedColor;

    // 先端太く(1.8x)→末尾細く(0.5x)の変型スケール
    const float baseThicknessMin = params.isMapView ? 0.12f : 0.06f;
    const float baseThicknessMax = params.isMapView ? 0.36f : 0.22f;
    const float thicknessForDot  = baseThicknessMax + (baseThicknessMin - baseThicknessMax) * nt;

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
          // ハイブリッド減衰（PhysicsSystemと同期）
          float t = std::clamp((currentSpeed - 1.0f) / 4.0f, 0.0f, 1.0f);

          float k = frictionAccel;
          float expRatio = std::exp(-k * subDt);

          float linearDrop = frictionAccel * subDt;
          float linearRatio = (currentSpeed > linearDrop) ? (currentSpeed - linearDrop) / currentSpeed : 0.0f;

          float finalRatio = t * expRatio + (1.0f - t) * linearRatio;
          vel = XMVectorScale(vel, finalRatio);

          if (safeLength(vel) < 0.02f) {
            vel = XMVectorZero();
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
      // 着地点マーカーをこの位置に表示
      if (m_landingEntity != UINT32_MAX) {
        auto *lmr = ctx.world.Get<MeshRenderer>(m_landingEntity);
        auto *lt  = ctx.world.Get<Transform>(m_landingEntity);
        if (lmr && lt) {
          XMFLOAT3 lp;
          XMStoreFloat3(&lp, currentPos);
          lp.y = currentGroundY + 0.05f; // 地面に蛻りつく変位
          lt->position = lp;
          lt->scale = {0.55f, 0.08f, 0.55f};
          // アイドル時は点滅する白円、ショット時は黄オレンジ
          lmr->color = isIdlePreview
              ? XMFLOAT4{0.85f, 0.97f, 1.00f, 0.70f}
              : XMFLOAT4{1.00f, 0.80f, 0.10f, 0.90f};
          lmr->isVisible = true;
        }
      }
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

    t->scale = {thicknessForDot, thicknessForDot, length};

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
  // 着地点マーカーも非表示
  if (m_landingEntity != UINT32_MAX) {
    auto *lmr = ctx.world.Get<MeshRenderer>(m_landingEntity);
    if (lmr) {
      lmr->isVisible = false;
    }
  }
}

const std::vector<ecs::Entity> &TrajectoryPredictor::GetDots() const {
  return m_dots;
}

} // namespace game::controllers
