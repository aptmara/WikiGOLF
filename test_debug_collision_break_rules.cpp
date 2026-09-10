#include "src/game/devtools/DebugCollisionBreakRules.h"
#include <iostream>

int main() {
  const game::components::CollisionEvent event{12, 34};
  game::debug::DebugCollisionBreakSettings settings;
  if (game::debug::MatchesCollisionBreakRule(event, settings, true, false,
                                              false, false)) {
    return 1;
  }
  settings.enabled = true;
  if (!game::debug::MatchesCollisionBreakRule(event, settings, false, false,
                                               false, false)) {
    std::cerr << "Any collision rule failed\n";
    return 1;
  }
  settings.anyCollision = false;
  settings.hole = true;
  if (!game::debug::MatchesCollisionBreakRule(event, settings, false, true,
                                               false, false)) {
    std::cerr << "Hole collision rule failed\n";
    return 1;
  }
  settings.hole = false;
  settings.entity = true;
  settings.entityId = 34;
  if (!game::debug::MatchesCollisionBreakRule(event, settings, false, false,
                                               false, false)) {
    std::cerr << "Entity collision rule failed\n";
    return 1;
  }
  settings.entity = false;
  settings.terrain = true;
  if (!game::debug::MatchesCollisionBreakRule(event, settings, false, false,
                                               true, false)) {
    std::cerr << "Terrain collision rule failed\n";
    return 1;
  }
  return 0;
}
