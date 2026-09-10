#include "src/game/devtools/DebugEntitySnapshot.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/Transform.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  const ecs::Entity entity = world.CreateEntity();
  auto &transform = world.Add<game::components::Transform>(entity);
  transform.position = {1.0f, 2.0f, 3.0f};
  auto &body = world.Add<game::components::RigidBody>(entity);
  body.velocity = {4.0f, 5.0f, 6.0f};
  auto &collider = world.Add<game::components::Collider>(entity);
  collider.type = game::components::ColliderType::Box;
  auto &heading = world.Add<game::components::Heading>(entity);
  heading.currentHealth = 2;
  heading.maxHealth = 3;
  heading.linkTarget = "Direct3D";
  auto &hole = world.Add<game::components::GolfHole>(entity);
  hole.isTarget = true;

  const auto data = game::debug::CaptureDebugEntitySnapshot(world, entity);
  if (!data.alive || !data.hasTransform || data.position.y != 2.0f ||
      !data.hasRigidBody || data.velocity.z != 6.0f || !data.hasCollider ||
      data.colliderType != "Box" || !data.hasHeading ||
      data.headingHealth != 2 || !data.hasGolfHole || !data.targetHole) {
    std::cerr << "Entity snapshot mismatch\n";
    return 1;
  }
  world.DestroyEntity(entity);
  if (game::debug::CaptureDebugEntitySnapshot(world, entity).alive) {
    std::cerr << "Destroyed entity was reported alive\n";
    return 1;
  }
  return 0;
}
