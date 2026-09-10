#include "DebugSlopeStatus.h"

#include "../../ecs/World.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../systems/PhysicsSystemInternals.h"

namespace game::debug {

DebugSlopeStatus CaptureSlopeStatus(ecs::World &world) {
  using namespace game::components;
  auto *golf = world.GetGlobal<GolfGameState>();
  if (!golf) {
    return {};
  }
  const ecs::Entity ball = static_cast<ecs::Entity>(golf->ballEntity);
  const auto *ballTransform = world.Get<Transform>(ball);
  if (!ballTransform) {
    return {};
  }

  DebugSlopeStatus result;
  world.Query<TerrainCollider, Transform>().Each(
      [&](ecs::Entity, TerrainCollider &terrain, Transform &terrainTransform) {
        if (result.available || !terrain.data) {
          return;
        }
        const auto sample = game::systems::SampleTerrainAt(
            *terrain.data, ballTransform->position.x - terrainTransform.position.x,
            ballTransform->position.z - terrainTransform.position.z);
        if (!sample.valid) {
          return;
        }
        result.available = true;
        result.evaluation = EvaluateSlope(
            golf->isBallGrounded, true,
            DirectX::XMVectorGetY(sample.normal));
      });
  return result;
}

} // namespace game::debug
