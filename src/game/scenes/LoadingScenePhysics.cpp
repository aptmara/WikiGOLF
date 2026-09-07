/**
 * @file LoadingScenePhysics.cpp
 * @brief LoadingSceneの責務別実装です。
 */

#include "LoadingScene.h"
#include "../../core/GameContext.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../core/SceneManager.h"
#include "../../graphics/TextRenderer.h"
#include "../../graphics/WikiTextureGenerator.h"
#include "../components/Camera.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/UIText.h"
#include "../systems/WikiClient.h"
#include "../systems/WikiShortestPath.h"
#include "LoadingSceneUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <d2d1_1.h>
#include <filesystem>
#include <random>
#include <thread>

namespace game::scenes {

void LoadingScene::UpdatePhysics(core::GameContext &ctx, float dt) {
  const float renderRadius = BALL_RADIUS * BALL_MODEL_SCALE;
  const float floorY = FLOOR_Y + 0.3f + renderRadius;
  const float wallX = ARENA_HALF_WIDTH - renderRadius;
  const float wallZ = ARENA_HALF_DEPTH - renderRadius;

  if (m_balls.empty()) {
    m_movingCount = 0;
    m_maxSpeed = 0.0f;
    m_avgSpeed = 0.0f;
    m_settledCount = 0;
    return;
  }

  // dt が急に大きくなったときの暴発対策
  dt = std::clamp(dt, 0.0f, 0.033f);

  std::vector<bool> supported(m_balls.size(), false);

  const int subSteps = 8;
  const float subDt = dt / static_cast<float>(subSteps);

  constexpr float pileRestitution = 0.08f;
  constexpr float lowImpactBounceCut = 2.0f;
  constexpr float positionCorrectionPercent = 0.75f;
  constexpr float positionCorrectionSlop = 0.01f;
  constexpr float supportNormalY = 0.45f;
  constexpr float supportLinearDamping = 0.86f;
  constexpr float supportAngularDamping = 0.90f;

  for (int step = 0; step < subSteps; ++step) {
    for (size_t i = 0; i < m_balls.size(); ++i) {
      auto &ball = m_balls[i];

      if (!ctx.world.IsAlive(ball.entity) || ball.settled) {
        continue;
      }

      auto *tr = ctx.world.Get<components::Transform>(ball.entity);
      if (!tr) {
        continue;
      }

      ball.velocity.y += GRAVITY * subDt;

      const float subDrag = std::pow(AIR_DRAG, subDt * 60.0f);
      ball.velocity.x *= subDrag;
      ball.velocity.z *= subDrag;

      tr->position.x += ball.velocity.x * subDt;
      tr->position.y += ball.velocity.y * subDt;
      tr->position.z += ball.velocity.z * subDt;

      DirectX::XMVECTOR currentRot = DirectX::XMLoadFloat4(&tr->rotation);
      DirectX::XMVECTOR deltaRot =
          DirectX::XMQuaternionRotationRollPitchYaw(
              ball.angularVelocity.x * subDt,
              ball.angularVelocity.y * subDt,
              ball.angularVelocity.z * subDt);

      DirectX::XMVECTOR nextRot = DirectX::XMQuaternionNormalize(
          DirectX::XMQuaternionMultiply(deltaRot, currentRot));
      DirectX::XMStoreFloat4(&tr->rotation, nextRot);

      const float subAngDrag = std::pow(ANGULAR_DAMPING, subDt * 60.0f);
      ball.angularVelocity.x *= subAngDrag;
      ball.angularVelocity.y *= subAngDrag;
      ball.angularVelocity.z *= subAngDrag;

      if (tr->position.y < floorY) {
        tr->position.y = floorY;
        supported[i] = true;

        if (ball.velocity.y < -0.5f) {
          ball.velocity.y = -ball.velocity.y * pileRestitution;
        } else {
          ball.velocity.y = 0.0f;
        }

        const float subFriction = std::pow(FRICTION, subDt * 60.0f);
        ball.velocity.x *= subFriction;
        ball.velocity.z *= subFriction;
      }

      const float wallMinX = -wallX;
      const float wallMaxX = wallX;
      const float wallMinZ = -wallZ;
      const float wallMaxZ = wallZ;

      if (tr->position.x < wallMinX) {
        tr->position.x = wallMinX;
        ball.velocity.x = -ball.velocity.x * pileRestitution;
      } else if (tr->position.x > wallMaxX) {
        tr->position.x = wallMaxX;
        ball.velocity.x = -ball.velocity.x * pileRestitution;
      }

      if (tr->position.z < wallMinZ) {
        tr->position.z = wallMinZ;
        ball.velocity.z = -ball.velocity.z * pileRestitution;
      } else if (tr->position.z > wallMaxZ) {
        tr->position.z = wallMaxZ;
        ball.velocity.z = -ball.velocity.z * pileRestitution;
      }
    }
  }

  // ボール同士の衝突
  for (size_t i = 0; i < m_balls.size(); ++i) {
    auto &ball = m_balls[i];

    if (!ctx.world.IsAlive(ball.entity)) {
      continue;
    }

    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    if (!tr) {
      continue;
    }

    for (size_t j = i + 1; j < m_balls.size(); ++j) {
      auto &other = m_balls[j];

      if (!ctx.world.IsAlive(other.entity)) {
        continue;
      }

      auto *otherTr = ctx.world.Get<components::Transform>(other.entity);
      if (!otherTr) {
        continue;
      }

      const float dx = otherTr->position.x - tr->position.x;
      const float dy = otherTr->position.y - tr->position.y;
      const float dz = otherTr->position.z - tr->position.z;

      const float distSq = dx * dx + dy * dy + dz * dz;
      const float minDist = renderRadius * 2.0f;

      if (distSq >= minDist * minDist || distSq <= 0.0001f) {
        continue;
      }

      const float dist = std::sqrt(distSq);
      const float overlap = minDist - dist;

      const float nx = dx / dist;
      const float ny = dy / dist;
      const float nz = dz / dist;

      // 上に乗っているボールを support 済みにする
      if (ny > supportNormalY) {
        supported[j] = true;
      } else if (ny < -supportNormalY) {
        supported[i] = true;
      }

      const bool ballStatic = ball.settled;
      const bool otherStatic = other.settled;

      // すでに静止済み同士なら何もしない
      if (ballStatic && otherStatic) {
        continue;
      }

      float invMass1 = 1.0f;
      float invMass2 = 1.0f;
      if (ballStatic) {
        invMass1 = 0.0f;
      }
      if (otherStatic) {
        invMass2 = 0.0f;
      }
      const float invMassSum = invMass1 + invMass2;

      if (invMassSum <= 0.0f) {
        continue;
      }

      // めり込み補正
      const float correction =
          std::max(overlap - positionCorrectionSlop, 0.0f) *
          positionCorrectionPercent / invMassSum;

      tr->position.x -= nx * correction * invMass1;
      tr->position.y -= ny * correction * invMass1;
      tr->position.z -= nz * correction * invMass1;

      otherTr->position.x += nx * correction * invMass2;
      otherTr->position.y += ny * correction * invMass2;
      otherTr->position.z += nz * correction * invMass2;

      const float rvx = other.velocity.x - ball.velocity.x;
      const float rvy = other.velocity.y - ball.velocity.y;
      const float rvz = other.velocity.z - ball.velocity.z;

      const float velAlongNormal = rvx * nx + rvy * ny + rvz * nz;

      // 離れている方向なら反発させない
      if (velAlongNormal > 0.0f) {
        continue;
      }

      const float impactSpeed = std::abs(velAlongNormal);

      // 低速接触時は反発ゼロ
      float restitution = pileRestitution;
      if (impactSpeed < lowImpactBounceCut) {
        restitution = 0.0f;
      }

      const float impulse =
          -(1.0f + restitution) * velAlongNormal / invMassSum;

      const float ix = nx * impulse;
      const float iy = ny * impulse;
      const float iz = nz * impulse;

      if (!ballStatic) {
        ball.velocity.x -= ix * invMass1;
        ball.velocity.y -= iy * invMass1;
        ball.velocity.z -= iz * invMass1;
      }

      if (!otherStatic) {
        other.velocity.x += ix * invMass2;
        other.velocity.y += iy * invMass2;
        other.velocity.z += iz * invMass2;
      }
    }
  }

  // 支持されているボールは横揺れと回転を強めに減衰
  for (size_t i = 0; i < m_balls.size(); ++i) {
    auto &ball = m_balls[i];

    if (!ctx.world.IsAlive(ball.entity) || ball.settled) {
      continue;
    }

    if (!supported[i]) {
      continue;
    }

    ball.velocity.x *= supportLinearDamping;
    ball.velocity.z *= supportLinearDamping;

    if (std::abs(ball.velocity.y) < 0.25f) {
      ball.velocity.y = 0.0f;
    }

    ball.angularVelocity.x *= supportAngularDamping;
    ball.angularVelocity.y *= supportAngularDamping;
    ball.angularVelocity.z *= supportAngularDamping;
  }

  // 停止判定
  for (size_t i = 0; i < m_balls.size(); ++i) {
    auto &ball = m_balls[i];

    if (!ctx.world.IsAlive(ball.entity)) {
      continue;
    }

    auto *tr = ctx.world.Get<components::Transform>(ball.entity);
    if (!tr) {
      continue;
    }

    const float speedSq =
        ball.velocity.x * ball.velocity.x +
        ball.velocity.y * ball.velocity.y +
        ball.velocity.z * ball.velocity.z;

    const float angularSpeedSq =
        ball.angularVelocity.x * ball.angularVelocity.x +
        ball.angularVelocity.y * ball.angularVelocity.y +
        ball.angularVelocity.z * ball.angularVelocity.z;

    const bool onFloor = tr->position.y <= floorY + 0.1f;
    const bool canSettle = supported[i] || onFloor;

    if (canSettle &&
        speedSq < SETTLE_THRESHOLD * SETTLE_THRESHOLD &&
        angularSpeedSq < 0.25f) {
      ball.settled = true;
      ball.velocity = {0.0f, 0.0f, 0.0f};
      ball.angularVelocity = {0.0f, 0.0f, 0.0f};
    } else if (!ball.settled) {
      ball.settled = false;
    }
  }

  // ログ用メトリクス
  int settledCount = 0;
  int moving = 0;
  float maxSpeed = 0.0f;
  float speedAccum = 0.0f;
  int speedCount = 0;

  m_hasMovingSample = false;

  for (const auto &ball : m_balls) {
    if (!ctx.world.IsAlive(ball.entity)) {
      continue;
    }

    if (ball.settled) {
      settledCount++;
      continue;
    }

    moving++;

    const float speed =
        std::sqrt(ball.velocity.x * ball.velocity.x +
                  ball.velocity.y * ball.velocity.y +
                  ball.velocity.z * ball.velocity.z);

    maxSpeed = std::max(maxSpeed, speed);
    speedAccum += speed;
    speedCount++;

    if (!m_hasMovingSample) {
      if (auto *tr = ctx.world.Get<components::Transform>(ball.entity)) {
        m_lastMovingPos = tr->position;
        m_hasMovingSample = true;
      }
    }
  }

  m_movingCount = moving;
  m_maxSpeed = maxSpeed;
  m_settledCount = settledCount;
  m_avgSpeed = 0.0f;
  if (speedCount > 0) {
    m_avgSpeed = speedAccum / speedCount;
  }
}

bool LoadingScene::AreAllBallsSettled() {
  if (m_spawnedCount < TOTAL_BALLS)
    return false;
  for (const auto &ball : m_balls) {
    if (!ball.settled)
      return false;
  }
  if (!m_allSettledLogged) {
    LOG_INFO("LoadingScene", "All balls settled");
    m_allSettledLogged = true;
  }
  return true;
}

} // namespace game::scenes
