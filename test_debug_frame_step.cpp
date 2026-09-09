#include "src/game/devtools/DebugTimeController.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
  } while (false)

int main() {
  game::debug::DebugTimeController time;
  time.RequestStep();
  CHECK(std::fabs(time.SimulationDelta(0.02f) - 0.02f) < 0.000001f,
        "frame step is ignored while running");

  time.SetPaused(true);
  time.RequestStep();
  CHECK(std::fabs(time.SimulationDelta(0.02f) -
                  game::debug::DebugTimeController::kStepDelta) < 0.000001f,
        "frame step advances one fixed update");
  CHECK(time.SimulationDelta(0.02f) == 0.0f,
        "frame step request is consumed exactly once");

  time.RequestStep();
  time.SetPaused(false);
  CHECK(std::fabs(time.SimulationDelta(0.02f) - 0.02f) < 0.000001f,
        "resuming clears a pending frame step");
  return 0;
}
