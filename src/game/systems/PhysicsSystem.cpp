#include "PhysicsSystemInternals.h"
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
#include "../../core/Profiler.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "GameJuiceSystem.h" // 演出効果用
#include "PhysicsFriction.h"
#include "../utils/GameplayPhysicsConstants.h"
#include "TerrainGenerator.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace game::systems {

void PhysicsSystem(core::GameContext &ctx, float dt) {
  PROFILE_SCOPE("Physics.Update");
  // DTキャップ（ラグスパイク対策）
  float clampedDt =
      std::min(dt, game::physics::kMaxSimulationDeltaTime); // 最大30FPS分

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
  ecs::Entity terrainEntity = ecs::NULL_ENTITY;
  ctx.world.Query<TerrainCollider>().Each(
      [&](ecs::Entity entity, TerrainCollider &tc) {
        if (tc.data) {
          terrainData = tc.data.get();
          terrainEntity = entity;
        }
      });

  // ゲーム状態
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();
  ecs::Entity ballEntity = 0xFFFFFFFF;
  if (golfState) {
    ballEntity = static_cast<ecs::Entity>(golfState->ballEntity);
  }

  float ballSpeedForSubsteps = 0.0f;
  if (golfState && ballEntity != UINT32_MAX) {
    golfState->isBallGrounded = false;
    if (auto *ballRb = ctx.world.Get<RigidBody>(ballEntity)) {
      ballSpeedForSubsteps = SafeLength(XMLoadFloat3(&ballRb->velocity));
    }
    golfState->currentBallSpeed = ballSpeedForSubsteps;
  }

  // サブステップ（速度に応じて可変化）
  int subSteps = 4;
  if (ballSpeedForSubsteps < 0.75f) {
    subSteps = 1;
  } else if (ballSpeedForSubsteps < 8.0f) {
    subSteps = 2;
  }
  float subDt = 0.0f;
  if (subSteps > 0) {
    subDt = clampedDt / static_cast<float>(subSteps);
  }
  PhysicsPerfStats perfStats;
  bool spatialCacheRebuilt = false;

  auto *spatialCache = ctx.world.GetGlobal<PhysicsSpatialCache>();
  if (!spatialCache) {
    ctx.world.SetGlobal(PhysicsSpatialCache{});
    spatialCache = ctx.world.GetGlobal<PhysicsSpatialCache>();
  }

  if (subSteps > 0 && spatialCache) {
    size_t holeCount = 0;
    if (golfState) {
      holeCount = golfState->holes.size();
    }
    ecs::Entity firstHole = ecs::NULL_ENTITY;
    ecs::Entity lastHole = ecs::NULL_ENTITY;
    if (holeCount > 0) {
      firstHole = static_cast<ecs::Entity>(golfState->holes.front());
      lastHole = static_cast<ecs::Entity>(golfState->holes.back());
    }
    std::string pageIdentity;
    if (golfState) {
      pageIdentity = golfState->currentPage;
    }
    const bool needsRebuild =
        !golfState || spatialCache->terrainIdentity != terrainData ||
        spatialCache->pageIdentity != pageIdentity ||
        spatialCache->holeCount != holeCount ||
        spatialCache->firstHole != firstHole || spatialCache->lastHole != lastHole;

    if (needsRebuild) {
      std::vector<HoleInfo> holes;
      size_t holeCapacity = holeCount;
      if (holeCapacity == 0) {
        holeCapacity = 64;
      }
      holes.reserve(holeCapacity);
      float maxHoleQueryRange = 0.5f;
      auto appendHole = [&](ecs::Entity e, Transform &t, GolfHole &h) {
        HoleInfo info;
        info.entity = e;
        info.position = XMLoadFloat3(&t.position);
        info.positionFloat = t.position;
        info.radius = h.radius;
        info.gravity = h.gravity;
        info.suctionRange = h.radius * 2.5f;
        maxHoleQueryRange = std::max(maxHoleQueryRange, info.suctionRange);
        holes.push_back(info);
      };
      if (golfState) {
        for (uint32_t id : golfState->holes) {
          const ecs::Entity e = static_cast<ecs::Entity>(id);
          auto *t = ctx.world.Get<Transform>(e);
          auto *h = ctx.world.Get<GolfHole>(e);
          if (t && h) {
            appendHole(e, *t, *h);
          }
        }
      } else {
        ctx.world.Query<Transform, GolfHole>().Each(appendHole);
      }

      std::vector<ecs::Entity> staticEntities;
      staticEntities.reserve(128);
      ctx.world.Query<Transform, RigidBody, Collider>().Each(
          [&](ecs::Entity e, Transform &, RigidBody &rb, Collider &) {
            if (rb.isStatic) {
              staticEntities.push_back(e);
            }
          });

      spatialCache->holeGrid.Build(holes);
      spatialCache->staticBodyGrid.Build(ctx.world, staticEntities);
      spatialCache->maxHoleQueryRange = maxHoleQueryRange;
      spatialCache->terrainIdentity = terrainData;
      spatialCache->pageIdentity = pageIdentity;
      spatialCache->holeCount = holeCount;
      spatialCache->firstHole = firstHole;
      spatialCache->lastHole = lastHole;
      spatialCacheRebuilt = true;
    }
  }

  HoleSpatialGrid emptyHoleGrid;
  StaticBodySpatialGrid emptyStaticBodyGrid;
  const HoleSpatialGrid *holeGrid = &emptyHoleGrid;
  const StaticBodySpatialGrid *staticBodyGrid = &emptyStaticBodyGrid;
  float maxHoleQueryRange = 0.5f;
  if (spatialCache) {
    holeGrid = &spatialCache->holeGrid;
    staticBodyGrid = &spatialCache->staticBodyGrid;
    maxHoleQueryRange = spatialCache->maxHoleQueryRange;
  }

  // 動的ボディだけをサブステップ開始前に一度収集します。
  std::vector<BodyInfo> dynamicBodies;
  dynamicBodies.reserve(8);
  if (subSteps > 0) {
    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
          if (!rb.isStatic) {
            dynamicBodies.push_back({e, &t, &rb, &c});
          }
        });
  }
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

  PhysicsUpdateContext frame{ctx, subDt, subSteps, gravity, terrainData,
                             terrainEntity, golfState, ballEntity, *holeGrid,
                             *staticBodyGrid, maxHoleQueryRange, dynamicBodies,
                             *events, perfStats, jitterCursor,
                             rollingAudioTimer, holeSlowMotionCooldown};
  SimulatePhysicsSubsteps(frame);

  auto &profiler = core::Profiler::Instance();
  profiler.SetCounter("Physics.SubSteps", static_cast<double>(subSteps));
  profiler.SetCounter("Physics.TerrainSamples",
                      static_cast<double>(perfStats.terrainSamples));
  profiler.SetCounter("Physics.HoleCandidates",
                      static_cast<double>(perfStats.holeCandidates));
  profiler.SetCounter("Physics.StaticCandidates",
                      static_cast<double>(perfStats.staticCandidates));
  profiler.SetCounter("Physics.StaticChecks",
                      static_cast<double>(perfStats.staticChecks));
  double cacheRebuiltValue = 0.0;
  if (spatialCacheRebuilt) {
    cacheRebuiltValue = 1.0;
  }
  profiler.SetCounter("Physics.SpatialCacheRebuilt", cacheRebuiltValue);

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
