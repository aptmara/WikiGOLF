#include "src/game/devtools/DebugBallTelemetryHistory.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  const ecs::Entity ball = world.CreateEntity();
  auto &body = world.Add<game::components::RigidBody>(ball);
  body.velocity = {3.0f, 4.0f, 0.0f};
  body.angularVelocity = {0.0f, 0.0f, 2.0f};
  game::components::GolfGameState state;
  state.ballEntity = ball;
  state.isBallGrounded = true;
  world.SetGlobal(state);
  game::debug::DebugBallTelemetryHistory history;
  if (history.Update(world, false) || !history.Samples().empty()) {
    return 1;
  }
  if (!history.Update(world, true) || history.SpeedSeries()[0] != 5.0f ||
      history.VerticalVelocitySeries()[0] != 4.0f ||
      history.AngularSpeedSeries()[0] != 2.0f ||
      history.GroundedSeries()[0] != 1.0f) {
    std::cerr << "Ball telemetry capture failed\n";
    return 1;
  }
  for (std::size_t index = 0;
       index < game::debug::DebugBallTelemetryHistory::kMaximumFrames;
       ++index) {
    history.Update(world, true);
  }
  if (history.Samples().size() !=
      game::debug::DebugBallTelemetryHistory::kMaximumFrames) {
    std::cerr << "Ball telemetry capacity failed\n";
    return 1;
  }
  return 0;
}
