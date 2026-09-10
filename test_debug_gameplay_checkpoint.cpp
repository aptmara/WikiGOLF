#include "src/game/devtools/DebugGameplayCheckpoint.h"
#include "src/ecs/World.h"
#include <iostream>

int main() {
  ecs::World world;
  game::debug::DebugGameplayCheckpoint checkpoint;
  if (checkpoint.Save(world) || checkpoint.Restore(world)) {
    return 1;
  }
  const ecs::Entity ball = world.CreateEntity();
  auto &transform = world.Add<game::components::Transform>(ball);
  auto &body = world.Add<game::components::RigidBody>(ball);
  game::components::GolfGameState golf;
  golf.ballEntity = ball;
  golf.shotCount = 2;
  world.SetGlobal(golf);
  game::components::ShotState shot;
  shot.confirmedPower = 0.75f;
  world.SetGlobal(shot);
  transform.position = {1.0f, 2.0f, 3.0f};
  body.velocity = {4.0f, 5.0f, 6.0f};
  if (!checkpoint.Save(world)) {
    return 1;
  }
  transform.position = {};
  body.velocity = {};
  world.GetGlobal<game::components::GolfGameState>()->shotCount = 9;
  world.GetGlobal<game::components::ShotState>()->confirmedPower = 0.0f;
  if (!checkpoint.Restore(world) || transform.position.y != 2.0f ||
      body.velocity.z != 6.0f ||
      world.GetGlobal<game::components::GolfGameState>()->shotCount != 2 ||
      world.GetGlobal<game::components::ShotState>()->confirmedPower != 0.75f) {
    std::cerr << "Checkpoint restore failed\n";
    return 1;
  }
  world.GetGlobal<game::components::GolfGameState>()->ballEntity =
      world.CreateEntity();
  if (checkpoint.Restore(world)) {
    std::cerr << "Checkpoint restored into a different ball\n";
    return 1;
  }
  return 0;
}
