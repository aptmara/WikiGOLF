#pragma once
/**
 * @file TrajectorySimulation.h
 * @brief ボール弾道の共通物理シミュレーション
 *
 * TrajectoryPredictor(予測線描画)と ClubController のキャリー距離テーブル
 * 算出で共用する1サブステップ分の物理更新処理をまとめる。両者が同じ関数を
 * 参照することで、予測線・着弾マーカーと実際の弾道結果を一致させる。
 */

#include "../components/WikiComponents.h"
#include "../systems/PhysicsFriction.h"
#include "../systems/WikiTerrainSystem.h"
#include "CarryDistanceTable.h"
#include "GameplayPhysicsConstants.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cmath>

namespace game::physics {

/// @brief ボールの物理特性(RigidBodyから抽出した値)
struct BallPhysicsParams {
  float drag = 0.30f;
  float restitution = 0.35f;
  float mass = 0.0459f;
  float ballRadius = game::physics::kBallRadius;
  float rollingFrictionScale = 1.0f; ///< クラブごとの摩擦スケール
};

/// @brief terrainSystem が無い場合に地面として扱う仮想の平面
struct FlatGroundParams {
  bool enabled = false;
  float groundY = 0.0f;
  game::components::TerrainMaterial material =
      game::components::TerrainMaterial::Fairway;
};

struct WindParams {
  float windSpeed = 0.0f;
  DirectX::XMFLOAT2 windDirection = {1.0f, 0.0f};
};

struct BallSimState {
  DirectX::XMFLOAT3 position = {0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 velocity = {0.0f, 0.0f, 0.0f};
  bool isGrounded = false;
};

namespace trajectory_detail {

inline float SafeVecLength(DirectX::XMVECTOR v) {
  float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(v));
  if (lenSq < 0.0f || !std::isfinite(lenSq)) {
    return 0.0f;
  }
  return std::sqrt(lenSq);
}

inline game::components::TerrainMaterial SampleGroundMaterial(
    float x, float z,
    const std::shared_ptr<game::systems::TerrainData> &terrainData,
    const FlatGroundParams &flatGround) {
  using game::components::TerrainMaterial;
  if (!terrainData || terrainData->materialMap.empty()) {
    return flatGround.material;
  }
  float u = x / terrainData->config.worldWidth + 0.5f;
  float v = 0.5f - z / terrainData->config.worldDepth;
  int ix = static_cast<int>(u * (terrainData->config.resolutionX - 1));
  int iz = static_cast<int>(v * (terrainData->config.resolutionZ - 1));
  if (ix < 0 || ix >= terrainData->config.resolutionX || iz < 0 ||
      iz >= terrainData->config.resolutionZ) {
    return flatGround.material;
  }
  uint8_t id =
      terrainData->materialMap[iz * terrainData->config.resolutionX + ix];
  return static_cast<TerrainMaterial>(id);
}

} // namespace trajectory_detail

/// @brief ボール弾道シミュレーションを1サブステップ分進める。
/// terrainSystem が nullptr の場合は flatGround の平面を地面として扱う。
inline void StepBallSimulation(BallSimState &state,
                               game::systems::WikiTerrainSystem *terrainSystem,
                               const FlatGroundParams &flatGround,
                               const BallPhysicsParams &ballParams,
                               const WindParams &wind, float subDt) {
  using namespace DirectX;
  using namespace game::components;
  using trajectory_detail::SafeVecLength;
  using trajectory_detail::SampleGroundMaterial;

  const bool hasGround = (terrainSystem != nullptr) || flatGround.enabled;
  auto terrainData = terrainSystem ? terrainSystem->GetTerrainData() : nullptr;

  XMVECTOR pos = XMLoadFloat3(&state.position);
  XMVECTOR vel = XMLoadFloat3(&state.velocity);

  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);
  XMVECTOR acc = gravity;

  XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);
  bool isGrounded = false;

  if (hasGround) {
    float px = XMVectorGetX(pos);
    float pz = XMVectorGetZ(pos);

    float rawGroundY;
    if (terrainSystem) {
      rawGroundY = terrainSystem->GetHeight(px, pz);
      float hX = terrainSystem->GetHeight(px + 0.1f, pz);
      float hZ = terrainSystem->GetHeight(px, pz + 0.1f);
      float gradX = (hX - rawGroundY) / 0.1f;
      float gradZ = (hZ - rawGroundY) / 0.1f;
      XMVECTOR slopeVec = XMVectorSet(-gradX, 1.0f, -gradZ, 0.0f);
      groundNormal = XMVector3Normalize(slopeVec);
    } else {
      rawGroundY = flatGround.groundY;
      groundNormal = XMVectorSet(0, 1, 0, 0);
    }
    const float groundY = game::physics::ToVisualSurfaceHeight(rawGroundY);

    const TerrainMaterial contactMaterial =
        SampleGroundMaterial(px, pz, terrainData, flatGround);
    const float verticalImpactSpeed = std::max(0.0f, -XMVectorGetY(vel));
    const float sinkDepth = game::systems::ComputeSurfaceSinkDepth(
        contactMaterial, verticalImpactSpeed, SafeVecLength(vel),
        ballParams.ballRadius);
    const float contactHeight = groundY - sinkDepth;
    float ballBottom = XMVectorGetY(pos) - ballParams.ballRadius;
    float penetration = contactHeight - ballBottom;

    if (penetration > 0.0f) {
      const float normalY = std::max(XMVectorGetY(groundNormal), 0.1f);
      pos = XMVectorAdd(pos, XMVectorScale(groundNormal, penetration / normalY));

      float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
      if (vn < 0.0f) {
        const float incomingSpeed = SafeVecLength(vel);
        float bounce = ballParams.restitution * 0.5f;
        if (contactMaterial == TerrainMaterial::Bunker) {
          bounce = 0.0f;
        }
        vel = XMVectorSubtract(vel,
                               XMVectorScale(groundNormal, vn * (1.0f + bounce)));

        if (contactMaterial == TerrainMaterial::Bunker) {
          const float retention =
              game::systems::ComputeSurfaceImpactTangentialRetention(
                  contactMaterial, std::abs(vn), incomingSpeed);
          const float remainingNormalSpeed =
              XMVectorGetX(XMVector3Dot(vel, groundNormal));
          const XMVECTOR normalVelocity =
              XMVectorScale(groundNormal, remainingNormalSpeed);
          const XMVECTOR tangentialVelocity =
              XMVectorSubtract(vel, normalVelocity);
          vel = XMVectorAdd(normalVelocity,
                            XMVectorScale(tangentialVelocity, retention));
        }
      }
      isGrounded = true;
    } else if (penetration > -std::max(0.002f, ballParams.ballRadius * 0.15f)) {
      isGrounded = true;
    }
  }

  if (isGrounded) {
    XMVECTOR normalComponent = XMVectorScale(
        groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
    acc = XMVectorSubtract(acc, normalComponent);

    float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
    if (vn < 0.0f) {
      vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
    }

    float currentSpeed = SafeVecLength(vel);
    float terrainScale = terrainData ? terrainData->config.friction : 1.0f;
    TerrainMaterial mat = SampleGroundMaterial(XMVectorGetX(pos),
                                               XMVectorGetZ(pos), terrainData,
                                               flatGround);
    float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
    float frictionAccel = game::systems::ComputeGrassRollingAcceleration(
        currentSpeed, ny, mat, terrainScale);

    float scale = ballParams.rollingFrictionScale;
    if (!std::isfinite(scale) || scale < 0.05f) {
      scale = 1.0f;
    }
    frictionAccel *= scale;

    float tangentialAcc = SafeVecLength(acc);
    float staticLimit = frictionAccel * 1.2f;

    if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
      vel = XMVectorZero();
      acc = XMVectorZero();
    } else if (currentSpeed > 0.0001f) {
      float t = std::clamp((currentSpeed - 1.0f) / 4.0f, 0.0f, 1.0f);
      float k = frictionAccel;
      float expRatio = std::exp(-k * subDt);
      float linearDrop = frictionAccel * subDt;
      float linearRatio = (currentSpeed > linearDrop)
                              ? (currentSpeed - linearDrop) / currentSpeed
                              : 0.0f;
      float finalRatio = t * expRatio + (1.0f - t) * linearRatio;
      vel = XMVectorScale(vel, finalRatio);
      if (SafeVecLength(vel) < 0.02f) {
        vel = XMVectorZero();
      }
    }

    float speedAfter = SafeVecLength(vel);
    float slopeFlatness = XMVectorGetY(groundNormal);
    if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
      vel = XMVectorZero();
    }
  }

  float speed = SafeVecLength(vel);
  if (speed > 0.001f) {
    float K = 0.000876f;
    float dragForce = K * ballParams.drag * speed * speed;
    float dragAccMag = dragForce / std::max(ballParams.mass, 0.001f);
    XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
    acc = XMVectorAdd(acc, XMVectorScale(dragDir, dragAccMag));
  }

  if (wind.windSpeed > 0.0f) {
    float windForce = wind.windSpeed * 0.1f;
    XMVECTOR windVec =
        XMVectorSet(wind.windDirection.x, 0, wind.windDirection.y, 0);
    acc = XMVectorAdd(acc, XMVectorScale(windVec, windForce));
  }

  vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
  pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));

  float speedFinal = SafeVecLength(vel);
  float slopeFlatnessFinal = XMVectorGetY(groundNormal);
  if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
    vel = XMVectorZero();
  }

  XMStoreFloat3(&state.position, pos);
  XMStoreFloat3(&state.velocity, vel);
  state.isGrounded = isGrounded;
}

struct CarryResult {
  DirectX::XMFLOAT3 landingPosition = {0.0f, 0.0f, 0.0f};
  float horizontalDistance = 0.0f;
  bool settled = false; ///< maxSimSeconds以内に静止したか
};

/// @brief 初速・打ち出し角からボールが静止するまでシミュレートし、
/// 水平到達距離を返す。
inline CarryResult
SimulateCarryDistance(float initialSpeed, float launchAngleDeg,
                      const DirectX::XMFLOAT3 &shotDirection,
                      const DirectX::XMFLOAT3 &startPos,
                      game::systems::WikiTerrainSystem *terrainSystem,
                      const FlatGroundParams &flatGround,
                      const BallPhysicsParams &ballParams,
                      const WindParams &wind, float maxSimSeconds = 20.0f) {
  using namespace DirectX;

  BallSimState state;
  state.position = startPos;

  XMVECTOR dirXZ = XMVector3Normalize(XMLoadFloat3(&shotDirection));
  float rad = XMConvertToRadians(launchAngleDeg);
  float vy = std::sin(rad) * initialSpeed;
  float vxz = std::cos(rad) * initialSpeed;
  XMVECTOR vel = XMVectorScale(dirXZ, vxz);
  vel = XMVectorSetY(vel, vy);
  XMStoreFloat3(&state.velocity, vel);

  if (terrainSystem) {
    float startGroundY = game::physics::ToVisualSurfaceHeight(
        terrainSystem->GetHeight(startPos.x, startPos.z));
    state.position.y = startGroundY + ballParams.ballRadius;
  } else if (flatGround.enabled) {
    state.position.y =
        game::physics::ToVisualSurfaceHeight(flatGround.groundY) +
        ballParams.ballRadius;
  }

  const float subDt = game::physics::kMaxSimulationDeltaTime / 4.0f;
  const int maxSteps = static_cast<int>(maxSimSeconds / subDt);

  CarryResult result;
  for (int i = 0; i < maxSteps; ++i) {
    StepBallSimulation(state, terrainSystem, flatGround, ballParams, wind,
                       subDt);

    float speed =
        trajectory_detail::SafeVecLength(XMLoadFloat3(&state.velocity));
    if (speed < 0.008f && state.isGrounded) {
      result.settled = true;
      break;
    }
  }

  result.landingPosition = state.position;
  float dx = result.landingPosition.x - startPos.x;
  float dz = result.landingPosition.z - startPos.z;
  result.horizontalDistance = std::sqrt(dx * dx + dz * dz);
  return result;
}

/// @brief クラブごとの「初速→キャリー飛距離」対応表を、平坦・無風条件で
/// 構築する。0からmaxSpeedまでsampleCount分割でサンプリングする。
inline game::utils::CarryDistanceTable
BuildCarryDistanceTable(float maxSpeed, float launchAngleDeg,
                        const BallPhysicsParams &ballParams,
                        int sampleCount = 12) {
  game::utils::CarryDistanceTable table;
  if (maxSpeed <= 0.0f || sampleCount <= 0) {
    table.speeds = {0.0f};
    table.distances = {0.0f};
    return table;
  }

  FlatGroundParams flat;
  flat.enabled = true;
  flat.groundY = 0.0f;
  flat.material = game::components::TerrainMaterial::Fairway;
  WindParams noWind;
  const DirectX::XMFLOAT3 shotDirection = {0.0f, 0.0f, 1.0f};
  const DirectX::XMFLOAT3 startPos = {0.0f, 0.0f, 0.0f};

  table.speeds.reserve(sampleCount + 1);
  table.distances.reserve(sampleCount + 1);
  table.speeds.push_back(0.0f);
  table.distances.push_back(0.0f);

  for (int i = 1; i <= sampleCount; ++i) {
    const float speed =
        maxSpeed * static_cast<float>(i) / static_cast<float>(sampleCount);
    const CarryResult result =
        SimulateCarryDistance(speed, launchAngleDeg, shotDirection, startPos,
                              nullptr, flat, ballParams, noWind);
    table.speeds.push_back(speed);
    table.distances.push_back(result.horizontalDistance);
  }
  return table;
}

} // namespace game::physics
