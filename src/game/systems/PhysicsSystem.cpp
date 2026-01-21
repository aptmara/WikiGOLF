#include "PhysicsSystem.h"
#include "../../core/Input.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h" // Flipper is needed, but Heading logic is removed
#include <algorithm>
#include <cmath>
#include <vector>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

/**
 * @brief 球体とOBB（有向境界ボックス）の衝突判定
 */
bool CheckSphereOBB(const XMFLOAT3 &spherePos, float radius,
                    const XMFLOAT3 &boxPos, const XMFLOAT3 &boxSize,
                    const XMFLOAT4 &boxRot, XMVECTOR &outNormal,
                    float &outDepth) {
  XMVECTOR sPos = XMLoadFloat3(&spherePos);
  XMVECTOR bPos = XMLoadFloat3(&boxPos);
  XMVECTOR bSize = XMLoadFloat3(&boxSize);
  XMVECTOR bRot = XMLoadFloat4(&boxRot);

  // 1. 球をボックスのローカル座標系に変換
  XMVECTOR relPos = XMVectorSubtract(sPos, bPos);
  XMVECTOR invRot = XMQuaternionInverse(bRot);
  XMVECTOR localPos = XMVector3Rotate(relPos, invRot);

  // 2. ローカル座標系でのAABB判定（クランプ）
  XMVECTOR closestLocal = XMVectorClamp(localPos, XMVectorNegate(bSize), bSize);

  // 3. 距離チェック
  XMVECTOR distVecLocal = XMVectorSubtract(localPos, closestLocal);
  XMVECTOR distSq = XMVector3LengthSq(distVecLocal);

  float d2;
  XMStoreFloat(&d2, distSq);

  if (d2 > radius * radius)
    return false;

  float d = sqrtf(d2);

  // 特殊ケース: 中心が内部にある場合
  if (d < 0.0001f) {
    // 簡易的にローカルY軸方向へ押し出し
    XMVECTOR localNormal = XMVectorSet(0, 1, 0, 0);
    outNormal = XMVector3Rotate(localNormal, bRot);
    outDepth = radius;
    return true;
  }

  // 4. 法線をワールド座標系へ変換
  XMVECTOR localNormal = XMVectorScale(distVecLocal, 1.0f / d);
  outNormal = XMVector3Rotate(localNormal, bRot);
  outDepth = radius - d;
  return true;
}

// ヘルパー：地形の高さと法線を取得
bool GetTerrainHeightAndNormal(const systems::TerrainData& terrain, float x, float z, float& outHeight, XMVECTOR& outNormal) {
    float width = terrain.config.worldWidth;
    float depth = terrain.config.worldDepth;
    int resX = terrain.config.resolutionX;
    int resZ = terrain.config.resolutionZ;

    // UV座標 (0.0~1.0)
    // Mesh生成時の座標系: px = (u - 0.5) * width, pz = (0.5 - v) * depth
    // u = (px / width) + 0.5
    // v = 0.5 - (pz / depth)
    
    float u = (x / width) + 0.5f;
    float v = 0.5f - (z / depth);

    if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return false;

    float fx = u * (resX - 1);
    float fz = v * (resZ - 1);
    
    int ix = (int)fx;
    int iz = (int)fz;
    
    if (ix >= resX - 1) ix = resX - 2;
    if (iz >= resZ - 1) iz = resZ - 2;
    
    float dx = fx - ix;
    float dz = fz - iz;
    
    auto GetInfo = [&](int gx, int gz, float& h, XMVECTOR& n) {
        int idx = gz * resX + gx;
        if (idx >= 0 && idx < terrain.heightMap.size()) {
            h = terrain.heightMap[idx];
            n = XMLoadFloat3(&terrain.normals[idx]);
        } else {
            h = 0.0f;
            n = XMVectorSet(0, 1, 0, 0);
        }
    };
    
    float h00, h10, h01, h11;
    XMVECTOR n00, n10, n01, n11;
    
    GetInfo(ix, iz, h00, n00);
    GetInfo(ix+1, iz, h10, n10);
    GetInfo(ix, iz+1, h01, n01);
    GetInfo(ix+1, iz+1, h11, n11);
    
    // 三角形分割 (左上分割)
    if (dx + dz <= 1.0f) {
        // Upper Left: (0,0)-(1,0)-(0,1)
        outHeight = h00 * (1.0f - dx - dz) + h10 * dx + h01 * dz;
        outNormal = XMVectorScale(n00, 1.0f - dx - dz) + XMVectorScale(n10, dx) + XMVectorScale(n01, dz);
    } else {
        // Lower Right: (1,1)-(0,1)-(1,0)
        outHeight = h11 * (dx + dz - 1.0f) + h01 * (1.0f - dx) + h10 * (1.0f - dz);
        outNormal = XMVectorScale(n11, dx + dz - 1.0f) + XMVectorScale(n01, 1.0f - dx) + XMVectorScale(n10, 1.0f - dz);
    }
    
    outNormal = XMVector3Normalize(outNormal);
    return true;
}

void PhysicsSystem(core::GameContext &ctx, float dt) {
  // 0. フリッパー制御 (省略なし)
  float flipperSpeed = 15.0f * dt;
  ctx.world.Query<Transform, Flipper>().Each(
      [&](ecs::Entity, Transform &t, Flipper &f) {
        bool pressed = false;
        if (f.side == Flipper::Left && ctx.input.GetKey('Z'))
          pressed = true;
        if (f.side == Flipper::Right && ctx.input.GetKey(VK_OEM_2))
          pressed = true; // '/' key

        float target = pressed ? 1.0f : 0.0f;

        // 線形補間
        if (f.currentParam < target) {
          f.currentParam += flipperSpeed;
          if (f.currentParam > 1.0f)
            f.currentParam = 1.0f;
        } else {
          f.currentParam -= flipperSpeed;
          if (f.currentParam < 0.0f)
            f.currentParam = 0.0f;
        }

        // 回転更新
        float angleDeg = f.currentParam * f.maxAngle;
        if (f.side == Flipper::Left) {
          angleDeg *= -1.0f;
        } else {
          angleDeg *= 1.0f;
        }

        XMVECTOR q = XMQuaternionRotationAxis(XMVectorSet(0, 1, 0, 0),
                                              XMConvertToRadians(angleDeg));
        XMStoreFloat4(&t.rotation, q);
      });

  // DTの急激な変化（ラグ）による物理崩壊（すり抜け）を防ぐため、DTをキャップする
  float clampedDt = (std::min)(dt, 0.05f);

  const int subSteps = 4;
  float subDt = clampedDt / static_cast<float>(subSteps);
  const XMVECTOR gravity = XMVectorSet(0.0f, -9.8f, 0.0f, 0.0f);

  // 0. イベントリソースの準備
  auto *events = ctx.world.GetGlobal<CollisionEvents>();
  if (!events) {
    CollisionEvents newEvents;
    ctx.world.SetGlobal(std::move(newEvents));
    events = ctx.world.GetGlobal<CollisionEvents>();
  }
  events->events.clear();

  // 地形コライダーの取得 (シーンに1つと仮定)
  systems::TerrainData* terrainData = nullptr;
  ctx.world.Query<TerrainCollider>().Each([&](ecs::Entity, TerrainCollider& tc) {
      if (tc.data) {
          terrainData = tc.data.get();
      }
  });

  // ホール情報の収集（吸引用）
  struct HoleInfo {
      XMVECTOR position;
      float radius;
      float gravity;
  };
  std::vector<HoleInfo> holes;
  ctx.world.Query<Transform, GolfHole>().Each([&](ecs::Entity, Transform& t, GolfHole& h) {
      holes.push_back({XMLoadFloat3(&t.position), h.radius, h.gravity});
  });

  for (int step = 0; step < subSteps; ++step) {
    // 1. 積分ステップ (動的オブジェクトの移動)
    ctx.world.Query<Transform, RigidBody, Collider>().Each(
        [&](ecs::Entity e, Transform &t, RigidBody &rb, Collider& col) {
          if (rb.isStatic)
            return;

          XMVECTOR pos = XMLoadFloat3(&t.position);
          XMVECTOR vel = XMLoadFloat3(&rb.velocity);
          XMVECTOR acc = XMLoadFloat3(&rb.acceleration);

          // 地形衝突判定 & 接地判定
          bool isGrounded = false;
          XMVECTOR groundNormal = XMVectorSet(0, 1, 0, 0);
          
          if (terrainData && col.type == ColliderType::Sphere) {
              float radius = col.radius;
              float terrainH = 0.0f;
              XMVECTOR terrainN;
              
              if (GetTerrainHeightAndNormal(*terrainData, XMVectorGetX(pos), XMVectorGetZ(pos), terrainH, terrainN)) {
                  float ballBottom = XMVectorGetY(pos) - radius;
                  if (ballBottom < terrainH) {
                      // めり込み修正 (押し出し)
                      pos = XMVectorSetY(pos, terrainH + radius);
                      
                      // 速度の法線成分を確認 (反射)
                      float vn = XMVectorGetX(XMVector3Dot(vel, terrainN));
                      if (vn < 0.0f) {
                          float res = rb.restitution * 0.5f; 
                          XMVECTOR j = XMVectorScale(terrainN, -(1.0f + res) * vn);
                          vel = XMVectorAdd(vel, j);
                      }
                      
                      isGrounded = true;
                      groundNormal = terrainN;
                  } else if (ballBottom < terrainH + 0.1f) {
                      // 接地判定のマージン (少し広めに)
                      isGrounded = true;
                      groundNormal = terrainN;
                  }
              }
          } else {
             // Fallback: Plane collision (y=0) if no terrain data
             float yPos = XMVectorGetY(pos);
             float yVel = XMVectorGetY(vel);
             if (yPos <= 0.0f + col.radius) {
                 if (yPos < 0.0f + col.radius) {
                      pos = XMVectorSetY(pos, col.radius);
                      float vn = XMVectorGetY(vel);
                      if (vn < 0.0f) {
                          vel = XMVectorSetY(vel, -vn * rb.restitution);
                      }
                 }
                 isGrounded = (std::abs(yVel) < 1.0f);
             }
          }

          // ホール吸引力
          if (col.type == ColliderType::Sphere) { // ボールのみ吸い込む
              for(const auto& h : holes) {
                  XMVECTOR toHole = XMVectorSubtract(h.position, pos);
                  toHole = XMVectorSetY(toHole, 0.0f); // 水平方向のみ
                  
                  float distSq = XMVectorGetX(XMVector3LengthSq(toHole));
                  float range = h.radius * 4.0f; // 吸い込み範囲
                  
                  if (distSq < range * range && distSq > 0.0001f) {
                      float dist = std::sqrt(distSq);
                      XMVECTOR dir = XMVectorScale(toHole, 1.0f / dist);
                      
                      // 距離に応じた強さ (線形減衰)
                      float factor = std::max(0.0f, 1.0f - (dist / range));
                      // 中心に近いほど強く
                      float force = h.gravity * factor * 10.0f; 
                      
                      acc = XMVectorAdd(acc, XMVectorScale(dir, force));
                  }
              }
          }

          // 重力加算
          acc = XMVectorAdd(acc, gravity);

          // 空気抵抗 (Air Drag)
          vel = XMVectorScale(vel, (1.0f - rb.drag * subDt)); 

          // 転がり抵抗 (Rolling Friction) & 斜面滑走
          if (isGrounded) {
             // 斜面での加速成分：重力のうち斜面に沿った成分はすでに acc に含まれている。
             // しかし、垂直抗力によって打ち消される法線成分を除去する必要がある。
             // 重力 G = (0, -g, 0)。 法線 N。
             // 斜面方向の力 F = G - (G.N)N ... だが、ここでは「速度」を操作して擬似的に摩擦と垂直抗力を表現する。
             
             // 1. 速度の法線成分（壁に向かう成分）を除去（完全に滑る斜面）
             float vn = XMVectorGetX(XMVector3Dot(vel, groundNormal));
             // もし速度が地面に食い込む方向ならキャンセル
             if (vn < 0.0f) {
                 vel = XMVectorSubtract(vel, XMVectorScale(groundNormal, vn));
             }
             
             // 2. 摩擦適用
             XMVECTOR xzVel = vel; // 3次元速度全体に対して摩擦をかけるべきだが、ここでは簡易的に速度ベクトルを縮める
             float speed = XMVectorGetX(XMVector3Length(xzVel));

             if (speed > 0.001f) {
               float frictionCoeff = rb.rollingFriction;
               if (terrainData) {
                   frictionCoeff *= terrainData->config.friction;
               }
               
               float frictionForce = frictionCoeff * subDt;
               // 斜面では摩擦が少し減るかも？ (N.Y 成分に応じて)
               // float slopeFactor = std::max(0.2f, XMVectorGetY(groundNormal));
               // frictionForce *= slopeFactor;

               if (speed < frictionForce) {
                 vel = XMVectorSet(0, 0, 0, 0);
               } else {
                 float newSpeed = speed - frictionForce;
                 vel = XMVectorScale(vel, newSpeed / speed);
               }
             }
          }

          // 速度更新 (v += a * dt)
          vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));

          // 位置更新 (p += v * dt)
          pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));

          // 停止閾値 (Sleep Threshold)
          float vSq;
          XMStoreFloat(&vSq, XMVector3LengthSq(vel));
          if (vSq < 0.1f * 0.1f && isGrounded) {
            vel = XMVectorSet(0, 0, 0, 0);
          }

          // 落下判定
          if (XMVectorGetY(pos) < -50.0f) {
            // Scene handles respawn
          }

          // 加速度リセット
          rb.acceleration = {0, 0, 0};

          XMStoreFloat3(&t.position, pos);
          XMStoreFloat3(&rb.velocity, vel);
        });


    // 2. 衝突検出と応答 (既存コードの維持)
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

    for (auto &dyn : dynamicBodies) {
      if (dyn.c->type != ColliderType::Sphere)
        continue;

      for (auto &other : staticBodies) {
        if (dyn.entity == other.entity)
          continue;

        bool isColliding = false;
        XMVECTOR normal;
        float depth;

        if (other.c->type == ColliderType::Box) {
          XMFLOAT3 scaledSize = {other.c->size.x * other.t->scale.x,
                                 other.c->size.y * other.t->scale.y,
                                 other.c->size.z * other.t->scale.z};
          if (CheckSphereOBB(dyn.t->position, dyn.c->radius, other.t->position,
                             scaledSize, other.t->rotation, normal, depth)) {
            isColliding = true;
          }
        }

        if (isColliding) {
          events->events.push_back({dyn.entity, other.entity});

          XMVECTOR pos = XMLoadFloat3(&dyn.t->position);
          pos = XMVectorAdd(pos, XMVectorScale(normal, depth));
          XMStoreFloat3(&dyn.t->position, pos);

          XMVECTOR vel = XMLoadFloat3(&dyn.rb->velocity);
          XMVECTOR vDotN = XMVector3Dot(vel, normal);
          float vn = XMVectorGetX(vDotN);

          if (vn < 0) {
            float extraImpulse = 0.0f;
            if (ctx.world.Has<Flipper>(other.entity)) {
              auto *f = ctx.world.Get<Flipper>(other.entity);
              bool isMoving =
                  (f->side == Flipper::Left && ctx.input.GetKey('Z')) ||
                  (f->side == Flipper::Right && ctx.input.GetKey(VK_OEM_2));

              bool isKicking = isMoving && (f->currentParam < 1.0f);

              if (isKicking) {
                extraImpulse = 25.0f;
              }
            }

            // 反発係数を少し抑える (ゴルフボールっぽく)
            float e = (dyn.rb->restitution + other.rb->restitution) *
                      0.4f; // 0.5 -> 0.4

            XMVECTOR j = XMVectorScale(normal, -(1.0f + e) * vn + extraImpulse);
            vel = XMVectorAdd(vel, j);
            XMStoreFloat3(&dyn.rb->velocity, vel);
          }
        }
      }
    }
  }
}

} // namespace game::systems
