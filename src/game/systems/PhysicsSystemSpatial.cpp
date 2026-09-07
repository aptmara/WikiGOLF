/**
 * @file PhysicsSystemSpatial.cpp
 * @brief 物理システムの空間キャッシュ実装
*/

#include "PhysicsSystemInternals.h"
#include <array>
#include <cmath>

namespace game::systems {

int64_t MakeGridKey(int x, int z) {
  return (static_cast<int64_t>(x) << 32) ^
         (static_cast<uint32_t>(z) & 0xffffffffu);
}

int GridCoord(float value, float cellSize) {
  return static_cast<int>(std::floor(value / cellSize));
}

void HoleSpatialGrid::Build(const std::vector<HoleInfo> &source) {
  holes = source;
  cells.clear();
  cells.reserve(holes.size() * 2 + 1);
  for (uint32_t index = 0; index < static_cast<uint32_t>(holes.size());
       ++index) {
    int cellX = GridCoord(holes[index].positionFloat.x, cellSize);
    int cellZ = GridCoord(holes[index].positionFloat.z, cellSize);
    cells[MakeGridKey(cellX, cellZ)].push_back(index);
  }
}

void StaticBodySpatialGrid::Build(ecs::World &world,
                                  const std::vector<ecs::Entity> &source) {
  bodies = source;
  cells.clear();
  cells.reserve(source.size() * 2 + 1);
  for (uint32_t index = 0; index < static_cast<uint32_t>(source.size());
       ++index) {
    auto *transform = world.Get<Transform>(source[index]);
    auto *rigidBody = world.Get<RigidBody>(source[index]);
    auto *collider = world.Get<Collider>(source[index]);
    if (!transform || !rigidBody || !rigidBody->isStatic || !collider ||
        collider->type != ColliderType::Box) {
      continue;
    }
    float halfX = std::abs(collider->size.x * transform->scale.x) * 0.5f;
    float halfZ = std::abs(collider->size.z * transform->scale.z) * 0.5f;
    int minX = GridCoord(transform->position.x - halfX, cellSize);
    int maxX = GridCoord(transform->position.x + halfX, cellSize);
    int minZ = GridCoord(transform->position.z - halfZ, cellSize);
    int maxZ = GridCoord(transform->position.z + halfZ, cellSize);
    for (int cellZ = minZ; cellZ <= maxZ; ++cellZ) {
      for (int cellX = minX; cellX <= maxX; ++cellX) {
        cells[MakeGridKey(cellX, cellZ)].push_back(index);
      }
    }
  }
}

float GetJitterFromTable(uint32_t &cursor, float amplitude) {
  static constexpr std::array<float, 32> kJitterTable = {
      -0.47f, 0.12f,  0.38f,  -0.21f, 0.04f,  0.49f,  -0.34f, 0.27f,
      -0.08f, 0.31f,  -0.42f, 0.18f,  -0.16f, 0.44f,  -0.29f, 0.06f,
      0.23f,  -0.36f, 0.41f,  -0.02f, -0.25f, 0.15f,  0.33f,  -0.45f,
      0.09f,  -0.11f, 0.46f,  -0.31f, 0.21f,  -0.39f, 0.02f,  0.28f};
  float jitter = kJitterTable[cursor % kJitterTable.size()];
  ++cursor;
  return 1.0f + jitter * amplitude;
}

} // namespace game::systems
