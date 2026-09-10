#include "DebugBallPhysicsTuning.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/WikiComponents.h"

namespace game::debug {
namespace {

game::components::RigidBody *FindBallBody(ecs::World &world) {
  const auto *state = world.GetGlobal<game::components::GolfGameState>();
  if (!state) {
    return nullptr;
  }
  return world.Get<game::components::RigidBody>(
      static_cast<ecs::Entity>(state->ballEntity));
}

} // namespace

bool CaptureDebugBallPhysics(ecs::World &world,
                             DebugBallPhysicsValues &values) {
  const auto *body = FindBallBody(world);
  if (!body) {
    return false;
  }
  values.mass = body->mass;
  values.drag = body->drag;
  values.rollingFriction = body->rollingFriction;
  values.restitution = body->restitution;
  values.spinDecay = body->spinDecay;
  return true;
}

bool ApplyDebugBallPhysics(ecs::World &world,
                           const DebugBallPhysicsValues &values) {
  auto *body = FindBallBody(world);
  if (!body) {
    return false;
  }
  body->mass = values.mass;
  body->drag = values.drag;
  body->rollingFriction = values.rollingFriction;
  body->restitution = values.restitution;
  body->spinDecay = values.spinDecay;
  return true;
}

} // namespace game::debug
