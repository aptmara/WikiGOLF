/**
 * @file PhysicsSystemStaticCollision.cpp
 * @brief 静的ボックスとの衝突解決
*/

#include "PhysicsSystemInternals.h"
#include "CollisionDebugInfo.h"
#include "../../core/Logger.h"

namespace game::systems {

using namespace DirectX;
using namespace game::components;

void ResolveStaticCollisions(PhysicsUpdateContext &frame) {
  auto &ctx = frame.gameContext;
  const float subDt = frame.subDt;
  const StaticBodySpatialGrid &staticBodyGrid = frame.staticBodyGrid;
  auto &dynamicBodies = frame.dynamicBodies;
  CollisionEvents *events = &frame.events;
  auto &perfStats = frame.perfStats;
  auto &jitterCursor = frame.jitterCursor;

    // 静的オブジェクトとの衝突
    for (auto &dyn : dynamicBodies) {
      if (dyn.c->type != ColliderType::Sphere)
        continue;

      const float horizontalSpeed = std::sqrt(
          dyn.rb->velocity.x * dyn.rb->velocity.x +
          dyn.rb->velocity.z * dyn.rb->velocity.z);
      const float travelDistance = horizontalSpeed * subDt;
      const float queryRadius = dyn.c->radius + travelDistance * 0.5f + 0.25f;
      const float queryX = dyn.t->position.x - dyn.rb->velocity.x * subDt * 0.5f;
      const float queryZ = dyn.t->position.z - dyn.rb->velocity.z * subDt * 0.5f;
      staticBodyGrid.Query(queryX, queryZ, queryRadius,
                           [&](ecs::Entity otherEntity) {
         ++perfStats.staticCandidates;
         auto *otherT = ctx.world.Get<Transform>(otherEntity);
         auto *otherRb = ctx.world.Get<RigidBody>(otherEntity);
         auto *otherC = ctx.world.Get<Collider>(otherEntity);
         if (!otherT || !otherRb || !otherRb->isStatic || !otherC ||
             otherC->type != ColliderType::Box) {
           return;
         }

         XMVECTOR normal;
         float depth;
         XMFLOAT3 scaledSize = {otherC->size.x * otherT->scale.x,
                                otherC->size.y * otherT->scale.y,
                                otherC->size.z * otherT->scale.z};

         ++perfStats.staticChecks;
         if (CheckSphereOBB(dyn.t->position, dyn.c->radius, otherT->position,
                            scaledSize, otherT->rotation, normal, depth)) {
           XMFLOAT3 normalValue;
           XMStoreFloat3(&normalValue, normal);
           const CollisionEvent collision = MakeSphereCollisionEvent(
               dyn.entity, otherEntity, dyn.t->position, dyn.c->radius,
               normalValue, depth);
           // ホールはトリガーのみ
           if (ctx.world.Has<GolfHole>(otherEntity)) {
             events->events.push_back(collision);
             return;
           }

           events->events.push_back(collision);

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
                 std::max(0.0f, (dyn.rb->restitution + otherRb->restitution) *
                                    0.5f * jitter);
            vel = XMVectorSubtract(vel,
                                   XMVectorScale(normal, vn * (1.0f + bounce)));
            XMStoreFloat3(&dyn.rb->velocity, vel);
          }
        }
      });
    }
}

} // namespace game::systems
