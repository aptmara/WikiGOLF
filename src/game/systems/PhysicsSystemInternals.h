#pragma once
/**
 * @file PhysicsSystemInternals.h
 * @brief 物理システムの分割実装で共有する内部型と関数
 */

#include "PhysicsSystem.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "TerrainGenerator.h"
#include "../../ecs/World.h"
#include <algorithm>
#include <cstdint>
#include <DirectXMath.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief 地形から取得した物理情報です。
 */
struct TerrainSample {
  bool valid = false;
  float height = 0.0f;
  XMVECTOR normal = XMVectorSet(0, 1, 0, 0);
  uint8_t material = 0;
};

/**
 * @brief 更新対象の動的剛体をまとめた参照情報です。
 */
struct BodyInfo {
  ecs::Entity entity = UINT32_MAX;
  Transform *t = nullptr;
  RigidBody *rb = nullptr;
  Collider *c = nullptr;
};

/**
 * @brief ホールの吸引判定に必要な情報です。
 */
struct HoleInfo {
  ecs::Entity entity = UINT32_MAX;
  XMVECTOR position = XMVectorZero();
  XMFLOAT3 positionFloat = {0.0f, 0.0f, 0.0f};
  float radius = 0.0f;
  float gravity = 0.0f;
  float suctionRange = 0.0f;
};

int64_t MakeGridKey(int x, int z);
int GridCoord(float value, float cellSize);

/**
 * @brief ホールを格子状に分類して近傍検索を高速化します。
 */
struct HoleSpatialGrid {
  float cellSize = 5.0f;
  std::vector<HoleInfo> holes;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;

  void Build(const std::vector<HoleInfo> &source);

  template <typename Func>
  void Query(float x, float z, float radius, Func &&func) const {
    int minX = GridCoord(x - radius, cellSize);
    int maxX = GridCoord(x + radius, cellSize);
    int minZ = GridCoord(z - radius, cellSize);
    int maxZ = GridCoord(z + radius, cellSize);
    for (int cellZ = minZ; cellZ <= maxZ; ++cellZ) {
      for (int cellX = minX; cellX <= maxX; ++cellX) {
        auto it = cells.find(MakeGridKey(cellX, cellZ));
        if (it == cells.end()) {
          continue;
        }
        for (uint32_t index : it->second) {
          func(holes[index]);
        }
      }
    }
  }
};

/**
 * @brief 静的ボックスを格子状に分類して近傍検索を高速化します。
 */
struct StaticBodySpatialGrid {
  float cellSize = 8.0f;
  std::vector<ecs::Entity> bodies;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;

  void Build(ecs::World &world, const std::vector<ecs::Entity> &source);

  template <typename Func>
  void Query(float x, float z, float radius, Func &&func) const {
    std::vector<uint32_t> emitted;
    int minX = GridCoord(x - radius, cellSize);
    int maxX = GridCoord(x + radius, cellSize);
    int minZ = GridCoord(z - radius, cellSize);
    int maxZ = GridCoord(z + radius, cellSize);
    for (int cellZ = minZ; cellZ <= maxZ; ++cellZ) {
      for (int cellX = minX; cellX <= maxX; ++cellX) {
        auto it = cells.find(MakeGridKey(cellX, cellZ));
        if (it == cells.end()) {
          continue;
        }
        for (uint32_t index : it->second) {
          if (std::find(emitted.begin(), emitted.end(), index) !=
              emitted.end()) {
            continue;
          }
          emitted.push_back(index);
          func(bodies[index]);
        }
      }
    }
  }
};

/**
 * @brief 物理空間の再構築条件と検索データを保持します。
 */
struct PhysicsSpatialCache {
  const TerrainData *terrainIdentity = nullptr;
  std::string pageIdentity;
  size_t holeCount = (std::numeric_limits<size_t>::max)();
  ecs::Entity firstHole = ecs::NULL_ENTITY;
  ecs::Entity lastHole = ecs::NULL_ENTITY;
  HoleSpatialGrid holeGrid;
  StaticBodySpatialGrid staticBodyGrid;
  float maxHoleQueryRange = 0.5f;
};

/**
 * @brief 物理更新中に収集する計測値です。
 */
struct PhysicsPerfStats {
  uint32_t terrainSamples = 0;
  uint32_t holeCandidates = 0;
  uint32_t staticCandidates = 0;
  uint32_t staticChecks = 0;
};

bool IsNaN(float value);
bool IsVectorNaN(XMVECTOR value);
XMVECTOR SafeNormalize(XMVECTOR value,
                       XMVECTOR fallback = XMVectorSet(0, 1, 0, 0));
float SafeLength(XMVECTOR value);

bool CheckSphereOBB(const XMFLOAT3 &spherePosition, float radius,
                    const XMFLOAT3 &boxPosition, const XMFLOAT3 &boxSize,
                    const XMFLOAT4 &boxRotation, XMVECTOR &outNormal,
                    float &outDepth);
TerrainSample SampleTerrainAt(const TerrainData &terrain, float x, float z);
float GetJitterFromTable(uint32_t &cursor, float amplitude);

/**
 * @brief 1フレーム分の物理サブステップへ渡す共有状態です。
 */
struct PhysicsUpdateContext {
  core::GameContext &gameContext;
  float subDt = 0.0f;
  int subSteps = 0;
  XMVECTOR gravity = XMVectorZero();
  TerrainData *terrainData = nullptr;
  GolfGameState *golfState = nullptr;
  ecs::Entity ballEntity = ecs::NULL_ENTITY;
  const HoleSpatialGrid &holeGrid;
  const StaticBodySpatialGrid &staticBodyGrid;
  float maxHoleQueryRange = 0.5f;
  std::vector<BodyInfo> &dynamicBodies;
  CollisionEvents &events;
  PhysicsPerfStats &perfStats;
  uint32_t &jitterCursor;
  float &rollingAudioTimer;
  float &holeSlowMotionCooldown;
};

void ResolveStaticCollisions(PhysicsUpdateContext &frame);
void UpdateRollingAudio(PhysicsUpdateContext &frame, const BodyInfo &body,
                        bool isGrounded, float speed, uint8_t material,
                        int step);
void SimulatePhysicsSubsteps(PhysicsUpdateContext &frame);

} // namespace game::systems
