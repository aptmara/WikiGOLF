#include "src/game/devtools/DebugBallTrailHistory.h"
#include <iostream>

int main() {
  game::debug::DebugBallTrailHistory trail;
  for (int frame = 0; frame < 8; ++frame) {
    trail.Update(1, {static_cast<float>(frame), 0.0f, 0.0f}, 2, 3);
  }
  if (trail.Points().size() != 3 || trail.Points().front().x != 2.0f ||
      trail.Points().back().x != 6.0f) {
    std::cerr << "Trail sampling or capacity failed\n";
    return 1;
  }
  trail.Update(2, {10.0f, 0.0f, 0.0f}, 2, 3);
  if (trail.Points().size() != 1 || trail.Points().front().x != 10.0f) {
    std::cerr << "Trail did not reset for a new ball\n";
    return 1;
  }
  trail.Clear();
  if (!trail.Points().empty()) {
    return 1;
  }
  return 0;
}
