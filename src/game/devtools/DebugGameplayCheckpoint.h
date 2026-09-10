#pragma once

#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"

namespace ecs {
class World;
}

namespace game::debug {

class DebugGameplayCheckpoint {
public:
  bool Save(ecs::World &world);
  bool Restore(ecs::World &world) const;
  bool HasSnapshot() const { return m_saved; }

private:
  bool m_saved = false;
  uint32_t m_ballEntity = 0;
  game::components::Transform m_transform;
  game::components::RigidBody m_body;
  game::components::GolfGameState m_golf;
  game::components::ShotState m_shot;
};

} // namespace game::debug
