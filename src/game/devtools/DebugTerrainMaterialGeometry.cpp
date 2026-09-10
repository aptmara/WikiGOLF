#include "DebugTerrainMaterialGeometry.h"

#include "../systems/TerrainGenerator.h"
#include <algorithm>

namespace game::debug {

void AppendTerrainMaterialLines(
    std::vector<DebugTerrainMaterialLine> &lines,
    const game::systems::TerrainData &terrain,
    const DirectX::XMFLOAT3 &worldPosition, float surfaceOffset, int stride) {
  const int resolutionX = terrain.config.resolutionX;
  const int resolutionZ = terrain.config.resolutionZ;
  if (resolutionX < 2 || resolutionZ < 2 ||
      terrain.heightMap.size() <
          static_cast<std::size_t>(resolutionX * resolutionZ) ||
      terrain.materialMap.size() <
          static_cast<std::size_t>(resolutionX * resolutionZ)) {
    return;
  }
  stride = (std::max)(1, stride);
  const auto point = [&](int x, int z) {
    const int index = z * resolutionX + x;
    return DirectX::XMFLOAT3{
        worldPosition.x - terrain.config.worldWidth * 0.5f +
            terrain.config.worldWidth * static_cast<float>(x) /
                static_cast<float>(resolutionX - 1),
        worldPosition.y + terrain.heightMap[index] + surfaceOffset,
        worldPosition.z - terrain.config.worldDepth * 0.5f +
            terrain.config.worldDepth * static_cast<float>(z) /
                static_cast<float>(resolutionZ - 1)};
  };
  for (int z = 0; z < resolutionZ; z += stride) {
    for (int x = 0; x < resolutionX; x += stride) {
      const uint8_t material = terrain.materialMap[z * resolutionX + x];
      if (x + stride < resolutionX) {
        lines.push_back({{point(x, z), point(x + stride, z)}, material});
      }
      if (z + stride < resolutionZ) {
        lines.push_back({{point(x, z), point(x, z + stride)}, material});
      }
    }
  }
}

} // namespace game::debug
