#pragma once

namespace game::debug {

class DebugTimeController {
public:
  bool IsPaused() const { return m_paused; }
  void SetPaused(bool paused) { m_paused = paused; }
  void TogglePaused() { m_paused = !m_paused; }

  float SimulationDelta(float realDelta) const {
    return m_paused ? 0.0f : realDelta;
  }

private:
  bool m_paused = false;
};

} // namespace game::debug
