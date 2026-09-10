#include "src/game/devtools/DebugCupInStatus.h"
#include <iostream>

int main() {
  const DirectX::XMFLOAT3 hole{0.0f, 1.0f, 0.0f};
  const auto ready = game::debug::EvaluateCupInStatus(
      {0.1f, 0.5f, 0.1f}, hole, 0.5f, {0.05f, 0.0f, 0.0f}, true, 1);
  if (!ready.readyForCupIn || !ready.holeInOne ||
      !ready.withinHorizontalRange || !ready.withinVerticalRange ||
      !ready.slowEnough) {
    std::cerr << "Valid cup-in conditions were rejected\n";
    return 1;
  }

  const auto fast = game::debug::EvaluateCupInStatus(
      {0.1f, 0.5f, 0.1f}, hole, 0.5f, {0.2f, 0.0f, 0.0f}, true, 1);
  const auto high = game::debug::EvaluateCupInStatus(
      {0.1f, 1.1f, 0.1f}, hole, 0.5f, {}, true, 1);
  const auto wide = game::debug::EvaluateCupInStatus(
      {0.5f, 0.5f, 0.0f}, hole, 0.5f, {}, true, 1);
  if (fast.readyForCupIn || fast.slowEnough || high.readyForCupIn ||
      high.withinVerticalRange || wide.readyForCupIn ||
      wide.withinHorizontalRange) {
    std::cerr << "Invalid cup-in conditions were accepted\n";
    return 1;
  }
  return 0;
}
