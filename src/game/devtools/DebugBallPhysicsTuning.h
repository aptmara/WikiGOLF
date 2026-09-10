#pragma once

namespace ecs {
class World;
}

namespace game::debug {

struct DebugBallPhysicsValues {
  float mass = 1.0f;
  float drag = 0.01f;
  float rollingFriction = 0.5f;
  float restitution = 0.5f;
  float spinDecay = 0.5f;
};

bool CaptureDebugBallPhysics(ecs::World &world,
                             DebugBallPhysicsValues &values);
bool ApplyDebugBallPhysics(ecs::World &world,
                           const DebugBallPhysicsValues &values);

} // namespace game::debug
