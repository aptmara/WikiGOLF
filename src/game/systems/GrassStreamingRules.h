#pragma once

#include <algorithm>
#include <cstdint>

namespace game::systems {

struct GrassStreamingBounds {
  float minX = 0.0f;
  float maxX = 0.0f;
  float minZ = 0.0f;
  float maxZ = 0.0f;
};

inline uint32_t MakeGrassCellSeed(uint32_t fieldSeed, int row, int column) {
  return fieldSeed ^ static_cast<uint32_t>(row) * 374761393u ^
         static_cast<uint32_t>(column) * 668265263u;
}

inline uint32_t MixGrassSeed(uint32_t value) {
  value ^= value >> 16u;
  value *= 0x7feb352du;
  value ^= value >> 15u;
  value *= 0x846ca68bu;
  return value ^ (value >> 16u);
}

inline float GrassRandom01(uint32_t cellSeed, uint32_t sampleIndex) {
  const uint32_t bits = MixGrassSeed(cellSeed + sampleIndex * 0x9e3779b9u);
  return static_cast<float>(bits >> 8u) * (1.0f / 16777216.0f);
}

inline uint64_t MakeGrassStreamChunkKey(int chunkX, int chunkZ) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(chunkX)) << 32u) |
         static_cast<uint32_t>(chunkZ);
}

inline int GrassStreamChunkX(uint64_t key) {
  return static_cast<int32_t>(key >> 32u);
}

inline int GrassStreamChunkZ(uint64_t key) {
  return static_cast<int32_t>(key & 0xffffffffu);
}

inline GrassStreamingBounds CalculateGrassStreamingBounds(
    float fieldWidth, float fieldDepth, float centerX, float centerZ,
    float drawDistance, float padding) {
  const float radius = std::max(0.0f, drawDistance) + std::max(0.0f, padding);
  const float halfWidth = std::max(0.0f, fieldWidth) * 0.5f;
  const float halfDepth = std::max(0.0f, fieldDepth) * 0.5f;

  GrassStreamingBounds bounds;
  bounds.minX = std::clamp(centerX - radius, -halfWidth, halfWidth);
  bounds.maxX = std::clamp(centerX + radius, -halfWidth, halfWidth);
  bounds.minZ = std::clamp(centerZ - radius, -halfDepth, halfDepth);
  bounds.maxZ = std::clamp(centerZ + radius, -halfDepth, halfDepth);
  return bounds;
}

} // namespace game::systems
