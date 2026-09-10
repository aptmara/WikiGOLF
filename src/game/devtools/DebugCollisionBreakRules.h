#pragma once

#include "../components/PhysicsComponents.h"
#include <cstdint>

namespace game::debug {

struct DebugCollisionBreakSettings {
  bool enabled = false;
  bool anyCollision = true;
  bool hole = false;
  bool terrain = false;
  bool entity = false;
  uint32_t entityId = 0;
};

bool MatchesCollisionBreakRule(
    const game::components::CollisionEvent &event,
    const DebugCollisionBreakSettings &settings, bool entityAIsHole,
    bool entityBIsHole, bool entityAIsTerrain, bool entityBIsTerrain);

} // namespace game::debug
