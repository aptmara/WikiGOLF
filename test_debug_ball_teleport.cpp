#include "src/game/devtools/DebugBallTeleport.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/Transform.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  if (game::debug::TeleportDebugBall(world, {}, true) !=
      game::debug::DebugBallTeleportResult::MissingGameState) {
    return 1;
  }

  const ecs::Entity ball = world.CreateEntity();
  auto &transform = world.Add<game::components::Transform>(ball);
  auto &body = world.Add<game::components::RigidBody>(ball);
  transform.position = {1.0f, 2.0f, 3.0f};
  body.velocity = {4.0f, 5.0f, 6.0f};
  body.acceleration = {1.0f, 1.0f, 1.0f};
  body.angularVelocity = {2.0f, 2.0f, 2.0f};
  game::components::GolfGameState state;
  state.ballEntity = ball;
  state.isBallGrounded = true;
  state.currentBallSpeed = 9.0f;
  world.SetGlobal(state);

  DirectX::XMFLOAT3 captured;
  if (!game::debug::CaptureDebugBallPosition(world, captured) ||
      captured.y != 2.0f) {
    std::cerr << "Ball position capture failed\n";
    return 1;
  }
  const auto result =
      game::debug::TeleportDebugBall(world, {7.0f, 8.0f, 9.0f}, true);
  const auto *updatedState =
      world.GetGlobal<game::components::GolfGameState>();
  if (result != game::debug::DebugBallTeleportResult::Success ||
      transform.position.x != 7.0f || body.velocity.x != 0.0f ||
      body.acceleration.y != 0.0f || body.angularVelocity.z != 0.0f ||
      updatedState->isBallGrounded || updatedState->currentBallSpeed != 0.0f) {
    std::cerr << "Ball teleport reset failed\n";
    return 1;
  }

  body.velocity = {3.0f, 0.0f, 0.0f};
  game::debug::TeleportDebugBall(world, {0.0f, 0.0f, 0.0f}, false);
  if (body.velocity.x != 3.0f) {
    std::cerr << "Motion preservation failed\n";
    return 1;
  }
  return 0;
}
