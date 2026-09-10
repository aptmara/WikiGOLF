#include "DebugColliderRenderer.h"

#include "DebugColliderGeometry.h"
#include "DebugCupInStatus.h"
#include "DebugTerrainMaterialGeometry.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/Camera.h"
#include "../components/PhysicsComponents.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"
#include "../utils/GameplayPhysicsConstants.h"
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

void DrawContact(ImDrawList &drawList, const CollisionEvent &event,
                 const ProjectionContext &projection,
                 const DebugColliderSettings &settings) {
  ImVec2 contact;
  if (!Project(event.contactPoint, projection, contact)) {
    return;
  }
  if (settings.contactPoints) {
    drawList.AddCircleFilled(contact, 4.0f, IM_COL32(255, 80, 230, 255));
  }
  if (!settings.collisionNormals) {
    return;
  }
  const float arrowLength = (std::max)(0.5f, event.penetrationDepth * 4.0f);
  const XMFLOAT3 tip3D = {
      event.contactPoint.x + event.normal.x * arrowLength,
      event.contactPoint.y + event.normal.y * arrowLength,
      event.contactPoint.z + event.normal.z * arrowLength};
  ImVec2 tip;
  if (Project(tip3D, projection, tip)) {
    drawList.AddLine(contact, tip, IM_COL32(255, 180, 30, 255), 2.0f);
    const ImVec2 direction = {tip.x - contact.x, tip.y - contact.y};
    const float length = std::sqrt(direction.x * direction.x +
                                   direction.y * direction.y);
    if (length > 0.01f) {
      const ImVec2 unit = {direction.x / length, direction.y / length};
      const ImVec2 side = {-unit.y, unit.x};
      drawList.AddTriangleFilled(
          tip, {tip.x - unit.x * 8.0f + side.x * 4.0f,
                tip.y - unit.y * 8.0f + side.y * 4.0f},
          {tip.x - unit.x * 8.0f - side.x * 4.0f,
           tip.y - unit.y * 8.0f - side.y * 4.0f},
          IM_COL32(255, 180, 30, 255));
    }
  }
}

ImU32 TerrainMaterialColor(uint8_t material) {
  switch (static_cast<TerrainMaterial>(material)) {
  case TerrainMaterial::Fairway: return IM_COL32(60, 230, 80, 230);
  case TerrainMaterial::Rough: return IM_COL32(150, 210, 45, 230);
  case TerrainMaterial::Bunker: return IM_COL32(245, 205, 70, 230);
  case TerrainMaterial::Green: return IM_COL32(40, 255, 135, 230);
  case TerrainMaterial::Ice: return IM_COL32(80, 220, 255, 230);
  case TerrainMaterial::Water: return IM_COL32(40, 100, 255, 230);
  case TerrainMaterial::Lava: return IM_COL32(255, 55, 25, 230);
  case TerrainMaterial::Stone: return IM_COL32(155, 155, 165, 230);
  case TerrainMaterial::None: return IM_COL32(255, 255, 255, 160);
  }
  return IM_COL32(255, 255, 255, 160);
}

} // namespace

void DebugColliderRenderer::Draw(core::GameContext &ctx,
                                 const DebugColliderSettings &settings) {
  if (settings.trailClearGeneration != m_seenTrailClearGeneration) {
    m_ballTrail.Clear();
    m_seenTrailClearGeneration = settings.trailClearGeneration;
  }
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
        [&](ecs::Entity entity, TerrainCollider &terrain,
            Transform &transform) {
          if (!terrain.data) {
            return;
          }
          std::vector<DebugLine3D> lines;
          const int maximumResolution = (std::max)(
              terrain.data->config.resolutionX,
              terrain.data->config.resolutionZ);
          const int stride = (std::max)(1, maximumResolution / 24);
          AppendTerrainHeightfieldLines(
              lines, terrain.data->heightMap,
              terrain.data->config.resolutionX,
              terrain.data->config.resolutionZ,
              terrain.data->config.worldWidth,
              terrain.data->config.worldDepth, transform.position,
              game::physics::kTerrainVisualSurfaceOffset, stride);
          const ImU32 color = collidingEntities.contains(entity)
                                  ? IM_COL32(255, 55, 55, 255)
                                  : IM_COL32(60, 150, 255, 220);
          DrawLines(drawList, lines, projection, color);
        });
  }

  if (settings.terrainMaterials) {
    ctx.world.Query<TerrainCollider, Transform>().Each(
        [&](ecs::Entity, TerrainCollider &terrain, Transform &transform) {
          if (!terrain.data) {
            return;
          }
          const int maximumResolution = (std::max)(
              terrain.data->config.resolutionX,
              terrain.data->config.resolutionZ);
          std::vector<DebugTerrainMaterialLine> materialLines;
          AppendTerrainMaterialLines(
              materialLines, *terrain.data, transform.position,
              game::physics::kTerrainVisualSurfaceOffset + 0.02f,
              (std::max)(1, maximumResolution / 32));
          for (const auto &materialLine : materialLines) {
            ImVec2 from;
            ImVec2 to;
            if (Project(materialLine.line.from, projection, from) &&
                Project(materialLine.line.to, projection, to)) {
              drawList.AddLine(from, to,
                               TerrainMaterialColor(materialLine.material),
                               2.0f);
            }
          }
        });
  }

  if (const auto *events = ctx.world.GetGlobal<CollisionEvents>()) {
    for (const auto &event : events->events) {
      DrawContact(drawList, event, projection, settings);
    }
  }

  if (settings.velocityVector) {
    const auto *state = ctx.world.GetGlobal<GolfGameState>();
    if (state) {
      const ecs::Entity ball = static_cast<ecs::Entity>(state->ballEntity);
      const auto *transform = ctx.world.Get<Transform>(ball);
      const auto *body = ctx.world.Get<RigidBody>(ball);
      if (transform && body) {
        std::vector<DebugLine3D> velocityLines;
        AppendVectorArrow(velocityLines, transform->position, body->velocity,
                          0.2f);
        DrawLines(drawList, velocityLines, projection,
                  IM_COL32(255, 225, 40, 255));
      }
    }
  }

  if (settings.ballTrail) {
    const auto *state = ctx.world.GetGlobal<GolfGameState>();
    if (state) {
      const ecs::Entity ball = static_cast<ecs::Entity>(state->ballEntity);
      const auto *transform = ctx.world.Get<Transform>(ball);
      if (transform) {
        m_ballTrail.Update(ball, transform->position,
                           settings.trailSampleInterval,
                           static_cast<std::size_t>(
                               settings.trailMaximumPoints));
      }
    }
    const auto &points = m_ballTrail.Points();
    for (std::size_t index = 1; index < points.size(); ++index) {
      ImVec2 from;
      ImVec2 to;
      if (!Project(points[index - 1], projection, from) ||
          !Project(points[index], projection, to)) {
        continue;
      }
      const float ratio = static_cast<float>(index) /
                          static_cast<float>(points.size());
      drawList.AddLine(from, to,
                       IM_COL32(40, 220, 255,
                                static_cast<int>(60.0f + ratio * 195.0f)),
                       2.0f);
    }
  }

  if (settings.cupInGuide) {
    const DebugCupInStatus status = CaptureCupInStatus(ctx.world);
    if (status.available) {
      std::vector<DebugLine3D> guideLines;
      const XMFLOAT3 center = {status.holePosition.x,
                               status.holePosition.y - 0.5f,
                               status.holePosition.z};
      AppendCylinderLines(guideLines, center, status.captureRadius, 1.0f,
                          {0.0f, 0.0f, 0.0f, 1.0f});
      guideLines.push_back({status.ballPosition, status.holePosition});
      const ImU32 color = status.readyForCupIn
                              ? IM_COL32(50, 240, 90, 255)
                              : IM_COL32(255, 80, 190, 255);
      DrawLines(drawList, guideLines, projection, color);
    }
  }
}

} // namespace game::debug
