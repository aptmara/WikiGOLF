#include "src/game/devtools/DebugSlopeRules.h"
#include <iostream>

int main() {
  const auto flat = game::debug::EvaluateSlope(true, true, 0.99f);
  const auto slope = game::debug::EvaluateSlope(true, true, 0.95f);
  const auto airborne = game::debug::EvaluateSlope(false, true, 0.8f);
  const auto missing = game::debug::EvaluateSlope(true, false, 0.8f);
  if (flat.isOnSlope || !slope.isOnSlope || airborne.isOnSlope ||
      missing.isOnSlope || slope.angleDegrees <= flat.angleDegrees) {
    std::cerr << "Slope threshold evaluation failed\n";
    return 1;
  }
  return 0;
}
