#pragma once

#include <array>

namespace game::debug {

class DebugTimeController {
public:
  static constexpr float kStepDelta = 1.0f / 60.0f;
  static constexpr std::array<float, 5> kTimeScales = {0.1f, 0.25f, 0.5f,
                                                       1.0f, 2.0f};

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
  float GetTimeScale() const { return kTimeScales[m_timeScaleIndex]; }
  void SetTimeScaleIndex(int index) {
    if (index >= 0 && index < static_cast<int>(kTimeScales.size())) {
      m_timeScaleIndex = index;
    }
  }
  void CycleTimeScale() {
    m_timeScaleIndex = (m_timeScaleIndex + 1) % kTimeScales.size();
  }

  float SimulationDelta(float realDelta) {
    if (!m_paused) {
      return realDelta * GetTimeScale();
    }
    if (!m_stepRequested) {
      return 0.0f;
    }
    m_stepRequested = false;
    return kStepDelta * GetTimeScale();
  }

private:
  bool m_paused = false;
  bool m_stepRequested = false;
  std::size_t m_timeScaleIndex = 3;
};

} // namespace game::debug
