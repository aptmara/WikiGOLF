#pragma once

#include "../systems/TerrainGenerator.h"
#include <DirectXMath.h>
#include <algorithm>
#include <cstddef>

namespace game::utils {

inline constexpr float kTerrainBoundsInset = 0.01f;

struct TerrainWorldBounds {
  float minX = 0.0f;
  float maxX = 0.0f;
  float minZ = 0.0f;
  float maxZ = 0.0f;

  bool Contains(float x, float z) const {
    return x >= minX && x <= maxX && z >= minZ && z <= maxZ;
  }
};

inline bool HasValidTerrainGrid(const game::systems::TerrainData &terrain) {
  const auto &config = terrain.config;
  if (config.resolutionX < 2 || config.resolutionZ < 2 ||
      config.worldWidth <= 0.0f || config.worldDepth <= 0.0f) {
    return false;
  }

  const std::size_t requiredSize =
      static_cast<std::size_t>(config.resolutionX) *
      static_cast<std::size_t>(config.resolutionZ);
  return terrain.heightMap.size() >= requiredSize;
}

inline TerrainWorldBounds
CalculateTerrainWorldBounds(const game::systems::TerrainData &terrain) {
  const float halfWidth = terrain.config.worldWidth * 0.5f;
  const float halfDepth = terrain.config.worldDepth * 0.5f;
  const float insetX = (std::min)(kTerrainBoundsInset, halfWidth * 0.5f);
  const float insetZ = (std::min)(kTerrainBoundsInset, halfDepth * 0.5f);
  return {-halfWidth + insetX, halfWidth - insetX,
          -halfDepth + insetZ, halfDepth - insetZ};
}

inline DirectX::XMFLOAT3
ClampToTerrainBounds(const DirectX::XMFLOAT3 &position,
                     const TerrainWorldBounds &bounds) {
  DirectX::XMFLOAT3 result = position;
  result.x = std::clamp(result.x, bounds.minX, bounds.maxX);
  result.z = std::clamp(result.z, bounds.minZ, bounds.maxZ);
  return result;
}

inline float SampleTerrainHeight(const game::systems::TerrainData &terrain,
                                 float x, float z) {
  if (!HasValidTerrainGrid(terrain)) {
    return 0.0f;
  }

  const auto &config = terrain.config;
  const float u = std::clamp(x / config.worldWidth + 0.5f, 0.0f, 1.0f);
  const float v = std::clamp(0.5f - z / config.worldDepth, 0.0f, 1.0f);
  const float fx = u * static_cast<float>(config.resolutionX - 1);
  const float fz = v * static_cast<float>(config.resolutionZ - 1);
  const int ix = std::clamp(static_cast<int>(fx), 0,
                            config.resolutionX - 2);
  const int iz = std::clamp(static_cast<int>(fz), 0,
                            config.resolutionZ - 2);
  const float dx = fx - static_cast<float>(ix);
  const float dz = fz - static_cast<float>(iz);

  const float h00 = terrain.heightMap[iz * config.resolutionX + ix];
  const float h10 = terrain.heightMap[iz * config.resolutionX + ix + 1];
  const float h01 =
      terrain.heightMap[(iz + 1) * config.resolutionX + ix];
  const float h11 =
      terrain.heightMap[(iz + 1) * config.resolutionX + ix + 1];
  const float h0 = h00 * (1.0f - dx) + h10 * dx;
  const float h1 = h01 * (1.0f - dx) + h11 * dx;
  return h0 * (1.0f - dz) + h1 * dz;
}

} // namespace game::utils
