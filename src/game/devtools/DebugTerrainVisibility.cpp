#include "DebugTerrainVisibility.h"

#include "DebugRenderState.h"
#include "../../ecs/World.h"
#include "../components/MeshRenderer.h"
#include "../components/Transform.h"
#include "../components/WikiComponents.h"

namespace game::debug {

void DebugTerrainVisibility::Apply(ecs::World &world,
                                   const DebugRenderState *state) {
  if (!state) {
    m_originalVisibility.clear();
    return;
  }
  if (state->hideTerrainMeshes) {
    Hide(world);
  } else {
    Restore(world);
  }
}

void DebugTerrainVisibility::Hide(ecs::World &world) {
  using namespace game::components;
  world.Query<TerrainObject, MeshRenderer>().Each(
      [&](ecs::Entity entity, TerrainObject &, MeshRenderer &renderer) {
        if (!m_originalVisibility.contains(entity)) {
          m_originalVisibility.emplace(entity, renderer.isVisible);
        }
        renderer.isVisible = false;
      });
}

void DebugTerrainVisibility::Restore(ecs::World &world) {
  using game::components::MeshRenderer;
  for (const auto &[entity, visible] : m_originalVisibility) {
    if (auto *renderer = world.Get<MeshRenderer>(entity)) {
      renderer->isVisible = visible;
    }
  }
  m_originalVisibility.clear();
}

} // namespace game::debug
