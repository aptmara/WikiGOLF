#pragma once

#include "DebugColliderGeometry.h"
#include <cstdint>
#include <vector>

namespace game::systems {
struct TerrainData;
}

namespace game::debug {

struct DebugTerrainMaterialLine {
  DebugLine3D line;
  uint8_t material = 0;
};

void AppendTerrainMaterialLines(
    std::vector<DebugTerrainMaterialLine> &lines,
    const game::systems::TerrainData &terrain,
    const DirectX::XMFLOAT3 &worldPosition, float surfaceOffset, int stride);

} // namespace game::debug
