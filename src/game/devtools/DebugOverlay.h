#pragma once

#include <array>
#include "DebugColliderRenderer.h"

namespace core {
struct GameContext;
}

namespace game::debug {

class DebugTimeController;

class DebugOverlay {
public:
  void Toggle() { m_visible = !m_visible; }
  bool IsVisible() const { return m_visible; }
  void Draw(core::GameContext &ctx, DebugTimeController &time);
  const DebugColliderSettings &GetColliderSettings() const {
    return m_colliderSettings;
  }

private:
  void DrawSimulation(DebugTimeController &time);
  void DrawLog();
  void DrawColliders(core::GameContext &ctx);
  void DrawSceneSelector(core::GameContext &ctx, DebugTimeController &time);

  bool m_visible = false;
  std::array<bool, 4> m_logLevels = {true, true, true, true};
  char m_logFilter[128] = {};
  DebugColliderSettings m_colliderSettings;
};

} // namespace game::debug
