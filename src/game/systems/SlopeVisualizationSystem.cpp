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
#include <algorithm>
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
  m_fadeAlpha = 0.0f;
  m_hasMesh = false;
  m_lastBuildCenter = {1.0e9f, 0.0f, 1.0e9f};
}

void SlopeVisualizationSystem::Update(core::GameContext &ctx, float dt,
                                      bool visible,
                                      const DirectX::XMFLOAT3 &center,
                                      const WikiTerrainSystem *terrain) {
  // フェード係数を目標値(表示=1、非表示=0)へdt分だけ近づける
  const float target = (visible && terrain) ? 1.0f : 0.0f;
  const float step = (kFadeDuration > 0.0f) ? (dt / kFadeDuration) : 1.0f;
  if (m_fadeAlpha < target) {
    m_fadeAlpha = (std::min)(target, m_fadeAlpha + step);
  } else if (m_fadeAlpha > target) {
    m_fadeAlpha = (std::max)(target, m_fadeAlpha - step);
  }

  if (visible && terrain) {
    const float dx = center.x - m_lastBuildCenter.x;
    const float dz = center.z - m_lastBuildCenter.z;
    const bool needsRebuild =
        !m_hasMesh || std::sqrt(dx * dx + dz * dz) > kRebuildDistance;

    if (needsRebuild) {
      RebuildMesh(ctx, center, *terrain);
      m_lastBuildCenter = center;
    }
  }

  ApplyFade(ctx);
}

void SlopeVisualizationSystem::ApplyFade(core::GameContext &ctx) {
  if (m_overlayEntity == UINT32_MAX) return;
  auto *mr = ctx.world.Get<MeshRenderer>(m_overlayEntity);
  if (!mr) return;
  mr->isVisible = m_fadeAlpha > 0.001f;
  mr->customFlags.x = m_fadeAlpha;
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
    mr.isVisible = false; // 直後のApplyFade()でフェード係数に応じて反映される
  } else if (auto *mr = ctx.world.Get<MeshRenderer>(m_overlayEntity)) {
    mr->mesh = meshHandle;
  }

  m_hasMesh = true;
}

} // namespace game::systems
