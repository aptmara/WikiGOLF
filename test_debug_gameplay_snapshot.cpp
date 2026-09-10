#include "src/game/devtools/DebugGameplaySnapshot.h"
#include "src/ecs/World.h"
#include "src/game/components/PhysicsComponents.h"
#include "src/game/components/Transform.h"
#include "src/game/components/WikiComponents.h"
#include <cmath>
#include <iostream>

int main() {
  ecs::World world;
  auto empty = game::debug::CaptureGameplaySnapshot(world);
  if (empty.golf.available || empty.shot.available || empty.ball.available) {
    return 1;
  }

  const ecs::Entity ball = world.CreateEntity();
  auto &transform = world.Add<game::components::Transform>(ball);
  transform.position = {1.0f, 2.0f, 3.0f};
  auto &body = world.Add<game::components::RigidBody>(ball);
  body.velocity = {3.0f, 0.0f, 4.0f};

  game::components::GolfGameState golf;
  golf.ballEntity = ball;
  golf.currentPage = "DirectX";
  golf.currentMaterial = game::components::TerrainMaterial::Bunker;
  golf.isBallGrounded = true;
  world.SetGlobal(golf);

  game::components::ShotState shot;
  shot.phase = game::components::ShotState::Phase::ImpactTiming;
  shot.judgement = game::components::ShotJudgement::Great;
  world.SetGlobal(shot);

  const auto data = game::debug::CaptureGameplaySnapshot(world);
  if (!data.golf.available || data.golf.material != "バンカー" ||
      !data.golf.grounded || !data.shot.available ||
      data.shot.phase != "インパクト入力" || data.shot.judgement != "GREAT" ||
      !data.ball.available || std::abs(data.ball.speed - 5.0f) > 0.0001f ||
      data.ball.position.y != 2.0f) {
    std::cerr << "Gameplay snapshot mismatch\n";
    return 1;
  }

  world.DestroyEntity(ball);
  if (game::debug::CaptureGameplaySnapshot(world).ball.available) {
    std::cerr << "Destroyed ball must not be inspected\n";
    return 1;
  }
  return 0;
}
