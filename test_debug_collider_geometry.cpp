#include "src/game/devtools/DebugColliderGeometry.h"
#include <cmath>
#include <iostream>
#include <vector>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

int main() {
  using game::debug::DebugLine3D;
  std::vector<DebugLine3D> lines;

  game::debug::AppendSphereLines(lines, {1.0f, 2.0f, 3.0f}, 2.0f, 8);
  CHECK(lines.size() == 24, "sphere creates three closed rings");
  CHECK(std::fabs(lines.front().from.x - 1.0f) < 0.00001f &&
            std::fabs(lines.front().from.y - 4.0f) < 0.00001f,
        "sphere lines use the requested center and radius");

  lines.clear();
  game::debug::AppendBoxLines(lines, {0.0f, 0.0f, 0.0f},
                              {2.0f, 4.0f, 6.0f},
                              {0.0f, 0.0f, 0.0f, 1.0f});
  CHECK(lines.size() == 12, "box creates twelve edges");
  CHECK(std::fabs(lines.front().from.x + 1.0f) < 0.00001f &&
            std::fabs(lines.front().from.y + 2.0f) < 0.00001f &&
            std::fabs(lines.front().from.z + 3.0f) < 0.00001f,
        "box edges use half extents");

  lines.clear();
  game::debug::AppendCylinderLines(lines, {0.0f, 0.0f, 0.0f}, 2.0f, 4.0f,
                                   {0.0f, 0.0f, 0.0f, 1.0f}, 8);
  CHECK(lines.size() == 24,
        "cylinder creates two rings and vertical supports");

  lines.clear();
  game::debug::AppendVectorArrow(lines, {1.0f, 2.0f, 3.0f},
                                 {5.0f, 0.0f, 0.0f}, 0.2f);
  CHECK(lines.size() == 3, "velocity vector creates a shaft and arrow head");
  CHECK(std::fabs(lines.front().to.x - 2.0f) < 0.00001f &&
            std::fabs(lines.front().to.y - 2.0f) < 0.00001f,
        "velocity vector applies display scale");
  lines.clear();
  game::debug::AppendVectorArrow(lines, {}, {}, 0.2f);
  CHECK(lines.empty(), "zero velocity does not create an arrow");
  return 0;
}
