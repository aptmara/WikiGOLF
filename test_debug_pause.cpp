#include "src/game/devtools/DebugTimeController.h"
#include <cmath>
#include <iostream>

#define CHECK(condition, message)                                             \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "[FAIL] " << message << "\n";                            \
      return 1;                                                               \
    }                                                                         \
    std::cout << "[PASS] " << message << "\n";                              \
  } while (false)

int main() {
  game::debug::DebugTimeController time;
  CHECK(!time.IsPaused(), "simulation starts unpaused");
  CHECK(std::fabs(time.SimulationDelta(0.016f) - 0.016f) < 0.000001f,
        "unpaused simulation receives the real delta");

  time.TogglePaused();
  CHECK(time.IsPaused(), "pause toggle stops the simulation");
  CHECK(time.SimulationDelta(0.016f) == 0.0f,
        "paused simulation receives a zero delta");

  time.TogglePaused();
  CHECK(!time.IsPaused(), "second pause toggle resumes the simulation");
  return 0;
}
