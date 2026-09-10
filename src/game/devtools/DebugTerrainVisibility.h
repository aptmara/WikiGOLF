#pragma once

#include "../../ecs/Entity.h"
#include <unordered_map>

namespace ecs {
class World;
}

namespace game::debug {

struct DebugRenderState;

class DebugTerrainVisibility {
public:
  void Apply(ecs::World &world, const DebugRenderState *state);

private:
  void Hide(ecs::World &world);
  void Restore(ecs::World &world);

  std::unordered_map<ecs::Entity, bool> m_originalVisibility;
};

} // namespace game::debug
