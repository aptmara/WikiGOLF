/**
 * @file SlopeVisualizationSystem.cpp
 * @brief SlopeVisualizationSystemの実装です。
*/

#include "SlopeVisualizationSystem.h"
#include "../../core/GameContext.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "WikiTerrainSystem.h"
#include <cmath>

namespace game::systems {

using namespace DirectX;
using namespace game::components;

namespace {
constexpr const char *kMeshName = "SlopeVisualizationOverlay";
} // namespace

void SlopeVisualizationSystem::Initialize(core::GameContext &ctx) {
  m_shader = ctx.resource.LoadShader(
      "SlopeOverlay", L"Assets/shaders/SlopeOverlayVS.hlsl",
      L"Assets/shaders/SlopeOverlayPS.hlsl");
}

void SlopeVisualizationSystem::Shutdown(core::GameContext &ctx) {
  m_entityOwner.DestroyAll(ctx.world);
  m_overlayEntity = UINT32_MAX;
  m_visible = false;
  m_hasMesh = false;
  m_lastBuildCenter = {1.0e9f, 0.0f, 1.0e9f};
}

void SlopeVisualizationSystem::Update(core::GameContext &ctx, bool visible,
                                      const DirectX::XMFLOAT3 &center,
                                      const WikiTerrainSystem *terrain) {
  if (!visible || !terrain) {
    Hide(ctx);
    return;
  }

  const float dx = center.x - m_lastBuildCenter.x;
  const float dz = center.z - m_lastBuildCenter.z;
  const bool needsRebuild =
      !m_hasMesh || std::sqrt(dx * dx + dz * dz) > kRebuildDistance;

  if (needsRebuild) {
    RebuildMesh(ctx, center, *terrain);
    m_lastBuildCenter = center;
  }

  Show(ctx);
}

void SlopeVisualizationSystem::Show(core::GameContext &ctx) {
  if (m_visible || m_overlayEntity == UINT32_MAX) return;
  if (auto *mr = ctx.world.Get<MeshRenderer>(m_overlayEntity)) {
    mr->isVisible = true;
  }
  m_visible = true;
}

void SlopeVisualizationSystem::Hide(core::GameContext &ctx) {
  if (!m_visible || m_overlayEntity == UINT32_MAX) return;
  if (auto *mr = ctx.world.Get<MeshRenderer>(m_overlayEntity)) {
    mr->isVisible = false;
  }
  m_visible = false;
}

void SlopeVisualizationSystem::RebuildMesh(core::GameContext &ctx,
                                           const DirectX::XMFLOAT3 &center,
                                           const WikiTerrainSystem &terrain) {
  const auto built =
      SlopeVisualizationMeshBuilder::Build(terrain, center, m_config);
  if (built.IsEmpty()) {
    return;
  }

  const auto meshHandle =
      ctx.resource.CreateDynamicMesh(kMeshName, built.vertices, built.indices);

  if (m_overlayEntity == UINT32_MAX) {
    m_overlayEntity = m_entityOwner.Create(ctx.world);
    ctx.world.Add<Transform>(m_overlayEntity);

    auto &mr = ctx.world.Add<MeshRenderer>(m_overlayEntity);
    mr.mesh = meshHandle;
    mr.shader = m_shader;
    mr.color = {1.0f, 1.0f, 1.0f, 1.0f};
    mr.hasTexture = false;
    mr.hasNormalMap = false;
    mr.isTransparent = true;
    mr.blendMode = BlendMode::Alpha;
    mr.isVisible = false; // Show()が呼ばれるまで非表示
  } else if (auto *mr = ctx.world.Get<MeshRenderer>(m_overlayEntity)) {
    mr->mesh = meshHandle;
  }

  m_hasMesh = true;
}

} // namespace game::systems
