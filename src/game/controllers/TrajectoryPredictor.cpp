#include "TrajectoryPredictor.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsFriction.h"
#include "../systems/WikiTerrainSystem.h"
#include "../utils/CarryDistanceTable.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "../utils/TrajectorySimulation.h"
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

  const float targetDistance = params.baseCarryDistance * effectivePowerRatio;
  const float initialSpeed =
      params.carryTable
          ? game::utils::LookupSpeedForDistance(*params.carryTable, targetDistance)
          : targetDistance; // テーブル未設定時のフォールバック
  XMVECTOR dirXZ = XMLoadFloat3(&params.shotDirection);
  float rad = XMConvertToRadians(params.launchAngle);
  float vy = std::sin(rad) * initialSpeed;
  float vxz = std::cos(rad) * initialSpeed;

  XMVECTOR initialVel = XMVectorScale(dirXZ, vxz);
  initialVel = XMVectorSetY(initialVel, vy);

  auto *ballT = ctx.world.Get<Transform>(params.ballEntity);
  if (!ballT) {
    return;
  }
  XMVECTOR pos = XMLoadFloat3(&ballT->position);

  auto *ballCollider = ctx.world.Get<Collider>(params.ballEntity);
  const float ballRadius =
      ballCollider ? ballCollider->radius : game::physics::kBallRadius;

  if (params.terrainSystem) {
    float startX = XMVectorGetX(pos);
    float startZ = XMVectorGetZ(pos);
    float startGroundY = game::physics::ToVisualSurfaceHeight(
        params.terrainSystem->GetHeight(startX, startZ));
    pos = XMVectorSetY(pos, startGroundY + ballRadius);
  }

  // 実際の弾道シミュレーション(ClubControllerのキャリー距離テーブル生成・
  // マップビュー着弾点プレビューと同一のStepBallSimulation)を使うことで、
  // 予測線・着弾マーカーが実際のショット結果と整合する。
  auto *rb = ctx.world.Get<RigidBody>(params.ballEntity);
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();

  game::physics::BallPhysicsParams ballParams;
  ballParams.drag = rb ? rb->drag : 0.30f;
  ballParams.restitution = rb ? rb->restitution : 0.35f;
  ballParams.mass = rb ? rb->mass : 0.0459f;
  ballParams.ballRadius = ballRadius;
  ballParams.rollingFrictionScale =
      golfState ? golfState->rollingFrictionScale : 1.0f;
  if (!std::isfinite(ballParams.rollingFrictionScale) ||
      ballParams.rollingFrictionScale < 0.05f) {
    ballParams.rollingFrictionScale = 1.0f;
  }

  game::physics::WindParams wind;
  if (golfState) {
    wind.windSpeed = golfState->windSpeed;
    wind.windDirection = golfState->windDirection;
  }

  const game::physics::FlatGroundParams flatGround; // 地形が無ければ自由落下のまま(既存挙動を維持)

  auto terrainData =
      params.terrainSystem ? params.terrainSystem->GetTerrainData() : nullptr;

  // 着地点マーカーの高さ計算にのみ使用(物理積分自体はStepBallSimulationに委譲)
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

  auto safeLength = [](XMVECTOR v) {
    float lenSq = XMVectorGetX(XMVector3LengthSq(v));
    if (lenSq < 0.0f || !std::isfinite(lenSq)) {
      return 0.0f;
    }
    return std::sqrt(lenSq);
  };

  game::physics::BallSimState simState;
  XMStoreFloat3(&simState.position, pos);
  XMStoreFloat3(&simState.velocity, initialVel);

  XMVECTOR prevPos = pos;

  float frameDt = std::clamp(ctx.dt, 0.008f,
                             game::physics::kMaxSimulationDeltaTime);
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

    for (int step = 0; step < simSubStepsPerDot; ++step) {
      game::physics::StepBallSimulation(simState, params.terrainSystem,
                                        flatGround, ballParams, wind, subDt);
    }

    XMVECTOR currentPos = XMLoadFloat3(&simState.position);
    XMVECTOR currentVel = XMLoadFloat3(&simState.velocity);

    float finalSpeed = safeLength(currentVel);
    float currentGroundY = 0.0f;
    if (params.terrainSystem) {
      float px = XMVectorGetX(currentPos);
      float pz = XMVectorGetZ(currentPos);
      const float surfaceHeight = game::physics::ToVisualSurfaceHeight(
          params.terrainSystem->GetHeight(px, pz));
      const TerrainMaterial material = getMaterial(px, pz);
      const float sinkDepth = game::systems::ComputeSurfaceSinkDepth(
          material, std::max(0.0f, -XMVectorGetY(currentVel)), finalSpeed,
          ballRadius);
      currentGroundY = surfaceHeight - sinkDepth;
    }

    bool isOnGround =
        XMVectorGetY(currentPos) - ballRadius <=
        currentGroundY + std::max(0.002f, ballRadius * 0.15f);
    if (finalSpeed < 0.008f && isOnGround) {
      mr->isVisible = false;
      // 着地点マーカーをこの位置に表示
      if (m_landingEntity != UINT32_MAX) {
        auto *lmr = ctx.world.Get<MeshRenderer>(m_landingEntity);
        auto *lt  = ctx.world.Get<Transform>(m_landingEntity);
        if (lmr && lt) {
          XMFLOAT3 lp;
          XMStoreFloat3(&lp, currentPos);
          lp.y = currentGroundY + 0.003f;
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
      float midGroundY = game::physics::ToVisualSurfaceHeight(
          params.terrainSystem->GetHeight(midX, midZ));
      float midY = XMVectorGetY(midPoint);
      if (midY < midGroundY + ballRadius) {
        midPoint = XMVectorSetY(midPoint, midGroundY + ballRadius);
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
