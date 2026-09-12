#pragma once

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace game::utils {

inline constexpr float kAutoClubMaxPowerRatio = 0.70f;
inline constexpr float kAutoPutterMaxPowerRatio = 0.40f;
inline constexpr float kAbnormalPutterPathSlope = 0.12f;
inline constexpr float kSlopeComparisonTolerance = 1e-5f;
inline constexpr float kPutterPathSampleSpacing = 0.5f;
inline constexpr float kPutterSlopeSampleOffset = 0.35f;

struct AimPinClubCandidate {
  float requiredPowerRatio = 0.0f;
  float baseCarryDistance = 0.0f;
  bool isPutter = false;
};

inline std::size_t SelectAimPinClubIndex(
    const std::vector<AimPinClubCandidate> &clubs,
    bool pathHasAbnormalSlope) {
  if (clubs.empty()) {
    return std::numeric_limits<std::size_t>::max();
  }

  std::size_t bestIndex = std::numeric_limits<std::size_t>::max();
  float bestUtilization = -1.0f;
  for (std::size_t i = 0; i < clubs.size(); ++i) {
    const auto &club = clubs[i];
    const float limit =
        club.isPutter ? kAutoPutterMaxPowerRatio : kAutoClubMaxPowerRatio;
    if (!std::isfinite(club.requiredPowerRatio) ||
        club.requiredPowerRatio < 0.0f || club.requiredPowerRatio > limit ||
        (club.isPutter && pathHasAbnormalSlope)) {
      continue;
    }

    const float utilization = club.requiredPowerRatio / limit;
    if (utilization > bestUtilization) {
      bestUtilization = utilization;
      bestIndex = i;
    }
  }

  if (bestIndex != std::numeric_limits<std::size_t>::max()) {
    return bestIndex;
  }

  std::size_t fallbackIndex = std::numeric_limits<std::size_t>::max();
  float longestCarry = -1.0f;
  for (std::size_t i = 0; i < clubs.size(); ++i) {
    if (!clubs[i].isPutter && clubs[i].baseCarryDistance > longestCarry) {
      longestCarry = clubs[i].baseCarryDistance;
      fallbackIndex = i;
    }
  }
  return fallbackIndex != std::numeric_limits<std::size_t>::max()
             ? fallbackIndex
             : 0;
}

template <typename HeightSampler>
bool HasAbnormalSlopeBetween(const DirectX::XMFLOAT3 &from,
                             const DirectX::XMFLOAT3 &to,
                             HeightSampler &&sampleHeight) {
  const float dx = to.x - from.x;
  const float dz = to.z - from.z;
  const float distance = std::sqrt(dx * dx + dz * dz);
  const int sampleCount =
      (std::max)(1, static_cast<int>(std::ceil(
                        distance / kPutterPathSampleSpacing)));
  const float offset = kPutterSlopeSampleOffset;

  for (int i = 0; i < sampleCount; ++i) {
    const float t = (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(sampleCount);
    const float x = from.x + dx * t;
    const float z = from.z + dz * t;
    const float gradientX =
        (sampleHeight(x + offset, z) - sampleHeight(x - offset, z)) /
        (2.0f * offset);
    const float gradientZ =
        (sampleHeight(x, z + offset) - sampleHeight(x, z - offset)) /
        (2.0f * offset);
    const float slope =
        std::sqrt(gradientX * gradientX + gradientZ * gradientZ);
    if (slope >
        kAbnormalPutterPathSlope + kSlopeComparisonTolerance) {
      return true;
    }
  }
  return false;
}

} // namespace game::utils
