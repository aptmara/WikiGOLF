#include "src/game/devtools/DebugBallPhysicsTuning.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  game::debug::DebugBallPhysicsValues values;
  if (game::debug::CaptureDebugBallPhysics(world, values) ||
      game::debug::ApplyDebugBallPhysics(world, values)) {
    std::cerr << "Missing ball must not be edited\n";
    return 1;
  }

  const ecs::Entity ball = world.CreateEntity();
  auto &body = world.Add<game::components::RigidBody>(ball);
  game::components::GolfGameState state;
  state.ballEntity = ball;
  world.SetGlobal(state);
  body.mass = 2.5f;
  body.drag = 0.2f;
  if (!game::debug::CaptureDebugBallPhysics(world, values) ||
      values.mass != 2.5f || values.drag != 0.2f) {
    std::cerr << "Physics capture failed\n";
    return 1;
  }

  values = {-2.0f, 0.3f, 0.4f, 1.2f, 0.6f};
  if (!game::debug::ApplyDebugBallPhysics(world, values) ||
      body.mass != -2.0f || body.drag != 0.3f ||
      body.rollingFriction != 0.4f || body.restitution != 1.2f ||
      body.spinDecay != 0.6f) {
    std::cerr << "Physics values were not applied exactly\n";
    return 1;
  }
  return 0;
}
