#include "src/game/devtools/DebugCollisionHistory.h"
#include <iostream>

int main() {
  game::debug::DebugCollisionHistory history;
  game::components::CollisionEvents events;
  events.events.push_back({1, 2, {}, {}, 0.25f});
  if (history.Update(&events, false) || !history.Records().empty()) {
    std::cerr << "Paused frame recorded a collision\n";
    return 1;
  }
  if (!history.Update(&events, true) || history.Records().size() != 1 ||
      history.Records()[0].simulationFrame != 1) {
    std::cerr << "Advanced frame collision was not recorded\n";
    return 1;
  }

  for (std::size_t index = 0;
       index < game::debug::DebugCollisionHistory::kMaximumEvents + 5;
       ++index) {
    history.Update(&events, true);
  }
  if (history.Records().size() !=
          game::debug::DebugCollisionHistory::kMaximumEvents ||
      history.Records().back().event.penetrationDepth != 0.25f) {
    std::cerr << "Collision history capacity failed\n";
    return 1;
  }
  history.Clear();
  if (!history.Records().empty()) {
    std::cerr << "Collision history clear failed\n";
    return 1;
  }
  return 0;
}
