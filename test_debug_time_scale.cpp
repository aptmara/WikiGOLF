#include "src/game/devtools/DebugTimeController.h"
#include <cmath>
#include <iostream>

#define CHECK_CLOSE(actual, expected, message)                                \
  do {                                                                        \
    if (std::fabs((actual) - (expected)) > 0.000001f) {                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

int main() {
  game::debug::DebugTimeController time;
  CHECK_CLOSE(time.GetTimeScale(), 1.0f, "time scale starts at 1x");

  time.SetTimeScaleIndex(0);
  CHECK_CLOSE(time.SimulationDelta(0.02f), 0.002f,
              "0.1x scales a running frame");

  time.SetPaused(true);
  time.SetTimeScaleIndex(4);
  time.RequestStep();
  CHECK_CLOSE(time.SimulationDelta(0.02f),
              game::debug::DebugTimeController::kStepDelta * 2.0f,
              "2x scales a fixed frame step");

  time.SetTimeScaleIndex(3);
  time.CycleTimeScale();
  CHECK_CLOSE(time.GetTimeScale(), 2.0f, "cycle selects the next scale");
  time.CycleTimeScale();
  CHECK_CLOSE(time.GetTimeScale(), 0.1f, "cycle wraps after 2x");
  return 0;
}
