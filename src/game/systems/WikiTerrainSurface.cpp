/**
 * @file WikiTerrainSurface.cpp
 * @brief コースの地形から、手続き的に生やす芝の元データを作る
 */

#include "WikiTerrainSystem.h"
#include "GrassFieldBuilder.h"
#include "../../core/GameContext.h"
#include "../../core/Logger.h"
#include "../../ecs/World.h"
#include "../components/GrassField.h"
#include "core/Profiler.h"

namespace game::systems {

void WikiTerrainSystem::CreateSurfaceGrass(core::GameContext &ctx) {
  PROFILE_SCOPE("WikiTerrain.BuildGrassField");
  auto *source = ctx.world.GetGlobal<components::GrassFieldSource>();
  const uint64_t version = source ? source->version + 1 : 1;
  components::GrassFieldSource next;
  next.version = version;
  if (m_terrainData && !m_terrainData->heightMap.empty() &&
      !m_terrainData->materialMap.empty()) {
    GrassFieldTerrainView view;
    view.heights = m_terrainData->heightMap.data();
    view.materials = m_terrainData->materialMap.data();
    view.resolutionX = m_terrainData->config.resolutionX;
    view.resolutionZ = m_terrainData->config.resolutionZ;
    view.width = m_terrainData->config.worldWidth;
    view.depth = m_terrainData->config.worldDepth;
    next.data = BuildGrassField(view);
    if (next.data) {
      LOG_INFO("WikiTerrain", "Grass field {}x{} texels", next.data->fieldResolutionX,
               next.data->fieldResolutionZ);
    }
  }
  ctx.world.SetGlobal(std::move(next));
}

void WikiTerrainSystem::ClearSurfaceGrass(core::GameContext &ctx) {
  auto *source = ctx.world.GetGlobal<components::GrassFieldSource>();
  if (source && source->data) {
    components::GrassFieldSource cleared;
    cleared.version = source->version + 1;
    ctx.world.SetGlobal(std::move(cleared));
  }
}

} // namespace game::systems
