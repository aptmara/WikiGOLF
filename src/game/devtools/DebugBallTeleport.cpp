#include "DebugBallTeleport.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"

namespace game::debug {

bool CaptureDebugBallPosition(ecs::World &world,
                              DirectX::XMFLOAT3 &position) {
  const auto *state = world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    return false;
  }
  const auto *transform = world.Get<game::components::Transform>(
      static_cast<ecs::Entity>(state->ballEntity));
  if (!transform) {
    return false;
  }
  position = transform->position;
  return true;
}

DebugBallTeleportResult TeleportDebugBall(
    ecs::World &world, const DirectX::XMFLOAT3 &position, bool resetMotion) {
  auto *state = world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    return DebugBallTeleportResult::MissingGameState;
  }
  const ecs::Entity ball = static_cast<ecs::Entity>(state->ballEntity);
  auto *transform = world.Get<game::components::Transform>(ball);
  auto *body = world.Get<game::components::RigidBody>(ball);
  if (!transform || !body) {
    return DebugBallTeleportResult::MissingBallComponents;
  }

  transform->position = position;
  state->isBallGrounded = false;
  if (resetMotion) {
    body->velocity = {};
    body->acceleration = {};
    body->angularVelocity = {};
    state->currentBallSpeed = 0.0f;
  }
  return DebugBallTeleportResult::Success;
}

} // namespace game::debug
