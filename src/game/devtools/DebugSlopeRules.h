#pragma once

#include <algorithm>
#include <cmath>

namespace game::debug {

inline constexpr float kSlopeFlatNormalYThreshold = 0.98f;

struct DebugSlopeEvaluation {
  bool isOnSlope = false;
  float normalY = 1.0f;
  float angleDegrees = 0.0f;
};

inline DebugSlopeEvaluation EvaluateSlope(bool grounded, bool terrainValid,
                                          float normalY) {
  const float clampedNormalY = std::clamp(normalY, -1.0f, 1.0f);
  return {grounded && terrainValid &&
              clampedNormalY < kSlopeFlatNormalYThreshold,
          clampedNormalY,
          std::acos(clampedNormalY) * 180.0f / 3.14159265358979323846f};
}

} // namespace game::debug
