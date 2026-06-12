/**
 * @file PhysicsSystem.cpp
 * @brief 物理��算システム���（�安定版�（�（
 *
 * ゴルフゲーム向けの安定した物理��ミュレーションを提供、（
 * NaN防止、地形衝突、�（ール吸引を実装��（
 */

#include "PhysicsSystem.h"
#include "../../audio/AudioSystem.h" // 効果音再生用
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "GameJuiceSystem.h" // 演出効果用
#include "PhysicsFriction.h"
#include "TerrainGenerator.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

// ========================================
// 安全なベクトル演算ヘルパー
// ========================================

/**
 * @brief NaNチェチ（��
 */
static bool IsNaN(float v) { return v != v; }

/**
 * @brief ベクトルがNaNを含むかチェチ（��
 */
static bool IsVectorNaN(XMVECTOR v) {
  float x = XMVectorGetX(v);
  float y = XMVectorGetY(v);
  float z = XMVectorGetZ(v);
  return IsNaN(x) || IsNaN(y) || IsNaN(z);
}

/**
 * @brief 安�（なベクトル正規化�（�ゼロベクトル対策）
 */
static XMVECTOR SafeNormalize(XMVECTOR v,
                              XMVECTOR fallback = XMVectorSet(0, 1, 0, 0)) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0001f) {
    return fallback;
  }
  return XMVector3Normalize(v);
}

/**
 * @brief 値を安�（な範囲��にクランチ（
 */
static float SafeClamp(float v, float minVal, float maxVal) {
  if (IsNaN(v))
    return 0.0f;
  return std::clamp(v, minVal, maxVal);
}

/**
 * @brief ベクトルの長さを安�（に取得
 */
static float SafeLength(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq))
    return 0.0f;
  return std::sqrt(lenSq);
}

static float SafeLengthSq(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq))
    return 0.0f;
  return lenSq;
}

// ========================================
// 衝突判定
// ========================================

/**
 * @brief 琁（��とOBB�（�有向壁面��ボックス�（��（衝突判定
 */
static bool CheckSphereOBB(const XMFLOAT3 &spherePos, float radius,
                           const XMFLOAT3 &boxPos, const XMFLOAT3 &boxSize,
                           const XMFLOAT4 &boxRot, XMVECTOR &outNormal,
                           float &outDepth) {
  XMVECTOR sPos = XMLoadFloat3(&spherePos);
  XMVECTOR bPos = XMLoadFloat3(&boxPos);
  // ボックスのサイズ情報から半サイズ（ハーフエクステント）を算出
  XMVECTOR bHalf = XMVectorScale(XMLoadFloat3(&boxSize), 0.5f);
  XMVECTOR bRot = XMLoadFloat4(&boxRot);

  // 球をボックスのローカル座標系に変換
  XMVECTOR relPos = XMVectorSubtract(sPos, bPos);
  XMVECTOR invRot = XMQuaternionInverse(bRot);
  XMVECTOR localPos = XMVector3Rotate(relPos, invRot);

  // ローカル座標系でのAABB判定（クランプ）
  XMVECTOR closestLocal = XMVectorClamp(localPos, XMVectorNegate(bHalf), bHalf);

  // 距離チェック
  XMVECTOR distVecLocal = XMVectorSubtract(localPos, closestLocal);
  float d2 = XMVectorGetX(XMVector3LengthSq(distVecLocal));

  // 中心が外側にある場合
  if (d2 > 0.00001f) {
    if (d2 > radius * radius) {
      return false; // 衝突なし
    }

    float d = std::sqrt(d2);
    XMVECTOR localNormal = XMVectorScale(distVecLocal, 1.0f / d);
    outNormal = XMVector3Rotate(localNormal, bRot);
    outDepth = radius - d;
    return true;
  }

  // 中心が内部にある場合：最も近い面を探す
  float x = XMVectorGetX(localPos);
  float y = XMVectorGetY(localPos);
  float z = XMVectorGetZ(localPos);
  float hx = XMVectorGetX(bHalf);
  float hy = XMVectorGetY(bHalf);
  float hz = XMVectorGetZ(bHalf);

  // 各面への距離（正: 内側への距離）
  float dx_p = hx - x; // +X face
  float dx_n = x + hx; // -X face
  float dy_p = hy - y; // +Y face
  float dy_n = y + hy; // -Y face
  float dz_p = hz - z; // +Z face
  float dz_n = z + hz; // -Z face

  // 最小の絶対値を持つ軸を探す（そこが最も浅い脱出ルート）
  float minD = dx_p;
  int axis = 0; // 0:+x, 1:-x, 2:+y, 3:-y, 4:+z, 5:-z

  if (dx_n < minD) {
    minD = dx_n;
    axis = 1;
  }
  if (dy_p < minD) {
    minD = dy_p;
    axis = 2;
  }
  if (dy_n < minD) {
    minD = dy_n;
    axis = 3;
  }
  if (dz_p < minD) {
    minD = dz_p;
    axis = 4;
  }
  if (dz_n < minD) {
    minD = dz_n;
    axis = 5;
  }

  XMVECTOR localNormal;
  // 脱出方向は面法線
  switch (axis) {
  case 0:
    localNormal = XMVectorSet(1, 0, 0, 0);
    break;
  case 1:
    localNormal = XMVectorSet(-1, 0, 0, 0);
    break;
  case 2:
    localNormal = XMVectorSet(0, 1, 0, 0);
    break;
  case 3:
    localNormal = XMVectorSet(0, -1, 0, 0);
    break;
  case 4:
    localNormal = XMVectorSet(0, 0, 1, 0);
    break;
  case 5:
    localNormal = XMVectorSet(0, 0, -1, 0);
    break;
  }

  outNormal = XMVector3Rotate(localNormal, bRot);
  // 貫通深度 = (表面までの距離) + 半径
  // minDは「表面までの距離」
  outDepth = minD + radius;

  return true;
}

/**
 * @brief 地形の高さと法線を取得
 */
static bool GetTerrainHeightAndNormal(const TerrainData &terrain, float x,
                                      float z, float &outHeight,
                                      XMVECTOR &outNormal) {
  float width = terrain.config.worldWidth;
  float depth = terrain.config.worldDepth;
  int resX = terrain.config.resolutionX;
  int resZ = terrain.config.resolutionZ;

  // UV座標 (0.0~1.0)
  float u = (x / width) + 0.5f;
  float v = 0.5f - (z / depth);

  // 範囲外チェック
  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) {
    outHeight = 0.0f;
    outNormal = XMVectorSet(0, 1, 0, 0);
    return false;
  }

  float fx = u * (resX - 1);
  float fz = v * (resZ - 1);

  int ix = static_cast<int>(fx);
  int iz = static_cast<int>(fz);

  // 境界クランプ
  ix = std::clamp(ix, 0, resX - 2);
  iz = std::clamp(iz, 0, resZ - 2);

  float dx = fx - ix;
  float dz = fz - iz;

  // 安全なインデックスアクセス
  auto GetHeightSafe = [&](int gx, int gz) -> float {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.heightMap.size())) {
      return terrain.heightMap[idx];
    }
    return 0.0f;
  };

  auto GetNormalSafe = [&](int gx, int gz) -> XMVECTOR {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.normals.size())) {
      XMVECTOR n = XMLoadFloat3(&terrain.normals[idx]);
      // 法線が不正な場合はデフォルト値
      if (IsVectorNaN(n)) {
        return XMVectorSet(0, 1, 0, 0);
      }
      return n;
    }
    return XMVectorSet(0, 1, 0, 0);
  };

  float h00 = GetHeightSafe(ix, iz);
  float h10 = GetHeightSafe(ix + 1, iz);
  float h01 = GetHeightSafe(ix, iz + 1);
  float h11 = GetHeightSafe(ix + 1, iz + 1);

  XMVECTOR n00 = GetNormalSafe(ix, iz);
  XMVECTOR n10 = GetNormalSafe(ix + 1, iz);
  XMVECTOR n01 = GetNormalSafe(ix, iz + 1);
  XMVECTOR n11 = GetNormalSafe(ix + 1, iz + 1);

  // バイリニア補間
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  outHeight = h0 * (1.0f - dz) + h1 * dz;

  // 法線の補間
  XMVECTOR n0 = XMVectorLerp(n00, n10, dx);
  XMVECTOR n1 = XMVectorLerp(n01, n11, dx);
  outNormal = SafeNormalize(XMVectorLerp(n0, n1, dz));

  // NaNチェック
  if (IsNaN(outHeight)) {
    outHeight = 0.0f;
  }

  return true;
}

struct TerrainSample {
  bool valid = false;
  float height = 0.0f;
  XMVECTOR normal = XMVectorSet(0, 1, 0, 0);
  uint8_t material = 0;
};

/**
 * @brief 地形の高さ・法線・マテリアルを一度の座標変換で取得します。
 * @author 山内陽
 */
static TerrainSample SampleTerrainAt(const TerrainData &terrain, float x,
                                     float z) {
  TerrainSample sample;
  float width = terrain.config.worldWidth;
  float depth = terrain.config.worldDepth;
  int resX = terrain.config.resolutionX;
  int resZ = terrain.config.resolutionZ;

  float u = (x / width) + 0.5f;
  float v = 0.5f - (z / depth);
  if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f || resX < 2 ||
      resZ < 2) {
    return sample;
  }

  float fx = u * (resX - 1);
  float fz = v * (resZ - 1);
  int ix = std::clamp(static_cast<int>(fx), 0, resX - 2);
  int iz = std::clamp(static_cast<int>(fz), 0, resZ - 2);
  float dx = fx - ix;
  float dz = fz - iz;

  auto getHeightSafe = [&](int gx, int gz) -> float {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.heightMap.size())) {
      return terrain.heightMap[idx];
    }
    return 0.0f;
  };
  auto getNormalSafe = [&](int gx, int gz) -> XMVECTOR {
    int idx = gz * resX + gx;
    if (idx >= 0 && idx < static_cast<int>(terrain.normals.size())) {
      XMVECTOR n = XMLoadFloat3(&terrain.normals[idx]);
      if (!IsVectorNaN(n))
        return n;
    }
    return XMVectorSet(0, 1, 0, 0);
  };

  float h00 = getHeightSafe(ix, iz);
  float h10 = getHeightSafe(ix + 1, iz);
  float h01 = getHeightSafe(ix, iz + 1);
  float h11 = getHeightSafe(ix + 1, iz + 1);
  float h0 = h00 * (1.0f - dx) + h10 * dx;
  float h1 = h01 * (1.0f - dx) + h11 * dx;
  sample.height = h0 * (1.0f - dz) + h1 * dz;
  if (IsNaN(sample.height)) {
    sample.height = 0.0f;
  }

  XMVECTOR n0 = XMVectorLerp(getNormalSafe(ix, iz), getNormalSafe(ix + 1, iz),
                             dx);
  XMVECTOR n1 = XMVectorLerp(getNormalSafe(ix, iz + 1),
                             getNormalSafe(ix + 1, iz + 1), dx);
  sample.normal = SafeNormalize(XMVectorLerp(n0, n1, dz));

  int matX = std::clamp(static_cast<int>(u * (resX - 1)), 0, resX - 1);
  int matZ = std::clamp(static_cast<int>(v * (resZ - 1)), 0, resZ - 1);
  int matIdx = matZ * resX + matX;
  if (matIdx >= 0 && matIdx < static_cast<int>(terrain.materialMap.size())) {
    sample.material = terrain.materialMap[matIdx];
  }
  sample.valid = true;
  return sample;
}

struct BodyInfo {
  ecs::Entity entity = UINT32_MAX;
  Transform *t = nullptr;
  RigidBody *rb = nullptr;
  Collider *c = nullptr;
};

struct HoleInfo {
  ecs::Entity entity = UINT32_MAX;
  XMVECTOR position = XMVectorZero();
  XMFLOAT3 positionFloat = {0.0f, 0.0f, 0.0f};
  float radius = 0.0f;
  float gravity = 0.0f;
  float suctionRange = 0.0f;
};

static int64_t MakeGridKey(int x, int z) {
  return (static_cast<int64_t>(x) << 32) ^
         (static_cast<uint32_t>(z) & 0xffffffffu);
}

static int GridCoord(float v, float cellSize) {
  return static_cast<int>(std::floor(v / cellSize));
}

struct HoleSpatialGrid {
  float cellSize = 5.0f;
  std::vector<HoleInfo> holes;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;

  void Build(const std::vector<HoleInfo> &source) {
    holes = source;
    cells.clear();
    cells.reserve(holes.size() * 2 + 1);
    for (uint32_t i = 0; i < static_cast<uint32_t>(holes.size()); ++i) {
      int cx = GridCoord(holes[i].positionFloat.x, cellSize);
      int cz = GridCoord(holes[i].positionFloat.z, cellSize);
      cells[MakeGridKey(cx, cz)].push_back(i);
    }
  }

  template <typename Func>
  void Query(float x, float z, float radius, Func &&func) const {
    int minX = GridCoord(x - radius, cellSize);
    int maxX = GridCoord(x + radius, cellSize);
    int minZ = GridCoord(z - radius, cellSize);
    int maxZ = GridCoord(z + radius, cellSize);
    for (int cz = minZ; cz <= maxZ; ++cz) {
      for (int cx = minX; cx <= maxX; ++cx) {
        auto it = cells.find(MakeGridKey(cx, cz));
        if (it == cells.end())
          continue;
        for (uint32_t index : it->second) {
          func(holes[index]);
        }
      }
    }
  }
};

struct StaticBodySpatialGrid {
  float cellSize = 8.0f;
  const std::vector<BodyInfo> *bodies = nullptr;
  std::unordered_map<int64_t, std::vector<uint32_t>> cells;

  void Build(const std::vector<BodyInfo> &source) {
    bodies = &source;
    cells.clear();
    cells.reserve(source.size() * 2 + 1);
    for (uint32_t i = 0; i < static_cast<uint32_t>(source.size()); ++i) {
      const BodyInfo &body = source[i];
      if (!body.t || !body.c || body.c->type != ColliderType::Box)
        continue;
      float halfX = std::abs(body.c->size.x * body.t->scale.x);
      float halfZ = std::abs(body.c->size.z * body.t->scale.z);
      int minX = GridCoord(body.t->position.x - halfX, cellSize);
      int maxX = GridCoord(body.t->position.x + halfX, cellSize);
      int minZ = GridCoord(body.t->position.z - halfZ, cellSize);
      int maxZ = GridCoord(body.t->position.z + halfZ, cellSize);
      for (int cz = minZ; cz <= maxZ; ++cz) {
        for (int cx = minX; cx <= maxX; ++cx) {
          cells[MakeGridKey(cx, cz)].push_back(i);
        }
      }
    }
  }

  template <typename Func>
  void Query(float x, float z, float radius, Func &&func) const {
    if (!bodies)
      return;
    std::vector<uint32_t> emitted;
    int minX = GridCoord(x - radius, cellSize);
    int maxX = GridCoord(x + radius, cellSize);
    int minZ = GridCoord(z - radius, cellSize);
    int maxZ = GridCoord(z + radius, cellSize);
    for (int cz = minZ; cz <= maxZ; ++cz) {
      for (int cx = minX; cx <= maxX; ++cx) {
        auto it = cells.find(MakeGridKey(cx, cz));
        if (it == cells.end())
          continue;
        for (uint32_t index : it->second) {
          if (std::find(emitted.begin(), emitted.end(), index) !=
              emitted.end()) {
            continue;
          }
          emitted.push_back(index);
          func((*bodies)[index]);
        }
      }
    }
  }
};

struct PhysicsPerfStats {
  uint32_t terrainSamples = 0;
  uint32_t holeCandidates = 0;
  uint32_t staticCandidates = 0;
  uint32_t staticChecks = 0;
};

static float GetJitterFromTable(uint32_t &cursor, float amplitude) {
  static constexpr std::array<float, 32> kJitterTable = {
      -0.47f, 0.12f,  0.38f,  -0.21f, 0.04f,  0.49f,  -0.34f, 0.27f,
      -0.08f, 0.31f,  -0.42f, 0.18f,  -0.16f, 0.44f,  -0.29f, 0.06f,
      0.23f,  -0.36f, 0.41f,  -0.02f, -0.25f, 0.15f,  0.33f,  -0.45f,
      0.09f,  -0.11f, 0.46f,  -0.31f, 0.21f,  -0.39f, 0.02f,  0.28f};
  float jitter = kJitterTable[cursor % kJitterTable.size()];
  ++cursor;
  return 1.0f + jitter * amplitude;
}

// ========================================
// メイン物理システム
// ========================================

void PhysicsSystem(core::GameContext &ctx, float dt) {
  // DTキャップ（ラグスパイク対策）
  float clampedDt = std::min(dt, 0.033f); // 最大30FPS分

  // 重力
  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);

  // イベントリソースの準備
  auto *events = ctx.world.GetGlobal<CollisionEvents>();
  if (!events) {
    CollisionEvents newEvents;
    ctx.world.SetGlobal(std::move(newEvents));
    events = ctx.world.GetGlobal<CollisionEvents>();
  }
  events->events.clear();
  if (events->events.capacity() < 64) {
    events->events.reserve(64);
  }

  // 地形データ取得
  TerrainData *terrainData = nullptr;
  ctx.world.Query<TerrainCollider>().Each(
      [&](ecs::Entity, TerrainCollider &tc) {
        if (tc.data) {
          terrainData = tc.data.get();
        }
      });

  // ホール情報収集
  std::vector<HoleInfo> holes;
  holes.reserve(64);
  float maxHoleQueryRange = 0.5f;
  ctx.world.Query<Transform, GolfHole>().Each(
      [&](ecs::Entity e, Transform &t, GolfHole &h) {
        HoleInfo info;
        info.entity = e;
        info.position = XMLoadFloat3(&t.position);
        info.positionFloat = t.position;
        info.radius = h.radius;
        info.gravity = h.gravity;
        info.suctionRange = h.radius * 2.5f;
        maxHoleQueryRange = std::max(maxHoleQueryRange, info.suctionRange);
        holes.push_back(info);
      });
  HoleSpatialGrid holeGrid;
  holeGrid.Build(holes);

  // ゲーム状態
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();
  ecs::Entity ballEntity = 0xFFFFFFFF;
  if (golfState) {
    ballEntity = static_cast<ecs::Entity>(golfState->ballEntity);
  }

  // ボディリストはサブステップ中に構成が変わらないため、フレーム先頭で一度だけ収集します。
  std::vector<BodyInfo> dynamicBodies;
  std::vector<BodyInfo> staticBodies;
  dynamicBodies.reserve(8);
  staticBodies.reserve(128);
  ctx.world.Query<Transform, RigidBody, Collider>().Each(
      [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
        BodyInfo info = {e, &t, &rb, &c};
        if (!rb.isStatic) {
          dynamicBodies.push_back(info);
        }
        if (rb.isStatic) {
          staticBodies.push_back(info);
        }
      });

  StaticBodySpatialGrid staticBodyGrid;
  staticBodyGrid.Build(staticBodies);

  bool skipPhysics = false;
  if (golfState && golfState->canShoot) {
    if (auto *ballRb = ctx.world.Get<RigidBody>(ballEntity)) {
      XMVECTOR ballVel = XMLoadFloat3(&ballRb->velocity);
      skipPhysics = SafeLengthSq(ballVel) < 0.000001f;
    }
  }

  // サブステップ（速度に応じて可変化）
  int subSteps = 4;
  if (skipPhysics) {
    subSteps = 0;
  } else if (golfState && golfState->currentBallSpeed < 0.75f) {
    subSteps = 1;
  } else if (golfState && golfState->currentBallSpeed < 8.0f) {
    subSteps = 2;
  }
  float subDt = subSteps > 0 ? clampedDt / static_cast<float>(subSteps) : 0.0f;
  PhysicsPerfStats perfStats;
  static uint32_t jitterCursor = 0;
  static float rollingAudioTimer = 0.0f;
  static float holeSlowMotionCooldown = 0.0f;
  rollingAudioTimer += clampedDt;
  holeSlowMotionCooldown = std::max(0.0f, holeSlowMotionCooldown - clampedDt);

  // デバッグログ用
#ifndef NDEBUG
  static float debugTimer = 0.0f;
  debugTimer += clampedDt;
#endif

  // フリッパー制御（ピンボール用）
  float flipperSpeed = 15.0f * clampedDt;
  ctx.world.Query<Transform, Flipper>().Each(
      [&](ecs::Entity, Transform &t, Flipper &f) {
        bool pressed = false;
        if (f.side == Flipper::Left && ctx.input.GetKey('Z'))
          pressed = true;
        if (f.side == Flipper::Right && ctx.input.GetKey(VK_OEM_2))
          pressed = true;

        float target = 0.0f;
        if (pressed) {
          target = 1.0f;
        }
        if (f.currentParam < target) {
          f.currentParam = std::min(f.currentParam + flipperSpeed, 1.0f);
        } else {
          f.currentParam = std::max(f.currentParam - flipperSpeed, 0.0f);
        }

        float angleDeg = f.currentParam * f.maxAngle;
        if (f.side == Flipper::Left)
          angleDeg *= -1.0f;

        XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
                                              XMConvertToRadians(angleDeg));
        XMStoreFloat4(&t.rotation, q);
      });

  if (subSteps == 0) {
    if (ctx.audio && ballEntity != UINT32_MAX) {
      ctx.audio->SetLoopingSE(ctx, "BallRoll", "", 0.0f);
    }
  }

  // サブステップループ
  for (int step = 0; step < subSteps; ++step) {
    // 動的オブジェクトの更新
    for (auto &body : dynamicBodies) {
      Transform &t = *body.t;
      RigidBody &rb = *body.rb;
      Collider &col = *body.c;

      XMVECTOR pos = XMLoadFloat3(&t.position);
      XMVECTOR vel = XMLoadFloat3(&rb.velocity);

      float posX = XMVectorGetX(pos);
      float posY = XMVectorGetY(pos);
      float posZ = XMVectorGetZ(pos);

      // マテリアル・地形高さ・法線をまとめて取得します。
      TerrainSample terrainSample;
      if (terrainData) {
        terrainSample = SampleTerrainAt(*terrainData, posX, posZ);
        ++perfStats.terrainSamples;
      }
      uint8_t mat = terrainSample.material; // フェアウェイ（デフォルト値）

      // 水・溶岩などOB地形に静止したらOBフラグを立てる（通過時はセーフ）
      const TerrainMaterial currentMaterial = static_cast<TerrainMaterial>(mat);
      if (IsOutOfBoundsTerrainMaterial(currentMaterial)) {
        if (golfState && body.entity == ballEntity) {
          float speed = SafeLength(vel);
          // 速度が十分低い(静止状態)ときのみOB
          if (speed < 0.5f) {
            golfState->isOB = true;
            LOG_INFO("Physics", "Ball stopped in OB terrain. material={}",
                     static_cast<int>(currentMaterial));
          }
        }
      }

      // NaNチェック - 異常値なら位置リセット
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "NaN detected, resetting position");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
        XMStoreFloat3(&t.position, pos);
        XMStoreFloat3(&rb.velocity, vel);
        continue;
      }

      // 速度クランプ
      float speed = SafeLength(vel);
      if (speed > 100.0f) {
        vel = XMVectorScale(SafeNormalize(vel), 100.0f);
      }

      // 加速度計算
      XMVECTOR acc = gravity;

      // 地形衝突判定
      bool isGrounded = false;
      XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);

      if (terrainData && col.type == ColliderType::Sphere) {
        float terrainH = 0.0f;
        XMVECTOR terrainN;

        bool insideHole = false;
        float carveDepth = 0.0f;
        holeGrid.Query(posX, posZ, 0.5f, [&](const HoleInfo &hole) {
          ++perfStats.holeCandidates;
          float dx = posX - XMVectorGetX(hole.position);
          float dz = posZ - XMVectorGetZ(hole.position);
          float distSq = dx * dx + dz * dz;
          // ホール視覚サイズに合わせた判定（scale 0.5 = 半径0.5）
          float holeVisualRadius = 0.5f; // ビジュアルと統一
          if (distSq < holeVisualRadius * holeVisualRadius &&
              std::abs(posY - XMVectorGetY(hole.position)) < 2.0f) {
            insideHole = true;
            carveDepth = 0.6f; // 穴の深さ

            // ホール内からの脱出防止：縁に向かう速度をカット
            float dist = std::sqrt(distSq);
            if (dist > 0.01f) {
              // ボールからホール中心への方向
              XMVECTOR toCenter = XMVectorSubtract(hole.position, pos);
              toCenter = XMVectorSetY(toCenter, 0.0f); // XZ平面のみ
              toCenter = XMVector3Normalize(toCenter);

              // 現在の速度のうち、縁に向かう成分（中心から離れる方向）
              float velOutward = -XMVectorGetX(XMVector3Dot(vel, toCenter));
              if (velOutward > 0.0f) {
                // 縁に向かう速度を大幅カット（脱出防止）
                vel = XMVectorAdd(vel,
                                  XMVectorScale(toCenter, velOutward * 0.9f));
              }
            }
          }
        });

        if (terrainSample.valid) {
          terrainH = terrainSample.height;
          terrainN = terrainSample.normal;
          if (insideHole) {
            terrainH -= carveDepth;
            terrainN = XMVectorSet(0, 1, 0, 0);
          }

          float ballBottom = posY - col.radius;
          float penetration = terrainH - ballBottom;

          if (penetration > 0.0f) {
            if (insideHole) {
              penetration = std::min(penetration, 0.01f);
            }
            // めり込み解消（法線方向に押し出し）
            float ny = std::max(XMVectorGetY(terrainN), 0.1f);
            float pushAmount = penetration / ny;
            pushAmount =
                std::min(pushAmount, col.radius * 2.0f); // 過度な押し出し防止

            pos = XMVectorAdd(pos, XMVectorScale(terrainN, pushAmount));

            // 速度の法線成分を処理
            float vn = XMVectorGetX(XMVector3Dot(vel, terrainN));
            if (vn < 0.0f) {
              // 衝突による反射ベクトルを計算し、僅かなランダム挙動を加算
              float jitter = GetJitterFromTable(jitterCursor, 0.18f);
              float bounce = std::max(0.0f, rb.restitution * 0.5f * jitter);
              vel = XMVectorSubtract(
                  vel, XMVectorScale(terrainN, vn * (1.0f + bounce)));

              // 一定の速度以上で衝突した際にバウンド演出および効果音を再生
              float impactSpeed = std::abs(vn);
              if (impactSpeed > 2.0f) {
                float strength =
                    std::clamp((impactSpeed - 2.0f) / 10.0f, 0.0f, 1.0f);

                // マテリアルエフェクト
                auto *juice = ctx.world.GetGlobal<
                    GameJuiceSystem>();
                if (auto *juiceSys = ctx.world.GetGlobal<GameJuiceSystem>()) {
                  juiceSys->TriggerMaterialEffect(
                      ctx, t.position, static_cast<TerrainMaterial>(mat),
                      strength);
                  if (impactSpeed > 3.0f) {
                    float rippleRadius = col.radius * 8.0f;
                    juiceSys->TriggerRippleEffect(ctx, t.position, rippleRadius,
                                                  strength);
                  }
                }

                // SE再生
                std::string seName = "se_shot_soft";
                float volume = strength;
                float pitch = 1.0f;

                switch (static_cast<TerrainMaterial>(mat)) {
                case TerrainMaterial::Bunker:
                  seName = "se_Bunker";
                  break;
                case TerrainMaterial::Rough:
                  seName = "se_Rough";
                  volume *= 0.8f;
                  break;
                case TerrainMaterial::Green:
                  seName = "se_Fairway";
                  pitch = 1.1f; // グリーンは硬め
                  break;
                default: // Fairway
                  seName = "se_Fairway";
                  break;
                }

                if (ctx.audio) {
                  ctx.audio->PlaySE(ctx, seName, volume, pitch);
                }
                // なければ volumeのみ。
              }
            }

            isGrounded = true;
            groundNormal = terrainN;
          } else if (penetration > -0.1f) {
            // 接地マージン
            isGrounded = true;
            groundNormal = terrainN;
          }
        }
      } else {
        // フォールバック: 平面コリジョン (y=0)
        float posY = XMVectorGetY(pos);
        float bottom = posY - col.radius;

        if (bottom < 0.0f) {
          pos = XMVectorSetY(pos, col.radius);
          float vy = XMVectorGetY(vel);
          if (vy < 0.0f) {
            vel = XMVectorSetY(vel, -vy * rb.restitution);
          }
          isGrounded = true;
        }
      }

      // ホール吸引（ボールのみ対象）
      if (body.entity == ballEntity && col.type == ColliderType::Sphere) {
        float ballY = XMVectorGetY(pos);
        holeGrid.Query(posX, posZ, maxHoleQueryRange,
                       [&](const HoleInfo &hole) {
          ++perfStats.holeCandidates;
          float holeY = XMVectorGetY(hole.position);
          if (std::abs(ballY - holeY) > 1.0f)
            return;

          XMVECTOR toHole = XMVectorSubtract(hole.position, pos);
          float distSq = XMVectorGetX(
              XMVector3LengthSq(XMVectorSetY(toHole, 0.0f))); // XZ距離
          float range = hole.suctionRange;

          if (distSq < range * range && distSq > 0.001f) {
            float verticalBias =
                XMVectorGetY(toHole) - 0.05f; // 常にわずかに下向きに引く
            verticalBias = std::min(verticalBias, -0.05f);
            XMVECTOR pullVec = XMVectorSetY(toHole, verticalBias);

            float dirLen = SafeLength(pullVec);
            XMVECTOR dir = XMVectorZero();
            if (dirLen > 0.0001f) {
              dir = XMVectorScale(pullVec, 1.0f / dirLen);
            }
            float dist = std::sqrt(distSq);
            float normalized = std::clamp(dist / range, 0.0f, 1.0f);
            float expo = std::exp(-normalized * normalized * 4.5f);
            float ease = std::pow(std::max(0.0f, 1.0f - normalized), 2.2f);
            float factor =
                std::clamp((expo * 0.65f + ease * 0.75f), 0.0f, 1.1f);
            if (holeSlowMotionCooldown <= 0.0f) {
              if (auto *juiceSys = ctx.world.GetGlobal<GameJuiceSystem>()) {
              float slowScale = 0.35f + normalized * 0.25f;
              float slowDuration = 0.5f + (1.0f - normalized) * 0.4f;
              juiceSys->TriggerSlowMotion(slowDuration, slowScale);
              holeSlowMotionCooldown = 0.25f;
              }
            }
            acc = XMVectorAdd(acc, XMVectorScale(dir, hole.gravity * factor));
          }
        });
      }

      // 接地時は法線方向の加速度を除去し、斜面方向の重力のみを残す
      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(
            groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      // 接地時の摩擦と斜面処理
      if (isGrounded) {
        // 接地時における斜面に沿った摩擦力と重力加速度の減衰処理
        float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
        if (vn < 0.0f) {
          vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
        }

        XMVECTOR slopeAccel = XMVectorSubtract(
            gravity,
            XMVectorScale(groundNormal,
                          XMVectorGetX(XMVector3Dot(gravity, groundNormal))));
        float slopeMag = SafeLength(slopeAccel);
        XMVECTOR slopeDir = XMVectorZero();
        if (slopeMag > 0.0001f) {
          slopeDir = XMVectorScale(slopeAccel, 1.0f / slopeMag);
        }

        // ゼロ速になっても斜面なら滑り出すための微小ブレークアウェイ
        float breakaway = 0.0f;
        if (slopeMag > 0.15f && SafeLength(vel) < 0.1f) {
          breakaway = 0.05f;
        }
        if (breakaway > 0.0f) {
          vel = XMVectorAdd(vel, XMVectorScale(slopeDir, breakaway * subDt));
        }

        float currentSpeed = SafeLength(vel);
        float terrainScale = 1.0f;
        if (terrainData) {
          terrainScale = terrainData->config.friction;
        }
        float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
        float frictionAccel = ComputeGrassRollingAcceleration(
            currentSpeed, ny, static_cast<TerrainMaterial>(mat), terrainScale);
        if (golfState && body.entity == ballEntity) {
          float scale = golfState->rollingFrictionScale;
          if (!std::isfinite(scale) || scale < 0.05f) {
            scale = 1.0f;
          }
          frictionAccel *= scale;
        }

        // 環境状態の保存（ボールのみ対象）
        if (golfState && body.entity == ballEntity) {
          golfState->isBallGrounded = true;
          golfState->currentMaterial = static_cast<TerrainMaterial>(mat);
          golfState->currentBallSpeed = currentSpeed;
        }

        // 接線方向の重力成分に対する静止摩擦チェック
        float tangentialAcc = SafeLength(acc);
        float staticLimit = frictionAccel * 1.2f;

        if (currentSpeed < 0.05f && tangentialAcc < staticLimit) {
          // ほぼ停止 & 重力に勝てる摩擦がある -> 完全停止
          vel = XMVectorZero();
          acc = XMVectorZero();
        } else if (currentSpeed > 0.0001f) {
          // 高速域の指数減衰と低速域の線形減衰をブレンドして自然な手触りで停止させる
          float t = std::clamp((currentSpeed - 1.0f) / 4.0f, 0.0f, 1.0f);

          float k = frictionAccel;
          float expRatio = std::exp(-k * subDt);

          float linearDrop = frictionAccel * subDt;
          float linearRatio = 0.0f;
          if (currentSpeed > linearDrop) {
            linearRatio = (currentSpeed - linearDrop) / currentSpeed;
          }
          float finalRatio = t * expRatio + (1.0f - t) * linearRatio;
          vel = XMVectorScale(vel, finalRatio);

          // 極低速時の停止判定
          if (SafeLength(vel) < 0.02f) {
            vel = XMVectorZero();
          }
        }

        // 極低速時の微細振動カット (Green上などでのピク付き防止)
        float speedAfter = SafeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.03f && slopeFlatness > 0.90f) {
          vel = XMVectorZero();
        }
      }

      // 速度の二乗に比例する簡易的な空気抵抗をボールに対して常時適用
      speed = SafeLength(vel);
      if (speed > 0.001f) {
        float K = 0.000876f;
        float dragForce = K * rb.drag * speed * speed;
        float dragAccMagnitude = dragForce / rb.mass;

        XMVECTOR dragDir = XMVectorScale(vel, -1.0f / speed);
        XMVECTOR dragAcc = XMVectorScale(dragDir, dragAccMagnitude);

        acc = XMVectorAdd(acc, dragAcc);
      }

      // オイラー積分 (復活)
      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));
      // 最終NaNチェック
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "Post-integration NaN detected, resetting");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
      }

      // 停止判定（平坦時のみ）。
      float speedFinal = SafeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.008f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }

      // 落下限界
      if (XMVectorGetY(pos) < -50.0f) {
        pos = XMVectorSet(0, 5, 0, 0);
        vel = XMVectorZero();
      }

      // 値を書き戻す
      XMStoreFloat3(&t.position, pos);
      XMStoreFloat3(&rb.velocity, vel);

      // 接地中の走行音 (Rolling SE)
      if (ctx.audio && body.entity == ballEntity && step == subSteps - 1 &&
          rollingAudioTimer >= (1.0f / 30.0f)) {
        if (isGrounded && speedFinal > 0.5f) {
          std::string seName = "se_Fairway";
          float pBase = 0.0f;
          switch (static_cast<TerrainMaterial>(mat)) {
          case TerrainMaterial::Bunker:
            seName = "se_Bunker";
            pBase = -0.2f;
            break;
          case TerrainMaterial::Rough:
            seName = "se_Rough";
            pBase = -0.1f;
            break;
          case TerrainMaterial::Green:
            seName = "se_Fairway";
            pBase = 0.1f;
            break;
          default:
            break;
          }
          // 速度に応じて音量・ピッチ調整 (0.0~1.0)
          float vol = std::clamp(speedFinal / 20.0f, 0.0f, 1.0f) * 0.4f;
          float p = pBase + (speedFinal / 40.0f);
          ctx.audio->SetLoopingSE(ctx, "BallRoll", seName, vol, p);
        } else {
          // 停止中または空中なら停止
          ctx.audio->SetLoopingSE(ctx, "BallRoll", "", 0.0f);
        }
      }
    }

    if (step == subSteps - 1 && rollingAudioTimer >= (1.0f / 30.0f)) {
      rollingAudioTimer = 0.0f;
    }

    // 静的オブジェクトとの衝突
    for (auto &dyn : dynamicBodies) {
      if (dyn.c->type != ColliderType::Sphere)
        continue;

      const float queryRadius = std::max(12.0f, dyn.c->radius + staticBodyGrid.cellSize);
      staticBodyGrid.Query(dyn.t->position.x, dyn.t->position.z, queryRadius,
                           [&](const BodyInfo &other) {
        ++perfStats.staticCandidates;
        if (other.c->type != ColliderType::Box)
          return;

        XMVECTOR normal;
        float depth;
        XMFLOAT3 scaledSize = {other.c->size.x * other.t->scale.x,
                               other.c->size.y * other.t->scale.y,
                               other.c->size.z * other.t->scale.z};

          ++perfStats.staticChecks;
        if (CheckSphereOBB(dyn.t->position, dyn.c->radius, other.t->position,
                           scaledSize, other.t->rotation, normal, depth)) {
          // ホールはトリガーのみ
          if (ctx.world.Has<GolfHole>(other.entity)) {
            events->events.push_back({dyn.entity, other.entity});
            return;
          }

          events->events.push_back({dyn.entity, other.entity});

          // 押し出し
          XMVECTOR pos = XMLoadFloat3(&dyn.t->position);
          pos = XMVectorAdd(pos, XMVectorScale(normal, depth));
          XMStoreFloat3(&dyn.t->position, pos);

          // 反射
          XMVECTOR vel = XMLoadFloat3(&dyn.rb->velocity);
          float vn = XMVectorGetX(XMVector3Dot(vel, normal));
          if (vn < 0.0f) {
            float jitter = GetJitterFromTable(jitterCursor, 0.2f);
            float bounce =
                std::max(0.0f, (dyn.rb->restitution + other.rb->restitution) *
                                   0.5f * jitter);
            vel = XMVectorSubtract(vel,
                                   XMVectorScale(normal, vn * (1.0f + bounce)));
            XMStoreFloat3(&dyn.rb->velocity, vel);
          }
        }
      });
    }
  }

  // デバッグログ出力
#ifndef NDEBUG
  if (debugTimer > 0.25f) {
    debugTimer = 0.0f;

    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &) {
          if (golfState && e == golfState->ballEntity) {
            XMVECTOR vel = XMLoadFloat3(&rb.velocity);
            float speed = SafeLength(vel);
            bool grounded = t.position.y < 1.0f; // 簡易判定
            std::string groundedStr = "N";
            if (grounded) groundedStr = "Y";
            LOG_DEBUG("Physics",
                      "ball speed={:.3f} grounded={} pos=({:.3f},{:.3f},{:.3f}) "
                      "subSteps={} terrainSamples={} holeCandidates={} staticCandidates={} staticChecks={}",
                      speed, groundedStr, t.position.x, t.position.y,
                      t.position.z, subSteps, perfStats.terrainSamples,
                      perfStats.holeCandidates, perfStats.staticCandidates,
                      perfStats.staticChecks);
          }
        });
  }
#endif
}

} // namespace game::systems
