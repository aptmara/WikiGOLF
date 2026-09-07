/**
 * @file WikiTerrainSurfaceResponse.cpp
 * @brief 芝の接触応答を実装します。
 */

#include "WikiTerrainSystem.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/GrassRenderBatch.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include "../components/Transform.h"
#include <algorithm>
#include <cmath>
#include <cfloat>

namespace game::systems {

using namespace game::components;

void WikiTerrainSystem::UpdateSurfaceResponse(core::GameContext &ctx,
                                              ecs::Entity ballEntity,
                                              float dt) {
  if (m_grassPatches.empty() || !ctx.world.IsAlive(ballEntity) ||
      !std::isfinite(dt) || dt <= 0.0f) {
    return;
  }

  const auto *ballTransform = ctx.world.Get<Transform>(ballEntity);
  const auto *ballBody = ctx.world.Get<RigidBody>(ballEntity);
  const auto *state = ctx.world.GetGlobal<GolfGameState>();
  if (!ballTransform) {
    return;
  }

  float velocityX = 0.0f;
  float velocityZ = 0.0f;
  if (ballBody) {
    velocityX = ballBody->velocity.x;
    velocityZ = ballBody->velocity.z;
  }
  const float horizontalSpeed =
      std::sqrt(velocityX * velocityX + velocityZ * velocityZ);
  if (horizontalSpeed > 0.05f) {
    velocityX /= horizontalSpeed;
    velocityZ /= horizontalSpeed;
  }

  const bool touchesGrass =
      state && state->isBallGrounded &&
      (state->currentMaterial == TerrainMaterial::Fairway ||
       state->currentMaterial == TerrainMaterial::Rough ||
       state->currentMaterial == TerrainMaterial::Green);

  // --- ボールの軌跡を一定距離ぶんだけ記録する ---
  // 直近の通過位置を残しておくことで、ボールが通り過ぎた後も
  // その軌跡上の芝を倒れたままにできる（時間経過だけに頼らない）。
  constexpr float kGrassTrailMaxDistance = 10.0f;  // 保持する軌跡の長さ(m)
  constexpr float kGrassTrailSampleSpacing = 0.1f; // サンプル間の最小間隔(m)
  constexpr float kGrassTrailTeleportDistance = 6.0f; // これ以上跳んだら軌跡をリセット

  const DirectX::XMFLOAT3 currentBallPos = ballTransform->position;

  if (!touchesGrass) {
    // 空中や芝以外の地形にいる間は軌跡を継続しない（着地後は新しい軌跡から始める）。
    m_ballTrail.clear();
  } else if (m_ballTrail.empty()) {
    m_ballTrail.push_back(currentBallPos);
  } else {
    const DirectX::XMFLOAT3 &last = m_ballTrail.back();
    const float jdx = currentBallPos.x - last.x;
    const float jdz = currentBallPos.z - last.z;
    const float jumpDistSq = jdx * jdx + jdz * jdz;
    if (jumpDistSq >
        kGrassTrailTeleportDistance * kGrassTrailTeleportDistance) {
      // ホール開始やリセットなど、瞬間移動した場合は軌跡を作り直す。
      m_ballTrail.clear();
      m_ballTrail.push_back(currentBallPos);
    } else if (jumpDistSq >=
               kGrassTrailSampleSpacing * kGrassTrailSampleSpacing) {
      m_ballTrail.push_back(currentBallPos);
    } else {
      m_ballTrail.back() = currentBallPos;
    }
  }

  // 末尾（最新位置）から遡って合計距離が上限を超えた分だけ先頭を捨てる。
  float trailLength = 0.0f;
  float trailMinX = currentBallPos.x, trailMaxX = currentBallPos.x;
  float trailMinZ = currentBallPos.z, trailMaxZ = currentBallPos.z;
  for (size_t i = m_ballTrail.size(); i-- > 1;) {
    const DirectX::XMFLOAT3 &a = m_ballTrail[i];
    const DirectX::XMFLOAT3 &b = m_ballTrail[i - 1];
    const float sdx = a.x - b.x;
    const float sdz = a.z - b.z;
    trailLength += std::sqrt(sdx * sdx + sdz * sdz);
    if (trailLength > kGrassTrailMaxDistance) {
      m_ballTrail.erase(m_ballTrail.begin(), m_ballTrail.begin() + i);
      break;
    }
  }
  for (const auto &sample : m_ballTrail) {
    trailMinX = std::min(trailMinX, sample.x);
    trailMaxX = std::max(trailMaxX, sample.x);
    trailMinZ = std::min(trailMinZ, sample.z);
    trailMaxZ = std::max(trailMaxZ, sample.z);
  }

  for (GrassPatch &grass : m_grassPatches) {
    const float patchReach = grass.halfExtent + 0.6f;
    const bool insideTrailBounds =
        touchesGrass && grass.position.x >= trailMinX - patchReach &&
        grass.position.x <= trailMaxX + patchReach &&
        grass.position.z >= trailMinZ - patchReach &&
        grass.position.z <= trailMaxZ + patchReach;

    if (!insideTrailBounds && grass.response <= 0.0f) {
      continue;
    }

    auto *batch = ctx.world.Get<GrassRenderBatch>(grass.entity);
    if (!batch || grass.instanceIndex >= batch->instances.size()) {
      continue;
    }

    float targetResponse = 0.0f;
    bool isNearBall = false;

    if (insideTrailBounds) {
      // 軌跡上で最も近いサンプル点を探し、それを接触点として扱う。
      float bestDistSq = FLT_MAX;
      DirectX::XMFLOAT3 bestSample = currentBallPos;
      for (const auto &sample : m_ballTrail) {
        const float sdx = grass.position.x - sample.x;
        const float sdz = grass.position.z - sample.z;
        const float sDistSq = sdx * sdx + sdz * sdz;
        if (sDistSq < bestDistSq) {
          bestDistSq = sDistSq;
          bestSample = sample;
        }
      }

      isNearBall = bestDistSq < patchReach * patchReach &&
                   std::abs(grass.position.y - bestSample.y) < 0.7f;

      if (isNearBall) {
        grass.interactionPoint = {bestSample.x, bestSample.z};
        const float speedResponse =
            std::clamp(0.68f + horizontalSpeed * 0.045f, 0.68f, 1.0f);
        targetResponse = speedResponse;

        const float dx = grass.position.x - bestSample.x;
        const float dz = grass.position.z - bestSample.z;
        if (horizontalSpeed > 0.05f) {
          grass.interactionYaw = std::atan2(velocityZ, velocityX);
        } else if (bestDistSq > 0.0001f) {
          grass.interactionYaw = std::atan2(dz, dx);
        }
      }
    }

    float responseSpeed = 3.8f;
    if (targetResponse > grass.response) {
      responseSpeed = 18.0f;
    }
    const float blend = 1.0f - std::exp(-responseSpeed * dt);
    grass.response += (targetResponse - grass.response) * blend;
    if (grass.response < 0.002f) {
      grass.response = 0.0f;
    }

    batch->instances[grass.instanceIndex].flags = {
        grass.interactionPoint.x, grass.interactionPoint.y,
        grass.interactionYaw, grass.response};
  }
}

} // namespace game::systems

