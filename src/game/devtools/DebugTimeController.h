#pragma once

namespace game::debug {

class DebugTimeController {
public:
  static constexpr float kStepDelta = 1.0f / 60.0f;

  bool IsPaused() const { return m_paused; }
  void SetPaused(bool paused) {
    m_paused = paused;
    if (!m_paused) {
      m_stepRequested = false;
    }
  }
  void TogglePaused() { SetPaused(!m_paused); }
  void RequestStep() {
    if (m_paused) {
      m_stepRequested = true;
    }
  }

  float SimulationDelta(float realDelta) {
    if (!m_paused) {
      return realDelta;
    }
    if (!m_stepRequested) {
      return 0.0f;
    }
    m_stepRequested = false;
    return kStepDelta;
  }

private:
  bool m_paused = false;
  bool m_stepRequested = false;
};

} // namespace game::debug
