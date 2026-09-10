#include "DebugCupInStatus.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../scenes/CupInUtils.h"
#include <cmath>
#include <limits>

namespace game::debug {

DebugCupInStatus EvaluateCupInStatus(const DirectX::XMFLOAT3 &ballPosition,
                                     const DirectX::XMFLOAT3 &holePosition,
                                     float holeRadius,
                                     const DirectX::XMFLOAT3 &velocity,
                                     bool targetHole, int shotCount) {
  const float dx = ballPosition.x - holePosition.x;
  const float dz = ballPosition.z - holePosition.z;
  const float distanceSquared = dx * dx + dz * dz;
  const float speedSquared = velocity.x * velocity.x +
                             velocity.y * velocity.y +
                             velocity.z * velocity.z;
  DebugCupInStatus result;
  result.available = true;
  result.targetHole = targetHole;
  result.horizontalDistance = std::sqrt(distanceSquared);
  result.captureRadius = holeRadius * 0.9f;
  result.verticalOffset = ballPosition.y - holePosition.y;
  result.speed = std::sqrt(speedSquared);
  result.withinHorizontalRange =
      distanceSquared <= result.captureRadius * result.captureRadius;
  result.withinVerticalRange =
      result.verticalOffset < 0.0f && result.verticalOffset > -1.0f;
  result.slowEnough = speedSquared < 0.01f;
  result.readyForCupIn = game::scenes::cupin::IsBallReadyForCupIn(
      ballPosition, holePosition, holeRadius, speedSquared);
  result.holeInOne = result.readyForCupIn && targetHole && shotCount == 1;
  return result;
}

DebugCupInStatus CaptureCupInStatus(ecs::World &world) {
  using namespace game::components;
  auto *golf = world.GetGlobal<GolfGameState>();
  if (!golf) {
    return {};
  }
  const ecs::Entity ball = static_cast<ecs::Entity>(golf->ballEntity);
  const auto *ballTransform = world.Get<Transform>(ball);
  const auto *body = world.Get<RigidBody>(ball);
  if (!ballTransform || !body) {
    return {};
  }

  DebugCupInStatus nearest;
  float nearestDistanceSquared = (std::numeric_limits<float>::max)();
  for (uint32_t id : golf->holes) {
    const ecs::Entity entity = static_cast<ecs::Entity>(id);
    const auto *holeTransform = world.Get<Transform>(entity);
    const auto *hole = world.Get<GolfHole>(entity);
    if (!holeTransform || !hole) {
      continue;
    }
    const float dx = ballTransform->position.x - holeTransform->position.x;
    const float dz = ballTransform->position.z - holeTransform->position.z;
    const float distanceSquared = dx * dx + dz * dz;
    if (distanceSquared >= nearestDistanceSquared) {
      continue;
    }
    nearestDistanceSquared = distanceSquared;
    nearest = EvaluateCupInStatus(
        ballTransform->position, holeTransform->position, hole->radius,
        body->velocity, hole->isTarget, golf->shotCount);
    nearest.holeEntity = id;
    nearest.linkTarget = hole->linkTarget;
  }
  return nearest;
}

} // namespace game::debug
