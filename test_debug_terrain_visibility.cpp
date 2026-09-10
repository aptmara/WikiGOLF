#include "src/game/devtools/DebugRenderState.h"
#include "src/game/devtools/DebugTerrainVisibility.h"
#include "src/ecs/World.h"
#include "src/game/components/MeshRenderer.h"
#include "src/game/components/WikiComponents.h"
#include <iostream>

int main() {
  ecs::World world;
  const ecs::Entity visibleTerrain = world.CreateEntity();
  world.Add<game::components::TerrainObject>(visibleTerrain);
  world.Add<game::components::MeshRenderer>(visibleTerrain).isVisible = true;
  const ecs::Entity hiddenTerrain = world.CreateEntity();
  world.Add<game::components::TerrainObject>(hiddenTerrain);
  world.Add<game::components::MeshRenderer>(hiddenTerrain).isVisible = false;
  const ecs::Entity other = world.CreateEntity();
  world.Add<game::components::MeshRenderer>(other).isVisible = true;

  game::debug::DebugTerrainVisibility visibility;
  game::debug::DebugRenderState state{true};
  visibility.Apply(world, &state);
  if (world.Get<game::components::MeshRenderer>(visibleTerrain)->isVisible ||
      world.Get<game::components::MeshRenderer>(hiddenTerrain)->isVisible ||
      !world.Get<game::components::MeshRenderer>(other)->isVisible) {
    std::cerr << "Terrain meshes were not isolated\n";
    return 1;
  }

  state.hideTerrainMeshes = false;
  visibility.Apply(world, &state);
  if (!world.Get<game::components::MeshRenderer>(visibleTerrain)->isVisible ||
      world.Get<game::components::MeshRenderer>(hiddenTerrain)->isVisible) {
    std::cerr << "Original terrain visibility was not restored\n";
    return 1;
  }
  return 0;
}
