#include "src/game/systems/CollisionDebugInfo.h"
#include <cmath>
#include <iostream>

int main() {
  const auto event = game::systems::MakeSphereCollisionEvent(
      12, 34, {2.0f, 3.0f, 4.0f}, 0.5f, {1.0f, 0.0f, 0.0f}, 0.125f);
  const bool valid =
      event.entityA == 12 && event.entityB == 34 &&
      std::fabs(event.contactPoint.x - 1.5f) < 0.000001f &&
      std::fabs(event.contactPoint.y - 3.0f) < 0.000001f &&
      std::fabs(event.contactPoint.z - 4.0f) < 0.000001f &&
      std::fabs(event.normal.x - 1.0f) < 0.000001f &&
      std::fabs(event.penetrationDepth - 0.125f) < 0.000001f;
  if (!valid) {
    std::cerr << "Collision debug contact data was not preserved\n";
    return 1;
  }

  const auto terrainEvent = game::systems::MakeSphereCollisionEvent(
      12, 99, {0.0f, 0.45f, 0.0f}, 0.5f, {0.0f, 1.0f, 0.0f}, 0.05f);
  if (terrainEvent.entityB != 99 ||
      std::fabs(terrainEvent.contactPoint.y + 0.05f) > 0.000001f ||
      std::fabs(terrainEvent.normal.y - 1.0f) > 0.000001f) {
    std::cerr << "Terrain contact debug data was not preserved\n";
    return 1;
  }
  return 0;
}
