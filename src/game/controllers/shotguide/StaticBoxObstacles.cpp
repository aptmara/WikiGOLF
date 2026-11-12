/**
 * @file StaticBoxObstacles.cpp
 * @brief ショット予測に使う、動かない箱の集まりの実装
*/

#include "StaticBoxObstacles.h"
#include "../../../ecs/World.h"
#include "../../components/PhysicsComponents.h"
#include "../../components/Transform.h"
#include "../../components/WikiComponents.h"
#include "../../systems/PhysicsSystemInternals.h"
#include <algorithm>
#include <cmath>

namespace game::guide {

using namespace DirectX;
using namespace game::components;

void StaticBoxObstacles::Collect(ecs::World &world) {
  m_boxes.clear();
  world.Query<Transform, RigidBody, Collider>().Each(
      [&](ecs::Entity entity, Transform &transform, RigidBody &body,
          Collider &collider) {
        if (!body.isStatic || collider.type != ColliderType::Box ||
            world.Has<GolfHole>(entity)) {
          return;
        }
        Box box;
        box.center = transform.position;
        box.size = {collider.size.x * transform.scale.x,
                    collider.size.y * transform.scale.y,
                    collider.size.z * transform.scale.z};
        box.rotation = transform.rotation;
        box.restitution = body.restitution;
        box.reach = 0.5f * std::sqrt(box.size.x * box.size.x +
                                     box.size.y * box.size.y +
                                     box.size.z * box.size.z);
        m_boxes.push_back(box);
      });
  ++m_revision;
}

bool StaticBoxObstacles::Resolve(game::physics::BallSimState &state,
                                 float ballRadius,
                                 float ballRestitution) const {
  bool hit = false;
  for (const Box &box : m_boxes) {
    // まず球どうしの距離で大まかにふるい落とす
    const float dx = state.position.x - box.center.x;
    const float dy = state.position.y - box.center.y;
    const float dz = state.position.z - box.center.z;
    const float range = box.reach + ballRadius;
    if (dx * dx + dy * dy + dz * dz > range * range) {
      continue;
    }

    XMVECTOR normal;
    float depth = 0.0f;
    if (!game::systems::CheckSphereOBB(state.position, ballRadius, box.center,
                                       box.size, box.rotation, normal,
                                       depth)) {
      continue;
    }
    hit = true;

    // 実際の物理(ResolveStaticCollisions)と同じ押し出しと反射。
    // 実際の跳ね返りには毎回わずかな揺らぎが乗るが、予測では平均値を使う。
    XMVECTOR position = XMLoadFloat3(&state.position);
    position = XMVectorAdd(position, XMVectorScale(normal, depth));
    XMStoreFloat3(&state.position, position);

    XMVECTOR velocity = XMLoadFloat3(&state.velocity);
    const float vn = XMVectorGetX(XMVector3Dot(velocity, normal));
    if (vn < 0.0f) {
      const float bounce =
          std::max(0.0f, (ballRestitution + box.restitution) * 0.5f);
      velocity = XMVectorSubtract(velocity,
                                  XMVectorScale(normal, vn * (1.0f + bounce)));
      XMStoreFloat3(&state.velocity, velocity);
    }
  }
  return hit;
}

} // namespace game::guide
