#include "src/game/devtools/DebugTerrainMaterialGeometry.h"
#include "src/game/systems/TerrainGenerator.h"
#include <iostream>

int main() {
  game::systems::TerrainData terrain;
  terrain.config.resolutionX = 2;
  terrain.config.resolutionZ = 2;
  terrain.config.worldWidth = 10.0f;
  terrain.config.worldDepth = 20.0f;
  terrain.heightMap = {0.0f, 1.0f, 2.0f, 3.0f};
  terrain.materialMap = {0, 1, 2, 3};
  std::vector<game::debug::DebugTerrainMaterialLine> lines;
  game::debug::AppendTerrainMaterialLines(lines, terrain, {1.0f, 2.0f, 3.0f},
                                          0.1f, 1);
  if (lines.size() != 4 || lines[0].material != 0 ||
      lines[0].line.from.x != -4.0f || lines[0].line.from.y != 2.1f ||
      lines[0].line.to.x != 6.0f) {
    std::cerr << "Terrain material line generation failed\n";
    return 1;
  }
  return 0;
}
