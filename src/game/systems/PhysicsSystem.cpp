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

  for (int step = 0; step < subSteps; ++step) {
    // 1. 積分ステップ (動的オブジェクトの移動)
    ctx.world.Query<Transform, RigidBody>().Each(
        [&](ecs::Entity, Transform &t, RigidBody &rb) {
          if (rb.isStatic)
            return;

          XMVECTOR pos = XMLoadFloat3(&t.position);
          XMVECTOR vel = XMLoadFloat3(&rb.velocity);
          XMVECTOR acc = XMLoadFloat3(&rb.acceleration);

          // 接地判定（簡易）: 床の高さに近いか？
          // 本来は衝突判定結果を使うべきだが、ゴルフなのでy=0付近を"地面"とみなして抵抗をかけるアプローチも可
          // ここでは速度のY成分が小さく、かつ地面付近にある場合に転がり抵抗を加える
          float yPos = XMVectorGetY(pos);
          float yVel = XMVectorGetY(vel);

          // 接地判定 (半径0.25f + マージン) かつ Y速度が小さい(跳ねていない)
          bool isGrounded = (yPos <= 0.55f && std::abs(yVel) < 1.0f);
          // ※マップの床の高さは0.0fを想定。球の半径0.1f + マージン

          // 重力加算
          acc = XMVectorAdd(acc, gravity);

          // 空気抵抗 (Air Drag)
          vel = XMVectorScale(vel, (1.0f - rb.drag * subDt)); // dt依存にする

          // 転がり抵抗 (Rolling Friction)
          if (isGrounded) {
            // 水平方向の速度成分を取得
            XMVECTOR xzVel =
                XMVectorSet(XMVectorGetX(vel), 0.0f, XMVectorGetZ(vel), 0.0f);
            float speed = XMVectorGetX(XMVector3Length(xzVel));

            if (speed > 0.001f) {
              float frictionForce = rb.rollingFriction * subDt;

              // 速度を減衰させる（逆方向への力というよりは減衰）
              // 速度が小さい場合はゼロにする
              if (speed < frictionForce) {
                // 停止
                vel = XMVectorSet(0, XMVectorGetY(vel), 0, 0);
              } else {
                float newSpeed = speed - frictionForce;
                float scale = newSpeed / speed;
                // Y成分は変えずにXZ成分のみスケーリング
                XMVECTOR newXZ = XMVectorScale(xzVel, scale);
                vel = XMVectorSet(XMVectorGetX(newXZ), XMVectorGetY(vel),
                                  XMVectorGetZ(newXZ), 0);
              }
            }
          }

          // 速度更新 (v += a * dt)
          vel = XMVectorAdd(vel, XMVectorScale(acc, subDt));

          // 位置更新 (p += v * dt)
          pos = XMVectorAdd(pos, XMVectorScale(vel, subDt));

          // 停止閾値 (Sleep Threshold) - 強化
          float vSq;
          XMStoreFloat(&vSq, XMVector3LengthSq(vel));
          // 速度が非常に小さく、かつ地面付近なら完全停止
          if (vSq < 0.1f * 0.1f && isGrounded) {
            vel = XMVectorSet(0, 0, 0, 0);
          }

          // 床へのめり込み防止（簡易床コリジョン）
          // Wikiマップなどは個別のColliderがあるので、これは安全策
          if (XMVectorGetY(pos) < -50.0f) {
            // 落下死（リスポーンはシーン側で管理）
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
