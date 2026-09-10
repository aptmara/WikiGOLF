#pragma once

#include <cstdint>

namespace core {
struct GameContext;
}

namespace game::debug {

struct DebugColliderSettings;

class DebugEntityInspector {
public:
  void Draw(core::GameContext &ctx, DebugColliderSettings &colliderSettings);

private:
  uint32_t m_entity = 0;
};

} // namespace game::debug
