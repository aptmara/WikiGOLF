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

/**
 * @brief 格子座標からハッシュキーを生成します。
 */
int64_t MakeGridKey(int x, int z);

/**
 * @brief ワールド座標から格子インデックスを算出します。
 */
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

/** @brief 浮動小数点数が非数（NaN）か判定します。 */
bool IsNaN(float value);

/** @brief ベクトル要素に非数（NaN）が含まれるか判定します。 */
bool IsVectorNaN(XMVECTOR value);

/** @brief ゼロ除算を回避して正規化ベクトルを取得します。 */
XMVECTOR SafeNormalize(XMVECTOR value,
                       XMVECTOR fallback = XMVectorSet(0, 1, 0, 0));

/** @brief ベクトルの長さを安全に取得します。 */
float SafeLength(XMVECTOR value);

/**
 * @brief 球とOBB（有向境界ボックス）の衝突を判定します。
 */
bool CheckSphereOBB(const XMFLOAT3 &spherePosition, float radius,
                    const XMFLOAT3 &boxPosition, const XMFLOAT3 &boxSize,
                    const XMFLOAT4 &boxRotation, XMVECTOR &outNormal,
                    float &outDepth);

/** @brief 地形データの指定座標における高低差と法線をサンプリングします。 */
TerrainSample SampleTerrainAt(const TerrainData &terrain, float x, float z);

/** @brief ジッターテーブルから微小乱数値を取得します。 */
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

/** @brief 静的コライダーとの衝突応答を解決します。 */
void ResolveStaticCollisions(PhysicsUpdateContext &frame);

/** @brief ボールの転がり音の再生状態を更新します。 */
void UpdateRollingAudio(PhysicsUpdateContext &frame, const BodyInfo &body,
                        bool isGrounded, float speed, uint8_t material,
                        int step);

/** @brief 物理サブステップの積分シミュレーションを実行します。 */
void SimulatePhysicsSubsteps(PhysicsUpdateContext &frame);

} // namespace game::systems
