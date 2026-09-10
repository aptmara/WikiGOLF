#include "DebugBallImpulse.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"
#include <cmath>

namespace game::debug {

DebugBallImpulseResult ApplyDebugBallImpulse(
    ecs::World &world, const DirectX::XMFLOAT3 &impulse) {
  const auto *state = world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    return DebugBallImpulseResult::MissingBall;
  }
  auto *body = world.Get<game::components::RigidBody>(
      static_cast<ecs::Entity>(state->ballEntity));
  if (!body) {
    return DebugBallImpulseResult::MissingBall;
  }
  if (std::fabs(body->mass) <= 0.000001f) {
    return DebugBallImpulseResult::ZeroMass;
  }
  body->velocity.x += impulse.x / body->mass;
  body->velocity.y += impulse.y / body->mass;
  body->velocity.z += impulse.z / body->mass;
  return DebugBallImpulseResult::Success;
}

} // namespace game::debug
