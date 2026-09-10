#include "src/game/devtools/DebugBallImpulse.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  if (game::debug::ApplyDebugBallImpulse(world, {}) !=
      game::debug::DebugBallImpulseResult::MissingBall) {
    return 1;
  }
  const ecs::Entity ball = world.CreateEntity();
  auto &body = world.Add<game::components::RigidBody>(ball);
  body.mass = 2.0f;
  body.velocity = {1.0f, 2.0f, 3.0f};
  game::components::GolfGameState state;
  state.ballEntity = ball;
  world.SetGlobal(state);
  if (game::debug::ApplyDebugBallImpulse(world, {2.0f, 4.0f, -2.0f}) !=
          game::debug::DebugBallImpulseResult::Success ||
      body.velocity.x != 2.0f || body.velocity.y != 4.0f ||
      body.velocity.z != 2.0f) {
    std::cerr << "Impulse calculation failed\n";
    return 1;
  }
  body.mass = 0.0f;
  if (game::debug::ApplyDebugBallImpulse(world, {1.0f, 0.0f, 0.0f}) !=
      game::debug::DebugBallImpulseResult::ZeroMass) {
    std::cerr << "Zero mass was not rejected\n";
    return 1;
  }
  return 0;
}
