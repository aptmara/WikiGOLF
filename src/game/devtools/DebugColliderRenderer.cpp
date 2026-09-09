#include "DebugColliderRenderer.h"

#include "DebugColliderGeometry.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace game::debug {
namespace {

using namespace DirectX;
using namespace game::components;

struct ProjectionContext {
  XMMATRIX viewProjection;
  ImVec2 displaySize;
};

bool FindProjection(core::GameContext &ctx, ProjectionContext &projection) {
  bool found = false;
  ctx.world.Query<Camera, Transform>().Each(
      [&](ecs::Entity, Camera &camera, Transform &transform) {
        if (found && !camera.isMainCamera) {
          return;
        }
        projection.viewProjection =
            camera.GetViewMatrix(transform) * camera.GetProjectionMatrix();
        projection.displaySize = ImGui::GetIO().DisplaySize;
        found = true;
      });
  return found && projection.displaySize.x > 0.0f &&
         projection.displaySize.y > 0.0f;
}

bool Project(const XMFLOAT3 &point, const ProjectionContext &projection,
             ImVec2 &screen) {
  const XMVECTOR clip = XMVector4Transform(
      XMVectorSet(point.x, point.y, point.z, 1.0f), projection.viewProjection);
  const float w = XMVectorGetW(clip);
  if (w <= 0.001f) {
    return false;
  }
  const float x = XMVectorGetX(clip) / w;
  const float y = XMVectorGetY(clip) / w;
  screen.x = (x * 0.5f + 0.5f) * projection.displaySize.x;
  screen.y = (-y * 0.5f + 0.5f) * projection.displaySize.y;
  return std::isfinite(screen.x) && std::isfinite(screen.y);
}

XMFLOAT3 ColliderCenter(const Transform &transform, const Collider &collider) {
  XMFLOAT3 scaledOffset = {collider.offset.x * transform.scale.x,
                           collider.offset.y * transform.scale.y,
                           collider.offset.z * transform.scale.z};
  XMVECTOR offset = XMVector3Rotate(XMLoadFloat3(&scaledOffset),
                                    XMLoadFloat4(&transform.rotation));
  XMFLOAT3 center;
  XMStoreFloat3(&center,
                XMVectorAdd(XMLoadFloat3(&transform.position), offset));
  return center;
}

void DrawLines(ImDrawList &drawList, const std::vector<DebugLine3D> &lines,
               const ProjectionContext &projection, ImU32 color) {
  for (const auto &line : lines) {
    ImVec2 from;
    ImVec2 to;
    if (Project(line.from, projection, from) &&
        Project(line.to, projection, to)) {
      drawList.AddLine(from, to, color, 1.5f);
    }
  }
}

} // namespace

void DebugColliderRenderer::Draw(core::GameContext &ctx,
                                 const DebugColliderSettings &settings) {
  if (!settings.enabled) {
    return;
  }

  ProjectionContext projection;
  if (!FindProjection(ctx, projection)) {
    return;
  }

  std::unordered_set<ecs::Entity> collidingEntities;
  if (const auto *events = ctx.world.GetGlobal<CollisionEvents>()) {
    for (const auto &event : events->events) {
      collidingEntities.insert(event.entityA);
      collidingEntities.insert(event.entityB);
    }
  }

  ImDrawList &drawList = *ImGui::GetBackgroundDrawList();
  ctx.world.Query<Collider, Transform>().Each(
      [&](ecs::Entity entity, Collider &collider, Transform &transform) {
        const bool isHole = ctx.world.Has<GolfHole>(entity);
        if (isHole && !settings.holes) {
          return;
        }
        if ((!isHole && collider.type == ColliderType::Sphere &&
             !settings.spheres) ||
            (!isHole && collider.type == ColliderType::Box && !settings.boxes) ||
            (!isHole && collider.type == ColliderType::Cylinder &&
             !settings.cylinders)) {
          return;
        }

        std::vector<DebugLine3D> lines;
        const XMFLOAT3 center = ColliderCenter(transform, collider);
        const float radiusScale = (std::max)(
            std::abs(transform.scale.x), std::abs(transform.scale.z));
        switch (collider.type) {
        case ColliderType::Sphere:
          AppendSphereLines(lines, center, collider.radius * radiusScale);
          break;
        case ColliderType::Box:
          AppendBoxLines(lines, center,
                         {collider.size.x * std::abs(transform.scale.x),
                          collider.size.y * std::abs(transform.scale.y),
                          collider.size.z * std::abs(transform.scale.z)},
                         transform.rotation);
          break;
        case ColliderType::Cylinder:
          AppendCylinderLines(lines, center, collider.radius * radiusScale,
                              collider.size.y * std::abs(transform.scale.y),
                              transform.rotation);
          break;
        }

        ImU32 color = IM_COL32(70, 230, 90, 230);
        if (isHole) {
          color = IM_COL32(255, 220, 40, 240);
        }
        if (collidingEntities.contains(entity)) {
          color = IM_COL32(255, 55, 55, 255);
        }
        DrawLines(drawList, lines, projection, color);

        if (settings.entityIds) {
          ImVec2 labelPosition;
          if (Project(center, projection, labelPosition)) {
            const std::string label = "#" + std::to_string(entity);
            drawList.AddText(labelPosition, color, label.c_str());
          }
        }
      });

  if (settings.terrain) {
    ctx.world.Query<TerrainCollider, Transform>().Each(
        [&](ecs::Entity, TerrainCollider &terrain, Transform &transform) {
          if (!terrain.data) {
            return;
          }
          const XMFLOAT3 size = {terrain.data->config.worldWidth, 0.0f,
                                 terrain.data->config.worldDepth};
          std::vector<DebugLine3D> lines;
          AppendBoxLines(lines, transform.position, size, transform.rotation);
          DrawLines(drawList, lines, projection, IM_COL32(60, 150, 255, 220));
        });
  }
}

} // namespace game::debug
