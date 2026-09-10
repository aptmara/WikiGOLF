#include "src/game/devtools/DebugTerrainRenderRules.h"
#include <iostream>

int main() {
  if (!game::debug::ShouldHideTerrainMesh(true, true) ||
      game::debug::ShouldHideTerrainMesh(false, true) ||
      game::debug::ShouldHideTerrainMesh(true, false)) {
    std::cerr << "Terrain-only rendering rule failed\n";
    return 1;
  }
  return 0;
}
