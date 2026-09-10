#include "DebugCollisionBreakRules.h"

namespace game::debug {

bool MatchesCollisionBreakRule(
    const game::components::CollisionEvent &event,
    const DebugCollisionBreakSettings &settings, bool entityAIsHole,
    bool entityBIsHole, bool entityAIsTerrain, bool entityBIsTerrain) {
  if (!settings.enabled) {
    return false;
  }
  if (settings.anyCollision) {
    return true;
  }
  if (settings.hole && (entityAIsHole || entityBIsHole)) {
    return true;
  }
  if (settings.terrain && (entityAIsTerrain || entityBIsTerrain)) {
    return true;
  }
  return settings.entity &&
         (event.entityA == settings.entityId || event.entityB == settings.entityId);
}

} // namespace game::debug
