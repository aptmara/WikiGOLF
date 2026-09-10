#include "DebugEntitySnapshot.h"

#include "../../ecs/World.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"

namespace game::debug {
namespace {

const char *ColliderTypeName(game::components::ColliderType type) {
  switch (type) {
  case game::components::ColliderType::Sphere: return "Sphere";
  case game::components::ColliderType::Box: return "Box";
  case game::components::ColliderType::Cylinder: return "Cylinder";
  }
  return "Unknown";
}

} // namespace

DebugEntitySnapshot CaptureDebugEntitySnapshot(ecs::World &world,
                                               uint32_t entityId) {
  const ecs::Entity entity = static_cast<ecs::Entity>(entityId);
  DebugEntitySnapshot result;
  result.entity = entityId;
  result.alive = world.IsAlive(entity);
  if (!result.alive) {
    return result;
  }
  if (const auto *transform = world.Get<game::components::Transform>(entity)) {
    result.hasTransform = true;
    result.position = transform->position;
    result.rotation = transform->rotation;
    result.scale = transform->scale;
  }
  if (const auto *body = world.Get<game::components::RigidBody>(entity)) {
    result.hasRigidBody = true;
    result.velocity = body->velocity;
    result.acceleration = body->acceleration;
    result.angularVelocity = body->angularVelocity;
    result.mass = body->mass;
    result.drag = body->drag;
    result.rollingFriction = body->rollingFriction;
    result.restitution = body->restitution;
    result.spinDecay = body->spinDecay;
    result.isStatic = body->isStatic;
  }
  if (const auto *collider = world.Get<game::components::Collider>(entity)) {
    result.hasCollider = true;
    result.colliderType = ColliderTypeName(collider->type);
    result.colliderRadius = collider->radius;
    result.colliderSize = collider->size;
    result.colliderOffset = collider->offset;
  }
  if (const auto *heading = world.Get<game::components::Heading>(entity)) {
    result.hasHeading = true;
    result.headingText = heading->textSnippet;
    result.headingLink = heading->linkTarget;
    result.headingHealth = heading->currentHealth;
    result.headingMaximumHealth = heading->maxHealth;
    result.headingDestroyed = heading->isDestroyed;
  }
  if (const auto *hole = world.Get<game::components::GolfHole>(entity)) {
    result.hasGolfHole = true;
    result.holeLink = hole->linkTarget;
    result.holeRadius = hole->radius;
    result.holeGravity = hole->gravity;
    result.targetHole = hole->isTarget;
  }
  return result;
}

} // namespace game::debug
