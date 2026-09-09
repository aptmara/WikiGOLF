#pragma once

#include <array>

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

private:
  void DrawSimulation(DebugTimeController &time);
  void DrawLog();

  bool m_visible = false;
  std::array<bool, 4> m_logLevels = {true, true, true, true};
  char m_logFilter[128] = {};
};

} // namespace game::debug
