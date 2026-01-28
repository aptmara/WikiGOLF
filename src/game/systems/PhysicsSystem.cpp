/**
 * @file PhysicsSystem.cpp
 * @brief 物理演算システム（安定版）
 *
 * ゴルフゲーム向けの安定した物理シミュレーションを提供。
 * NaN防止、地形衝突、ホール吸引を実装。
 */

#include "PhysicsSystem.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

// ========================================
// 安全なベクトル演算ヘルパー
// ========================================

/**
 * @brief NaNチェック
 */
static bool IsNaN(float v) { return v != v; }

/**
 * @brief ベクトルがNaNを含むかチェック
 */
static bool IsVectorNaN(XMVECTOR v) {
  float x = XMVectorGetX(v);
  float y = XMVectorGetY(v);
  float z = XMVectorGetZ(v);
  return IsNaN(x) || IsNaN(y) || IsNaN(z);
}

/**
 * @brief 安全なベクトル正規化（ゼロベクトル対策）
 */
static XMVECTOR SafeNormalize(XMVECTOR v, XMVECTOR fallback = XMVectorSet(0, 1, 0, 0)) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0001f) {
    return fallback;
  }
  return XMVector3Normalize(v);
}

/**
 * @brief 値を安全な範囲にクランプ
 */
static float SafeClamp(float v, float minVal, float maxVal) {
  if (IsNaN(v)) return 0.0f;
  return std::clamp(v, minVal, maxVal);
}

/**
 * @brief ベクトルの長さを安全に取得
 */
static float SafeLength(XMVECTOR v) {
  float lenSq = XMVectorGetX(XMVector3LengthSq(v));
  if (lenSq < 0.0f || IsNaN(lenSq)) return 0.0f;
  return std::sqrt(lenSq);
}

// ========================================
// 衝突判定
// ========================================

/**
 * @brief 球体とOBB（有向境界ボックス）の衝突判定
 */
static bool CheckSphereOBB(const XMFLOAT3 &spherePos, float radius,
                           const XMFLOAT3 &boxPos, const XMFLOAT3 &boxSize,
                           const XMFLOAT4 &boxRot, XMVECTOR &outNormal,
                           float &outDepth) {
  XMVECTOR sPos = XMLoadFloat3(&spherePos);
  XMVECTOR bPos = XMLoadFloat3(&boxPos);
  XMVECTOR bSize = XMLoadFloat3(&boxSize);
  XMVECTOR bRot = XMLoadFloat4(&boxRot);

  // 球をボックスのローカル座標系に変換
  XMVECTOR relPos = XMVectorSubtract(sPos, bPos);
  XMVECTOR invRot = XMQuaternionInverse(bRot);
  XMVECTOR localPos = XMVector3Rotate(relPos, invRot);

  // ローカル座標系でのAABB判定（クランプ）
  XMVECTOR closestLocal = XMVectorClamp(localPos, XMVectorNegate(bSize), bSize);

  // 距離チェック
  XMVECTOR distVecLocal = XMVectorSubtract(localPos, closestLocal);
  float d2 = XMVectorGetX(XMVector3LengthSq(distVecLocal));

  if (d2 > radius * radius)
    return false;

  float d = std::sqrt(d2);

  // 中心が内部にある場合
  if (d < 0.0001f) {
    XMVECTOR localNormal = XMVectorSet(0, 1, 0, 0);
    outNormal = XMVector3Rotate(localNormal, bRot);
    outDepth = radius;
    return true;
  }

  // 法線をワールド座標系へ変換
  XMVECTOR localNormal = XMVectorScale(distVecLocal, 1.0f / d);
  outNormal = XMVector3Rotate(localNormal, bRot);
  outDepth = radius - d;
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

// ========================================
// メイン物理システム
// ========================================

void PhysicsSystem(core::GameContext &ctx, float dt) {
  // DTキャップ（ラグスパイク対策）
  float clampedDt = std::min(dt, 0.033f); // 最大30FPS分
  
  // サブステップ（安定性向上）
  const int subSteps = 4;
  float subDt = clampedDt / static_cast<float>(subSteps);
  
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

  // 地形データ取得
  TerrainData *terrainData = nullptr;
  ctx.world.Query<TerrainCollider>().Each(
      [&](ecs::Entity, TerrainCollider &tc) {
        if (tc.data) {
          terrainData = tc.data.get();
        }
      });

  // ホール情報収集
  struct HoleInfo {
    XMVECTOR position;
    float radius;
    float gravity;
  };
  std::vector<HoleInfo> holes;
  ctx.world.Query<Transform, GolfHole>().Each(
      [&](ecs::Entity, Transform &t, GolfHole &h) {
        holes.push_back({XMLoadFloat3(&t.position), h.radius, h.gravity});
      });

  // ゲーム状態
  auto *golfState = ctx.world.GetGlobal<GolfGameState>();
  ecs::Entity ballEntity =
      golfState ? static_cast<ecs::Entity>(golfState->ballEntity) : 0xFFFFFFFF;

  // デバッグログ用
  static float debugTimer = 0.0f;
  debugTimer += clampedDt;

  // フリッパー制御（ピンボール用）
  float flipperSpeed = 15.0f * clampedDt;
  ctx.world.Query<Transform, Flipper>().Each(
      [&](ecs::Entity, Transform &t, Flipper &f) {
        bool pressed = false;
        if (f.side == Flipper::Left && ctx.input.GetKey('Z'))
          pressed = true;
        if (f.side == Flipper::Right && ctx.input.GetKey(VK_OEM_2))
          pressed = true;

        float target = pressed ? 1.0f : 0.0f;
        if (f.currentParam < target) {
          f.currentParam = std::min(f.currentParam + flipperSpeed, 1.0f);
        } else {
          f.currentParam = std::max(f.currentParam - flipperSpeed, 0.0f);
        }

        float angleDeg = f.currentParam * f.maxAngle;
        if (f.side == Flipper::Left) angleDeg *= -1.0f;

        XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
                                              XMConvertToRadians(angleDeg));
        XMStoreFloat4(&t.rotation, q);
      });

  // サブステップループ
  for (int step = 0; step < subSteps; ++step) {
    // ボディリスト収集
    struct BodyInfo {
      ecs::Entity entity;
      Transform *t;
      RigidBody *rb;
      Collider *c;
    };
    std::vector<BodyInfo> dynamicBodies;
    std::vector<BodyInfo> staticBodies;

    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &c) {
          BodyInfo info = {e, &t, &rb, &c};
          if (!rb.isStatic) {
            dynamicBodies.push_back(info);
          }
          staticBodies.push_back(info);
        });

    // 動的オブジェクトの更新
    for (auto &body : dynamicBodies) {
      Transform &t = *body.t;
      RigidBody &rb = *body.rb;
      Collider &col = *body.c;

      XMVECTOR pos = XMLoadFloat3(&t.position);
      XMVECTOR vel = XMLoadFloat3(&rb.velocity);

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

        float posX = XMVectorGetX(pos);
        float posY = XMVectorGetY(pos);
        float posZ = XMVectorGetZ(pos);

        if (GetTerrainHeightAndNormal(*terrainData, posX, posZ, terrainH, terrainN)) {
          float ballBottom = posY - col.radius;
          float penetration = terrainH - ballBottom;

          if (penetration > 0.0f) {
            // めり込み解消（法線方向に押し出し）
            float ny = std::max(XMVectorGetY(terrainN), 0.1f);
            float pushAmount = penetration / ny;
            pushAmount = std::min(pushAmount, col.radius * 2.0f); // 過度な押し出し防止

            pos = XMVectorAdd(pos, XMVectorScale(terrainN, pushAmount));

            // 速度の法線成分を処理
            float vn = XMVectorGetX(XMVector3Dot(vel, terrainN));
            if (vn < 0.0f) {
              // 反射
              float bounce = rb.restitution * 0.5f;
              vel = XMVectorSubtract(vel, XMVectorScale(terrainN, vn * (1.0f + bounce)));
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

      // ホール吸引
      if (col.type == ColliderType::Sphere) {
        float ballY = XMVectorGetY(pos);
        for (const auto &hole : holes) {
          float holeY = XMVectorGetY(hole.position);
          if (std::abs(ballY - holeY) > 0.5f) continue;

          XMVECTOR toHole = XMVectorSubtract(hole.position, pos);
          toHole = XMVectorSetY(toHole, 0.0f);

          float distSq = XMVectorGetX(XMVector3LengthSq(toHole));
          float range = hole.radius * 2.0f;

          if (distSq < range * range && distSq > 0.001f) {
            float dist = std::sqrt(distSq);
            XMVECTOR dir = XMVectorScale(toHole, 1.0f / dist);
            float factor = 1.0f - (dist / range);
            factor = factor * factor;
            acc = XMVectorAdd(acc, XMVectorScale(dir, hole.gravity * factor));
          }
        }
      }

      // 接地時は法線方向の加速度を除去し、斜面方向の重力のみを残す
      if (isGrounded) {
        XMVECTOR normalComponent = XMVectorScale(groundNormal, XMVectorGetX(XMVector3Dot(acc, groundNormal)));
        acc = XMVectorSubtract(acc, normalComponent);
      }

      // 接地時の摩擦と斜面処理
      if (isGrounded) {
        // 法線成分を除去
        float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
        if (vn < 0.0f) {
          vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
        }

        // 斜面方向の重力（タンジェント成分）を計算
        XMVECTOR slopeAccel = XMVectorSubtract(gravity, XMVectorScale(groundNormal, XMVectorGetX(XMVector3Dot(gravity, groundNormal))));
        float slopeMag = SafeLength(slopeAccel);
        XMVECTOR slopeDir = (slopeMag > 0.0001f) ? XMVectorScale(slopeAccel, 1.0f / slopeMag) : XMVectorZero();

        // ゼロ速になっても斜面なら滑り出すための微小ブレークアウェイ
        float breakaway = (slopeMag > 0.05f && SafeLength(vel) < 0.15f) ? 0.2f : 0.0f;
        if (breakaway > 0.0f) {
          vel = XMVectorAdd(vel, XMVectorScale(slopeDir, breakaway * subDt));
        }

        // 摩擦係数計算（マテリアルと斜面考慮）
        float friction = rb.rollingFriction;
        if (terrainData) {
          friction *= terrainData->config.friction;
          uint8_t mat = 0;
          if (terrainData && !terrainData->materialMap.empty()) {
            float u = XMVectorGetX(pos) / terrainData->config.worldWidth + 0.5f;
            float v = 0.5f - XMVectorGetZ(pos) / terrainData->config.worldDepth;
            int ix = (int)(u * (terrainData->config.resolutionX - 1));
            int iz = (int)(v * (terrainData->config.resolutionZ - 1));
            if (ix >= 0 && ix < terrainData->config.resolutionX && iz >= 0 && iz < terrainData->config.resolutionZ) {
              mat = terrainData->materialMap[iz * terrainData->config.resolutionX + ix];
            }
          }
          switch (mat) {
          case 1: friction *= 2.0f; break;      // Rough
          case 2: friction *= 3.5f; break;      // Bunker
          case 3: friction *= 0.5f; break;      // Green
          default: break;
          }
        }

        // 斜面が急なほど摩擦を減らして滑りやすくする
        float ny = std::clamp(XMVectorGetY(groundNormal), 0.0f, 1.0f);
        float slopeFrictionScale = 0.3f + 0.7f * ny; // ny=1 ->1.0, ny=0 ->0.3
        friction *= slopeFrictionScale;

        // 摩擦による減速
        float speed = SafeLength(vel);
        if (speed > 0.0001f) {
          float frictionForce = friction * subDt;
          if (speed < frictionForce) {
            vel = XMVectorZero();
          } else {
            float newSpeed = speed - frictionForce;
            vel = XMVectorScale(vel, newSpeed / speed);
          }
        }

        // 低速減衰（平坦のみ）。斜面では止めない。
        float speedAfter = SafeLength(vel);
        float slopeFlatness = XMVectorGetY(groundNormal);
        if (speedAfter < 0.5f && slopeFlatness > 0.98f) {
          vel = XMVectorScale(vel, 0.98f);
        }
      } else {
        // 空中での空気抵抗
        float airDrag = rb.drag * 0.1f;
        vel = XMVectorScale(vel, 1.0f - airDrag * subDt);
      }

      // オイラー積分
      vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));
      pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));

      // 最終NaNチェック
      if (IsVectorNaN(pos) || IsVectorNaN(vel)) {
        LOG_DEBUG("Physics", "Post-integration NaN detected, resetting");
        pos = XMVectorSet(0, 2, 0, 0);
        vel = XMVectorZero();
      }

      // 停止判定（平坦時のみ）。斜面では静止させない。
      float speedFinal = SafeLength(vel);
      float slopeFlatnessFinal = XMVectorGetY(groundNormal);
      if (speedFinal < 0.05f && isGrounded && slopeFlatnessFinal > 0.98f) {
        vel = XMVectorZero();
      }

      // 落下限界
      if (XMVectorGetY(pos) < -50.0f) {
        pos = XMVectorSet(0, 5, 0, 0);
        vel = XMVectorZero();
      }

      XMStoreFloat3(&t.position, pos);
      XMStoreFloat3(&rb.velocity, vel);
    }

    // 静的オブジェクトとの衝突
    for (auto &dyn : dynamicBodies) {
      if (dyn.c->type != ColliderType::Sphere) continue;

      for (auto &other : staticBodies) {
        if (dyn.entity == other.entity) continue;
        if (other.c->type != ColliderType::Box) continue;

        XMVECTOR normal;
        float depth;
        XMFLOAT3 scaledSize = {
            other.c->size.x * other.t->scale.x,
            other.c->size.y * other.t->scale.y,
            other.c->size.z * other.t->scale.z
        };

        if (CheckSphereOBB(dyn.t->position, dyn.c->radius, other.t->position,
                           scaledSize, other.t->rotation, normal, depth)) {
          // ホールはトリガーのみ
          if (ctx.world.Has<GolfHole>(other.entity)) {
            events->events.push_back({dyn.entity, other.entity});
            continue;
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
            float bounce = (dyn.rb->restitution + other.rb->restitution) * 0.4f;
            vel = XMVectorSubtract(vel, XMVectorScale(normal, vn * (1.0f + bounce)));
            XMStoreFloat3(&dyn.rb->velocity, vel);
          }
        }
      }
    }
  }

  // デバッグログ出力
  if (debugTimer > 0.25f) {
    debugTimer = 0.0f;
    
    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider &) {
          if (golfState && e == golfState->ballEntity) {
            XMVECTOR vel = XMLoadFloat3(&rb.velocity);
            float speed = SafeLength(vel);
            bool grounded = t.position.y < 1.0f; // 簡易判定
            LOG_DEBUG("Physics",
                      "ball speed={:.3f} grounded={} pos=({:.3f},{:.3f},{:.3f})",
                      speed, grounded ? "Y" : "N", t.position.x, t.position.y,
                      t.position.z);
          }
        });
  }
}

} // namespace game::systems
