#pragma once

#include "../components/PhysicsComponents.h"

namespace game::systems {

inline components::CollisionEvent MakeSphereCollisionEvent(
    uint32_t entityA, uint32_t entityB,
    const DirectX::XMFLOAT3 &sphereCenter, float sphereRadius,
    const DirectX::XMFLOAT3 &normal, float penetrationDepth) {
  components::CollisionEvent event;
  event.entityA = entityA;
  event.entityB = entityB;
  event.normal = normal;
  event.penetrationDepth = penetrationDepth;
  event.contactPoint = {
      sphereCenter.x - normal.x * sphereRadius,
      sphereCenter.y - normal.y * sphereRadius,
      sphereCenter.z - normal.z * sphereRadius};
  return event;
}

} // namespace game::systems
