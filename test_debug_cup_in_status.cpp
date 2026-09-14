#include "src/game/devtools/DebugCupInStatus.h"
#include "src/game/utils/GolfCupPhysics.h"
#include <iostream>

int main() {
  using namespace game::physics;
  const DirectX::XMFLOAT3 hole{0.0f, 1.0f, 0.0f};
  const float radius = kGolfCupRadius;
  const float floorBallY =
      hole.y - (kGolfCupDepth - kGolfCupBallVisualPadding) + kBallRadius;

  const auto ready = game::debug::EvaluateCupInStatus(
      {0.05f, floorBallY, 0.05f}, hole, radius, {0.05f, 0.0f, 0.0f}, true, 1);
  if (!ready.readyForCupIn || !ready.holeInOne ||
      !ready.withinHorizontalRange || !ready.withinVerticalRange ||
      !ready.slowEnough || ready.ballPosition.x != 0.05f ||
      ready.holePosition.y != 1.0f) {
    std::cerr << "Valid cup-in conditions were rejected\n";
    return 1;
  }

  // 底に達していれば速度は判定条件にならない（壁と底で止まる）
  const auto moving = game::debug::EvaluateCupInStatus(
      {0.05f, floorBallY, 0.05f}, hole, radius, {0.4f, 0.0f, 0.0f}, true, 1);
  const auto high = game::debug::EvaluateCupInStatus(
      {0.05f, hole.y + kBallRadius, 0.05f}, hole, radius, {}, true, 1);
  const auto wide = game::debug::EvaluateCupInStatus(
      {0.3f, floorBallY, 0.0f}, hole, radius, {}, true, 1);
  if (!moving.readyForCupIn || moving.slowEnough || high.readyForCupIn ||
      high.withinVerticalRange || wide.readyForCupIn ||
      wide.withinHorizontalRange) {
    std::cerr << "Invalid cup-in conditions were accepted\n";
    return 1;
  }
  return 0;
}
